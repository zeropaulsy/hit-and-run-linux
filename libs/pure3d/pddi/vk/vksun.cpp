//=============================================================================
// Linux port: sun lighting with ray-traced shadows (see vksun.hpp)
//=============================================================================

#include <pddi/vk/vksun.hpp>
#include <pddi/vk/vktex.hpp>

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const int kMaxInstances = 16384;
static const unsigned kKeepMs = 10000;          // a static instance out of view stays this long
static const float kHugeSize = 1500.0f;         // a caster bigger than this (sky dome) never casts
static const unsigned kMaxBuildsPerFrame = 96;
static const unsigned kMaxCasterIds = 65536;    // alpha-tested casters need an id below this (the table's size)
static const int kMaskSize = 64;                // texels across an alpha mask
static const int kMaxMasks = 1024;
static const unsigned kGeometryWords = 8 * 1024 * 1024;  // alpha-tested casters' UVs and triangles (32 MB)

static int s_wanted = -1;
static bool s_enabled = true;
static float s_eyeToWorld[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
static unsigned s_version = 1;
static float s_direction[3] = { 0.36f, 0.80f, 0.48f };
static bool s_fixedDirection = false;
static float s_levels[3] = { 0.62f, 1.25f, 0.65f };
static bool s_log = false;

// the sun's height per level, degrees (0: the game's light as it is). The game's key light is low (about 30 degrees),
// fine for its own shading but long shadows: level 1 is a midday sun, from the same compass direction.
static const float kLevelElevation[] = { 68.0f };
static int s_level = -1;
static float s_elevation = -1.0f;      // SHAR_SUN_ELEVATION: every level

// the frame's strongest directional light (world space)
static float s_bestLight = 0.0f;
static float s_bestDirection[3];

static float EnvFloat(const char* name, float fallback)
{
    const char* e = getenv(name);
    return e && e[0] ? (float)atof(e) : fallback;
}

static void Normalise(float* v)
{
    float l = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if(l > 0.0f)
    {
        v[0] /= l;
        v[1] /= l;
        v[2] /= l;
    }
}

bool pvkSunWanted()
{
    if(s_wanted < 0)
    {
        const char* e = getenv("SHAR_SUN");
        s_wanted = e && e[0] && e[0] != '0';
        s_levels[0] = EnvFloat("SHAR_SUN_SHADOW", s_levels[0]);
        s_levels[1] = EnvFloat("SHAR_SUN_LIGHT", s_levels[1]);
        s_levels[2] = EnvFloat("SHAR_SUN_LIT_SHADOW", s_levels[2]);
        const char* d = getenv("SHAR_SUN_DIR");
        if(d && sscanf(d, "%f,%f,%f", &s_direction[0], &s_direction[1], &s_direction[2]) == 3)
        {
            Normalise(s_direction);
            s_fixedDirection = true;
        }
        s_elevation = EnvFloat("SHAR_SUN_ELEVATION", -1.0f);
        const char* l = getenv("SHAR_SUN_LOG");
        s_log = l && l[0] == '1';
    }
    return s_wanted > 0;
}

void pvkSunSetLevel(int level)
{
    s_level = level;
}

// the game's light raised (or lowered) to the level's height, the compass direction kept
static void Elevate(float* d)
{
    pvkSunWanted();
    float elevation = s_elevation >= 0.0f ? s_elevation :
                      s_level >= 0 && s_level < (int)(sizeof(kLevelElevation) / sizeof(kLevelElevation[0])) ?
                      kLevelElevation[s_level] : 0.0f;
    float horizontal = sqrtf(d[0] * d[0] + d[2] * d[2]);
    if(elevation <= 0.0f || horizontal < 1e-4f)
        return;
    float e = elevation * 3.14159265f / 180.0f;
    d[0] = d[0] / horizontal * cosf(e);
    d[2] = d[2] / horizontal * cosf(e);
    d[1] = sinf(e);
}

bool pvkSunReady()
{
    return g_pvk && g_pvk->rayQuery && pvkSunWanted();
}

bool pvkSunOn()
{
    return s_enabled && pvkSunReady();
}

void pvkSunToggle()
{
    if(!pvkSunReady())
        return;
    s_enabled = !s_enabled;
    s_version++;
    fprintf(stderr, "PDDI: sun %s\n", s_enabled ? "on" : "off");
}

// GL eye space is p3d's camera space with z negated: eye -> world = diag(1, 1, -1, 1) * cameraToWorld
void pvkSetViewMatrix(const float* cameraToWorld)
{
    float m[16];
    memcpy(m, cameraToWorld, sizeof(m));
    m[8] = -m[8];
    m[9] = -m[9];
    m[10] = -m[10];
    m[11] = -m[11];
    if(memcmp(m, s_eyeToWorld, sizeof(m)) == 0)
        return;
    memcpy(s_eyeToWorld, m, sizeof(m));
    s_version++;
}

const float* pvkSunEyeToWorld()
{
    return s_eyeToWorld;
}

unsigned pvkSunVersion()
{
    return s_version;
}

void pvkSunParameters(float* direction, float* levels)
{
    memcpy(direction, s_direction, sizeof(s_direction));
    memcpy(levels, s_levels, sizeof(s_levels));
}

void pvkSunNoteLight(const float* eye, const float* colour)
{
    float strength = 0.3f * colour[0] + 0.59f * colour[1] + 0.11f * colour[2];
    if(strength <= s_bestLight)
        return;
    float w[3];
    for(int c = 0; c < 3; c++)
        w[c] = eye[0] * s_eyeToWorld[c] + eye[1] * s_eyeToWorld[4 + c] + eye[2] * s_eyeToWorld[8 + c];
    Normalise(w);
    if(w[1] < 0.05f)        // from below the horizon: a fill light
        return;
    s_bestLight = strength;
    memcpy(s_bestDirection, w, sizeof(w));
}

//-----------------------------------------------------------------------------
// Triangles
//-----------------------------------------------------------------------------

// the triangles of a draw as a list (strips unrolled, their winding kept, degenerate ones dropped); malloc'd
static unsigned* Triangles(const unsigned short* indices, unsigned count, bool strip, unsigned* triangles)
{
    unsigned n = strip ? (count >= 3 ? count - 2 : 0) : count / 3;
    unsigned* list = (unsigned*)malloc((n ? n : 1) * 3 * sizeof(unsigned));
    unsigned t = 0;
    for(unsigned i = 0; i < n; i++)
    {
        unsigned a, b, c;
        if(strip)
        {
            a = i;
            b = (i & 1) ? i + 2 : i + 1;
            c = (i & 1) ? i + 1 : i + 2;
        }
        else
        {
            a = i * 3;
            b = a + 1;
            c = a + 2;
        }
        if(indices)
        {
            a = indices[a];
            b = indices[b];
            c = indices[c];
        }
        if(a == b || b == c || a == c)
            continue;
        list[t * 3] = a;
        list[t * 3 + 1] = b;
        list[t * 3 + 2] = c;
        t++;
    }
    *triangles = t;
    return list;
}

void pvkSunSmoothNormals(const float* p, unsigned vertexCount, const unsigned short* indices, unsigned count,
                         bool strip, float* normals)
{
    memset(normals, 0, vertexCount * 12);
    if(vertexCount == 0)
        return;
    // weld by position (1 mm): group[v] is the first vertex at v's position
    unsigned size = 1;
    while(size < vertexCount * 2)
        size <<= 1;
    int* table = (int*)malloc(size * sizeof(int));
    unsigned* group = (unsigned*)malloc(vertexCount * sizeof(unsigned));
    memset(table, 0xff, size * sizeof(int));
    for(unsigned v = 0; v < vertexCount; v++)
    {
        int q[3] = { (int)lrintf(p[v * 3] * 1000.0f), (int)lrintf(p[v * 3 + 1] * 1000.0f), (int)lrintf(p[v * 3 + 2] * 1000.0f) };
        unsigned h = ((unsigned)q[0] * 73856093u) ^ ((unsigned)q[1] * 19349663u) ^ ((unsigned)q[2] * 83492791u);
        group[v] = v;
        for(unsigned s = h & (size - 1);; s = (s + 1) & (size - 1))
        {
            if(table[s] < 0)
            {
                table[s] = (int)v;
                break;
            }
            unsigned o = (unsigned)table[s];
            if(lrintf(p[o * 3] * 1000.0f) == q[0] && lrintf(p[o * 3 + 1] * 1000.0f) == q[1] &&
               lrintf(p[o * 3 + 2] * 1000.0f) == q[2])
            {
                group[v] = o;
                break;
            }
        }
    }
    free(table);

    unsigned triangles;
    unsigned* list = Triangles(indices, count, strip, &triangles);
    for(unsigned t = 0; t < triangles; t++)
    {
        unsigned a = list[t * 3], b = list[t * 3 + 1], c = list[t * 3 + 2];
        if(a >= vertexCount || b >= vertexCount || c >= vertexCount)
            continue;
        float e1[3], e2[3];
        for(int k = 0; k < 3; k++)
        {
            e1[k] = p[b * 3 + k] - p[a * 3 + k];
            e2[k] = p[c * 3 + k] - p[a * 3 + k];
        }
        float n[3] = { e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0] };
        unsigned g[3] = { group[a], group[b], group[c] };
        for(int i = 0; i < 3; i++)
            for(int k = 0; k < 3; k++)
                normals[g[i] * 3 + k] += n[k];
    }
    free(list);
    // a group's first vertex comes before the others: normalised by the time they copy it
    for(unsigned v = 0; v < vertexCount; v++)
    {
        if(group[v] != v)
            memcpy(&normals[v * 3], &normals[group[v] * 3], 12);
        else
            Normalise(&normals[v * 3]);
    }
    free(group);
}

//-----------------------------------------------------------------------------
// Bottom level: one per prim buffer
//-----------------------------------------------------------------------------

struct Blas
{
    VkAccelerationStructureKHR as;
    pvkBuffer storage;
    bool alpha;                 // alpha-tested: UVs and triangles in the geometry buffer
    unsigned geometry, geometryWords;
    VkDeviceAddress address;
    unsigned version;
    int builds;
    bool live;
    bool huge;
};

static Blas* s_blas;
static unsigned s_blasCount, s_blasCapacity;
static unsigned* s_freeIds;
static unsigned s_freeCount, s_freeCapacity;
static pthread_mutex_t s_deadLock = PTHREAD_MUTEX_INITIALIZER;
static unsigned* s_dead;
static unsigned s_deadCount, s_deadCapacity;
static unsigned s_buildsThisFrame;

static VkDeviceAddress BufferAddress(const pvkBuffer& b)
{
    VkBufferDeviceAddressInfo info = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO };
    info.buffer = b.buffer;
    return vkGetBufferDeviceAddress(g_pvk->device, &info);
}

static void GeometryFree(unsigned offset, unsigned words);

static void ReleaseBlas(Blas& b)
{
    g_pvk->RetireAccel(b.as);
    g_pvk->Retire(b.storage);
    if(b.alpha)
        GeometryFree(b.geometry, b.geometryWords);
    b.alpha = false;
    b.as = VK_NULL_HANDLE;
    b.address = 0;
}

static const VkBufferUsageFlags kInputUsage = VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
                                              VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

// scratch for one build (retired with the frame), its address aligned as builds need
static bool Scratch(VkDeviceSize size, VkDeviceAddress* address)
{
    pvkBuffer scratch;
    if(!g_pvk->CreateBuffer(size + 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
                            false, &scratch))
        return false;
    *address = (BufferAddress(scratch) + 255) & ~(VkDeviceAddress)255;
    g_pvk->Retire(scratch);
    return true;
}

//-----------------------------------------------------------------------------
// Alpha-tested casters: the table (a uvec4 per id: UV and triangle offsets in the geometry, mask | compare << 16,
// alpha ref), the masks (64x64 alpha bytes per texture) and the geometry (first fit; a range freed while a frame in
// flight reads it can only misplace a shadow for that frame)
//-----------------------------------------------------------------------------

static pvkBuffer s_casterTable, s_masks, s_geometry;
struct Range { unsigned offset, words; };
static Range* s_geometryFree;
static unsigned s_geometryFreeCount, s_geometryFreeCapacity;

static bool GeometryAlloc(unsigned words, unsigned* offset)
{
    for(unsigned i = 0; i < s_geometryFreeCount; i++)
    {
        Range& r = s_geometryFree[i];
        if(r.words < words)
            continue;
        *offset = r.offset;
        r.offset += words;
        r.words -= words;
        if(r.words == 0)
        {
            memmove(&s_geometryFree[i], &s_geometryFree[i + 1], (s_geometryFreeCount - i - 1) * sizeof(Range));
            s_geometryFreeCount--;
        }
        return true;
    }
    return false;
}

static void GeometryFree(unsigned offset, unsigned words)
{
    // in offset order, merged with the neighbours it touches
    unsigned i = 0;
    while(i < s_geometryFreeCount && s_geometryFree[i].offset < offset)
        i++;
    bool before = i > 0 && s_geometryFree[i - 1].offset + s_geometryFree[i - 1].words == offset;
    bool after = i < s_geometryFreeCount && offset + words == s_geometryFree[i].offset;
    if(before && after)
    {
        s_geometryFree[i - 1].words += words + s_geometryFree[i].words;
        memmove(&s_geometryFree[i], &s_geometryFree[i + 1], (s_geometryFreeCount - i - 1) * sizeof(Range));
        s_geometryFreeCount--;
    }
    else if(before)
        s_geometryFree[i - 1].words += words;
    else if(after)
    {
        s_geometryFree[i].offset = offset;
        s_geometryFree[i].words += words;
    }
    else
    {
        if(s_geometryFreeCount == s_geometryFreeCapacity)
        {
            s_geometryFreeCapacity = s_geometryFreeCapacity * 2 + 64;
            s_geometryFree = (Range*)realloc(s_geometryFree, s_geometryFreeCapacity * sizeof(Range));
        }
        memmove(&s_geometryFree[i + 1], &s_geometryFree[i], (s_geometryFreeCount - i) * sizeof(Range));
        s_geometryFree[i].offset = offset;
        s_geometryFree[i].words = words;
        s_geometryFreeCount++;
    }
}
static pthread_mutex_t s_maskLock = PTHREAD_MUTEX_INITIALIZER;
static pvkTexture* s_maskOwner[kMaxMasks];

static int MaskSlot(pvkTexture* texture)
{
    if(!s_masks.memory.mapped)
        return -1;
    pthread_mutex_lock(&s_maskLock);
    int slot = -1, empty = -1;
    for(int i = 0; i < kMaxMasks && slot < 0; i++)
    {
        if(s_maskOwner[i] == texture)
            slot = i;
        else if(!s_maskOwner[i] && empty < 0)
            empty = i;
    }
    if(slot < 0 && empty >= 0 &&
       texture->AlphaMask(s_masks.memory.mapped + (size_t)empty * kMaskSize * kMaskSize, kMaskSize))
    {
        s_maskOwner[empty] = texture;
        slot = empty;
    }
    pthread_mutex_unlock(&s_maskLock);
    return slot;
}

void pvkSunForgetTexture(pvkTexture* texture)
{
    pthread_mutex_lock(&s_maskLock);
    for(int i = 0; i < kMaxMasks; i++)
        if(s_maskOwner[i] == texture)
            s_maskOwner[i] = NULL;
    pthread_mutex_unlock(&s_maskLock);
}

bool pvkSunBuffers(VkDescriptorBufferInfo* buffers)
{
    if(!s_casterTable.buffer || !s_masks.buffer || !s_geometry.buffer)
        return false;
    const pvkBuffer* b[3] = { &s_casterTable, &s_masks, &s_geometry };
    for(int i = 0; i < 3; i++)
    {
        buffers[i].buffer = b[i]->buffer;
        buffers[i].offset = 0;
        buffers[i].range = b[i]->size;
    }
    return true;
}

// alphaSlot >= 0: alpha-tested (uvs, the mask and the test written to the table at id)
static bool BuildBlas(Blas& b, unsigned id, const float* positions, const float* uvs, unsigned vertexCount,
                      const unsigned short* indices, unsigned count, bool strip, int alphaSlot, int alphaCompare,
                      float alphaRef)
{
    bool alpha = alphaSlot >= 0 && uvs && id < kMaxCasterIds && s_casterTable.memory.mapped;
    b.alpha = false;
    unsigned triangles;
    unsigned* list = Triangles(indices, count, strip, &triangles);
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    unsigned kept = 0;
    for(unsigned t = 0; t < triangles; t++)
    {
        unsigned* tri = &list[t * 3];
        if(tri[0] >= vertexCount || tri[1] >= vertexCount || tri[2] >= vertexCount)
            continue;
        memmove(&list[kept * 3], tri, 12);
        kept++;
        for(int i = 0; i < 3; i++)
            for(int k = 0; k < 3; k++)
            {
                float v = positions[tri[i] * 3 + k];
                lo[k] = v < lo[k] ? v : lo[k];
                hi[k] = v > hi[k] ? v : hi[k];
            }
    }
    triangles = kept;
    if(triangles == 0)
    {
        free(list);
        return false;
    }
    float d[3] = { hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2] };
    b.huge = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]) > kHugeSize;

    // the input in host-visible memory, read by the build straight from there
    pvkBuffer input;
    VkDeviceSize vertexBytes = (VkDeviceSize)vertexCount * 12;
    VkDeviceSize triangleBytes = (VkDeviceSize)triangles * 12;
    if(!g_pvk->CreateBuffer(vertexBytes + triangleBytes, kInputUsage, true, &input))
    {
        free(list);
        return false;
    }
    memcpy(input.memory.mapped, positions, vertexBytes);
    memcpy(input.memory.mapped + vertexBytes, list, triangleBytes);
    VkDeviceAddress inputAddress = BufferAddress(input);
    unsigned offset = 0, words = vertexCount * 2 + triangles * 3;
    if(alpha && GeometryAlloc(words, &offset))
    {
        unsigned* g = (unsigned*)s_geometry.memory.mapped + offset;
        memcpy(g, uvs, (size_t)vertexCount * 8);
        memcpy(g + vertexCount * 2, list, triangleBytes);
        unsigned* row = (unsigned*)s_casterTable.memory.mapped + id * 4;
        row[0] = offset;
        row[1] = offset + vertexCount * 2;
        row[2] = (unsigned)alphaSlot | ((unsigned)alphaCompare << 16);
        memcpy(&row[3], &alphaRef, 4);
        b.geometry = offset;
        b.geometryWords = words;
        b.alpha = true;         // freed with the structure from here on
    }
    else if(alpha)
    {
        // no room: it would cast as solid cards
        free(list);
        g_pvk->Retire(input);
        return false;
    }
    free(list);

    VkAccelerationStructureGeometryKHR geometry = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR };
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = alpha ? 0 : VK_GEOMETRY_OPAQUE_BIT_KHR;   // alpha-tested: the shaders decide
    VkAccelerationStructureGeometryTrianglesDataKHR& tri = geometry.geometry.triangles;
    tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexData.deviceAddress = inputAddress;
    tri.vertexStride = 12;
    tri.maxVertex = vertexCount - 1;
    tri.indexType = VK_INDEX_TYPE_UINT32;
    tri.indexData.deviceAddress = inputAddress + vertexBytes;

    VkAccelerationStructureBuildGeometryInfoKHR build = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR };
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = b.builds > 3 ? VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR :     // rebuilt often
                                 VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
    VkAccelerationStructureBuildSizesInfoKHR sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(g_pvk->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build,
                                            &triangles, &sizes);

    VkDeviceAddress scratch;
    if(!g_pvk->CreateBuffer(sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false, &b.storage) ||
       !Scratch(sizes.buildScratchSize, &scratch))
    {
        g_pvk->Retire(input);
        g_pvk->Retire(b.storage);
        return false;
    }
    VkAccelerationStructureCreateInfoKHR create = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    create.buffer = b.storage.buffer;
    create.size = sizes.accelerationStructureSize;
    create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    if(vkCreateAccelerationStructureKHR(g_pvk->device, &create, NULL, &b.as) != VK_SUCCESS)
    {
        b.as = VK_NULL_HANDLE;
        g_pvk->Retire(input);
        g_pvk->Retire(b.storage);
        return false;
    }
    build.dstAccelerationStructure = b.as;
    build.scratchData.deviceAddress = scratch;
    VkAccelerationStructureBuildRangeInfoKHR range = { triangles, 0, 0, 0 };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    vkCmdBuildAccelerationStructuresKHR(g_pvk->UploadCmd(), 1, &build, &ranges);
    g_pvk->Retire(input);       // read by this frame's upload commands only
    b.alpha = alpha;

    VkAccelerationStructureDeviceAddressInfoKHR ai = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
    ai.accelerationStructure = b.as;
    b.address = vkGetAccelerationStructureDeviceAddressKHR(g_pvk->device, &ai);
    s_buildsThisFrame++;
    return true;
}

void pvkSunForget(unsigned id)
{
    if(!id)
        return;
    pthread_mutex_lock(&s_deadLock);
    if(s_deadCount == s_deadCapacity)
    {
        s_deadCapacity = s_deadCapacity * 2 + 64;
        s_dead = (unsigned*)realloc(s_dead, s_deadCapacity * sizeof(unsigned));
    }
    s_dead[s_deadCount++] = id;
    pthread_mutex_unlock(&s_deadLock);
}

static unsigned NewId()
{
    if(s_freeCount)
        return s_freeIds[--s_freeCount];
    if(s_blasCount + 1 >= s_blasCapacity)
    {
        s_blasCapacity = s_blasCapacity * 2 + 1024;
        s_blas = (Blas*)realloc(s_blas, s_blasCapacity * sizeof(Blas));
    }
    unsigned id = ++s_blasCount;    // 0 is "none"
    memset(&s_blas[id], 0, sizeof(Blas));
    return id;
}

//-----------------------------------------------------------------------------
// Instances: this frame's draws and the static ones seen lately
//-----------------------------------------------------------------------------

struct Instance
{
    unsigned id;
    int key[12];                // the transform quantised (rotation 1/1024, translation 1/64 m)
    float transform[12];        // VkTransformMatrixKHR: 3 rows of 4
    unsigned lastFrame;
    unsigned streak;            // frames running seen with this transform
    unsigned lastMs;
    bool keep;                  // pre-lit world: may stay out of view, in lit draws' rays (mask)
};

static Instance* s_instances;
static unsigned s_instanceCount, s_instanceCapacity;
static int* s_hash;
static unsigned s_hashSize;
static unsigned s_frame = 1;

static unsigned HashInstance(unsigned id, const int* key)
{
    unsigned h = id * 2654435761u;
    for(int i = 0; i < 12; i++)
        h = (h ^ (unsigned)key[i]) * 16777619u;
    return h;
}

static void RebuildHash()
{
    unsigned want = 1024;
    while(want < s_instanceCount * 2 + 64)
        want <<= 1;
    if(want != s_hashSize)
    {
        free(s_hash);
        s_hash = (int*)malloc(want * sizeof(int));
        s_hashSize = want;
    }
    memset(s_hash, 0xff, s_hashSize * sizeof(int));
    for(unsigned i = 0; i < s_instanceCount; i++)
    {
        unsigned s = HashInstance(s_instances[i].id, s_instances[i].key) & (s_hashSize - 1);
        while(s_hash[s] >= 0)
            s = (s + 1) & (s_hashSize - 1);
        s_hash[s] = (int)i;
    }
}

void pvkSunAddCaster(unsigned* id, unsigned version, const float* positions, const float* uvs, unsigned vertexCount,
                     const unsigned short* indices, unsigned count, bool strip, const float* mv, bool keep,
                     pvkTexture* alphaTexture, int alphaCompare, float alphaRef)
{
    if(!pvkSunOn() || vertexCount == 0)
        return;
    if(!*id)
        *id = NewId();
    Blas& b = s_blas[*id];
    if(!b.live || b.version != version)
    {
        // rebuilt every frame (skinned characters, animated things): a budget per frame, the old shape meanwhile
        if(s_buildsThisFrame >= kMaxBuildsPerFrame && b.builds > 3)
        {
            if(!b.as)
                return;
        }
        else
        {
            ReleaseBlas(b);
            b.live = true;
            b.version = version;
            b.builds++;
            int slot = alphaTexture ? MaskSlot(alphaTexture) : -1;
            if(alphaTexture && slot < 0)
                return;         // no mask: it would cast as a solid card
            if(!BuildBlas(b, *id, positions, uvs, vertexCount, indices, count, strip, slot, alphaCompare, alphaRef))
                return;
        }
    }
    if(!b.as || b.huge)
        return;

    // object -> world = model-view (GL eye) * eye -> world, row vectors; the instance's matrix is its transpose
    const float* e = s_eyeToWorld;
    float w[16];
    for(int r = 0; r < 4; r++)
        for(int c = 0; c < 4; c++)
            w[r * 4 + c] = mv[r * 4] * e[c] + mv[r * 4 + 1] * e[4 + c] + mv[r * 4 + 2] * e[8 + c] + mv[r * 4 + 3] * e[12 + c];
    // drawn at the camera (a ring around it, moved with it each frame): not scenery
    float dx = w[12] - e[12], dy = w[13] - e[13], dz = w[14] - e[14];
    if(dx * dx + dy * dy + dz * dz < 0.25f)
        return;
    Instance in;
    memset(&in, 0, sizeof(in));
    in.id = *id;
    in.keep = keep;
    for(int row = 0; row < 3; row++)
    {
        for(int col = 0; col < 3; col++)
            in.transform[row * 4 + col] = w[col * 4 + row];
        in.transform[row * 4 + 3] = w[12 + row];
    }
    for(int i = 0; i < 12; i++)
        in.key[i] = (int)lrintf(in.transform[i] * ((i & 3) == 3 ? 64.0f : 1024.0f));

    if(s_hashSize == 0)
        RebuildHash();
    unsigned s = HashInstance(in.id, in.key) & (s_hashSize - 1);
    for(; s_hash[s] >= 0; s = (s + 1) & (s_hashSize - 1))
    {
        Instance& o = s_instances[s_hash[s]];
        if(o.id == in.id && memcmp(o.key, in.key, sizeof(in.key)) == 0)
        {
            if(o.lastFrame != s_frame)
                o.streak = o.lastFrame + 1 == s_frame ? o.streak + 1 : 1;
            o.lastFrame = s_frame;
            o.lastMs = SDL_GetTicks();
            o.keep = keep;
            return;
        }
    }
    if(s_instanceCount * 2 + 64 > s_hashSize)
    {
        // the table is full enough: append and rebuild
        if(s_instanceCount == s_instanceCapacity)
        {
            s_instanceCapacity = s_instanceCapacity * 2 + 1024;
            s_instances = (Instance*)realloc(s_instances, s_instanceCapacity * sizeof(Instance));
        }
        in.lastFrame = s_frame;
        in.streak = 1;
        in.lastMs = SDL_GetTicks();
        s_instances[s_instanceCount++] = in;
        RebuildHash();
        return;
    }
    if(s_instanceCount == s_instanceCapacity)
    {
        s_instanceCapacity = s_instanceCapacity * 2 + 1024;
        s_instances = (Instance*)realloc(s_instances, s_instanceCapacity * sizeof(Instance));
    }
    in.lastFrame = s_frame;
    in.streak = 1;
    in.lastMs = SDL_GetTicks();
    s_hash[s] = (int)s_instanceCount;
    s_instances[s_instanceCount++] = in;
}

//-----------------------------------------------------------------------------
// Top level: one per frame in flight, rebuilt every frame
//-----------------------------------------------------------------------------

struct Scene
{
    VkAccelerationStructureKHR as;
    pvkBuffer storage, scratch, instances;
    VkDeviceAddress scratchAddress, instanceAddress;
};

static Scene s_scenes[pvkFrames];
static bool s_scenesMade = false;
static bool s_scenesFailed = false;

static void TopLevelGeometry(VkAccelerationStructureGeometryKHR& geometry, VkAccelerationStructureBuildGeometryInfoKHR& build,
                             VkDeviceAddress instances)
{
    memset(&geometry, 0, sizeof(geometry));
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometry.geometry.instances.data.deviceAddress = instances;
    memset(&build, 0, sizeof(build));
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    build.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
}

static bool MakeScenes()
{
    VkAccelerationStructureGeometryKHR geometry;
    VkAccelerationStructureBuildGeometryInfoKHR build;
    TopLevelGeometry(geometry, build, 0);
    unsigned max = kMaxInstances;
    VkAccelerationStructureBuildSizesInfoKHR sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(g_pvk->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &max,
                                            &sizes);
    for(int f = 0; f < pvkFrames; f++)
    {
        Scene& sc = s_scenes[f];
        if(!g_pvk->CreateBuffer(sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false, &sc.storage) ||
           !g_pvk->CreateBuffer(sizes.buildScratchSize + 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false, &sc.scratch) ||
           !g_pvk->CreateBuffer((VkDeviceSize)kMaxInstances * sizeof(VkAccelerationStructureInstanceKHR), kInputUsage,
                                true, &sc.instances))
            return false;
        if(f == 0 && (!g_pvk->CreateBuffer((VkDeviceSize)kMaxCasterIds * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true,
                                           &s_casterTable) ||
                      !g_pvk->CreateBuffer((VkDeviceSize)kMaxMasks * kMaskSize * kMaskSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                           true, &s_masks) ||
                      !g_pvk->CreateBuffer((VkDeviceSize)kGeometryWords * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true,
                                           &s_geometry)))
            return false;
        if(f == 0)
        {
            s_geometryFreeCount = 0;
            GeometryFree(0, kGeometryWords);
        }
        sc.scratchAddress = (BufferAddress(sc.scratch) + 255) & ~(VkDeviceAddress)255;
        sc.instanceAddress = BufferAddress(sc.instances);
        VkAccelerationStructureCreateInfoKHR create = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        create.buffer = sc.storage.buffer;
        create.size = sizes.accelerationStructureSize;
        create.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
        if(vkCreateAccelerationStructureKHR(g_pvk->device, &create, NULL, &sc.as) != VK_SUCCESS)
        {
            sc.as = VK_NULL_HANDLE;
            return false;
        }
    }
    return true;
}

VkAccelerationStructureKHR pvkSunScene()
{
    if(!pvkSunReady() || s_scenesFailed)
        return VK_NULL_HANDLE;
    if(!s_scenesMade)
    {
        s_scenesMade = true;
        if(!MakeScenes())
        {
            fprintf(stderr, "PDDI: cannot make the sun's scene\n");
            s_scenesFailed = true;
            return VK_NULL_HANDLE;
        }
    }
    return s_scenes[g_pvk->FrameSerial() % pvkFrames].as;
}

static void FillInstance(VkAccelerationStructureInstanceKHR& out, const Instance& in)
{
    memcpy(out.transform.matrix, in.transform, sizeof(out.transform.matrix));
    const Blas& b = s_blas[in.id];
    out.instanceCustomIndex = b.alpha ? in.id : 0;      // the caster table's row
    out.mask = in.keep ? 0x01 : 0x02;     // the world; lit things (cars), which lit draws' rays skip
    out.instanceShaderBindingTableRecordOffset = 0;
    out.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR |
                (b.alpha ? 0 : VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR);
    out.accelerationStructureReference = s_blas[in.id].address;
}

void pvkSunEndFrame()
{
    if(!pvkSunReady())
        return;
    // prim buffers gone: their instances out, their structures retired, their ids free again
    pthread_mutex_lock(&s_deadLock);
    unsigned deadCount = s_deadCount;
    unsigned* dead = NULL;
    if(deadCount)
    {
        dead = (unsigned*)malloc(deadCount * sizeof(unsigned));
        memcpy(dead, s_dead, deadCount * sizeof(unsigned));
        s_deadCount = 0;
    }
    pthread_mutex_unlock(&s_deadLock);
    for(unsigned i = 0; i < deadCount; i++)
    {
        if(dead[i] > s_blasCount)
            continue;
        ReleaseBlas(s_blas[dead[i]]);
        s_blas[dead[i]].live = false;
    }

    VkAccelerationStructureKHR scene = pvkSunScene();
    unsigned now = SDL_GetTicks();
    bool on = pvkSunOn();
    unsigned kept = 0, used = 0, current = 0;
    VkAccelerationStructureInstanceKHR* out = scene ?
        (VkAccelerationStructureInstanceKHR*)s_scenes[g_pvk->FrameSerial() % pvkFrames].instances.memory.mapped : NULL;
    for(unsigned i = 0; i < s_instanceCount; i++)
    {
        Instance& in = s_instances[i];
        Blas& b = s_blas[in.id];
        bool thisFrame = in.lastFrame == s_frame;
        bool keep = on && b.live && b.as && b.address &&
                    (thisFrame || (in.keep && in.streak >= 3 && now - in.lastMs < kKeepMs));
        if(!keep)
            continue;
        s_instances[kept++] = in;
        if(out && used < (unsigned)kMaxInstances)
        {
            FillInstance(out[used++], in);
            current += thisFrame;
        }
    }
    s_instanceCount = kept;
    RebuildHash();
    for(unsigned i = 0; i < deadCount; i++)
    {
        if(dead[i] > s_blasCount)
            continue;
        memset(&s_blas[dead[i]], 0, sizeof(Blas));
        if(s_freeCount == s_freeCapacity)
        {
            s_freeCapacity = s_freeCapacity * 2 + 1024;
            s_freeIds = (unsigned*)realloc(s_freeIds, s_freeCapacity * sizeof(unsigned));
        }
        s_freeIds[s_freeCount++] = dead[i];
    }
    free(dead);

    // the sun: the frame's strongest directional light
    if(!s_fixedDirection && s_bestLight > 0.0f)
    {
        Elevate(s_bestDirection);
        float d = fabsf(s_bestDirection[0] - s_direction[0]) + fabsf(s_bestDirection[1] - s_direction[1]) +
                  fabsf(s_bestDirection[2] - s_direction[2]);
        if(d > 1e-4f)
        {
            memcpy(s_direction, s_bestDirection, sizeof(s_direction));
            s_version++;
        }
    }
    s_bestLight = 0.0f;

    if(scene)
    {
        Scene& sc = s_scenes[g_pvk->FrameSerial() % pvkFrames];
        VkCommandBuffer cmd = g_pvk->UploadCmd();
        // the bottom levels built this frame, before the top level reads them
        VkMemoryBarrier2 b = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        b.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        b.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        b.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        b.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
        VkAccelerationStructureGeometryKHR geometry;
        VkAccelerationStructureBuildGeometryInfoKHR build;
        TopLevelGeometry(geometry, build, sc.instanceAddress);
        build.dstAccelerationStructure = sc.as;
        build.scratchData.deviceAddress = sc.scratchAddress;
        VkAccelerationStructureBuildRangeInfoKHR range = { used, 0, 0, 0 };
        const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &ranges);
        // (pvkCore::Present's barrier after the uploads makes it visible to the draws)
    }

    if(s_log)
    {
        static unsigned s_lastLog = 0;
        static unsigned s_builds = 0;
        s_builds += s_buildsThisFrame;
        if(now - s_lastLog > 5000)
        {
            fprintf(stderr, "PDDI sun: %s, direction %.2f %.2f %.2f, %u instances (%u this frame), %u structures, %u built\n",
                    on ? "on" : "off", s_direction[0], s_direction[1], s_direction[2], used, current,
                    s_blasCount - s_freeCount, s_builds);
            s_lastLog = now;
            s_builds = 0;
        }
    }
    s_buildsThisFrame = 0;
    s_frame++;
}

void pvkSunShutdown()
{
    if(!g_pvk || !g_pvk->device)
        return;
    for(unsigned i = 1; i <= s_blasCount; i++)
    {
        if(s_blas[i].as)
            vkDestroyAccelerationStructureKHR(g_pvk->device, s_blas[i].as, NULL);
        g_pvk->DestroyNow(s_blas[i].storage);
    }
    g_pvk->DestroyNow(s_casterTable);
    g_pvk->DestroyNow(s_masks);
    g_pvk->DestroyNow(s_geometry);
    free(s_geometryFree);
    s_geometryFree = NULL;
    s_geometryFreeCount = s_geometryFreeCapacity = 0;
    memset(s_maskOwner, 0, sizeof(s_maskOwner));
    free(s_blas);
    s_blas = NULL;
    s_blasCount = s_blasCapacity = 0;
    s_freeCount = 0;
    for(int f = 0; f < pvkFrames; f++)
    {
        Scene& sc = s_scenes[f];
        if(sc.as)
            vkDestroyAccelerationStructureKHR(g_pvk->device, sc.as, NULL);
        g_pvk->DestroyNow(sc.storage);
        g_pvk->DestroyNow(sc.scratch);
        g_pvk->DestroyNow(sc.instances);
        memset(&sc, 0, sizeof(sc));
    }
    s_scenesMade = false;
    s_instanceCount = 0;
    s_hashSize = 0;
    free(s_hash);
    s_hash = NULL;
}
