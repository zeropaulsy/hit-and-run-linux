//=============================================================================
// Linux port: sun lighting with ray-traced shadows (see vksun.hpp)
//=============================================================================

#include <pddi/vk/vksun.hpp>
#include <pddi/vk/vktex.hpp>
#include <shar_glslang.h>

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
static const int kMapSize = 2048;               // texels across a level of the baked map (a power of two)
static const int kMapLevels = 4;
static const unsigned kWorldKeepMs = 600000;    // a baked caster not drawn for this long leaves the world's scene
static const unsigned kMissingFrames = 30;      // ... or when it is in view, near, and not drawn for this many frames
static const float kMissingRange = 60.0f;
static const unsigned kWorldStreak = 30;        // frames running with the same transform before it is baked

static int s_wanted = -1;
static bool s_enabled = false;     // Options > Graphics Shadows (simpsons.ini shadows=), F9, SHAR_SUN=1
static float s_eyeToWorld[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
static unsigned s_version = 1;
static float s_direction[3] = { 0.36f, 0.80f, 0.48f };
static bool s_fixedDirection = false;
static float s_levels[3] = { 0.68f, 1.22f, 0.7f };
static float s_softDegrees = 0.3f;      // the sun disc's radius: about the real sun's (0.27)
static float s_rays = 8.0f;
static float s_range[3] = { 80.0f, 320.0f, 150.0f };   // full rays to (one at twice it), no shadow past, ray length
static int s_bake = 1;                  // SHAR_SUN_BAKE=0: every shadow ray traced per pixel
static float s_mapTexel = 0.04f;        // SHAR_SUN_TEXEL: the baked map's level 0 texel (m); each level doubles it
static unsigned s_bakeBudget = 4000000;    // SHAR_SUN_BAKE_BUDGET: texels baked a frame
static const float* s_projectionSource;
static float s_viewProjection[16];
static bool s_viewProjectionSet = false;
static unsigned s_notInView;
static bool s_log = false;
static int s_logLevel = 0;
static int s_debug = 0;            // SHAR_SUN_DEBUG=1: shadowed pixels show the caster's id (red low, green high byte)
static const char* s_drawingMaterial = "-";
static const char* s_drawingFile = "-";

// the sun's height per level, degrees (0: as it is). The sky's sun (lens flare) is low (L1 14 degrees): long shadows.
static const float kLevelElevation[] = { 35.0f };
static int s_level = -1;
static float s_elevation = -1.0f;      // SHAR_SUN_ELEVATION: every level

// the sky's painted sun (the level's lens flare, LensFlareDSG): the compass direction when there is one, else the
// game's key light (L1: ~85 degrees round from the sky's sun, behind the Simpsons' house)
static bool s_sky = false;
static float s_skyDirection[3];

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
        s_wanted = !e || e[0] != '0';      // ray queries on the device unless SHAR_SUN=0 (the menu can turn it on)
        if(e && e[0] == '1')
            s_enabled = true;
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
        s_softDegrees = EnvFloat("SHAR_SUN_SOFT", s_softDegrees);
        s_rays = EnvFloat("SHAR_SUN_RAYS", s_rays);
        s_rays = s_rays < 1.0f ? 1.0f : s_rays > 32.0f ? 32.0f : floorf(s_rays);
        s_range[0] = EnvFloat("SHAR_SUN_NEAR", s_range[0]);
        s_range[1] = EnvFloat("SHAR_SUN_FAR", s_range[1]);
        s_range[2] = EnvFloat("SHAR_SUN_REACH", s_range[2]);
        s_range[2] = s_range[2] < 1.0f ? 1.0f : s_range[2];
        const char* bake = getenv("SHAR_SUN_BAKE");
        s_bake = bake && bake[0] == '0' ? 0 : 1;
        s_mapTexel = EnvFloat("SHAR_SUN_TEXEL", s_mapTexel);
        s_mapTexel = s_mapTexel < 0.01f ? 0.01f : s_mapTexel;
        float budget = EnvFloat("SHAR_SUN_BAKE_BUDGET", (float)s_bakeBudget);
        s_bakeBudget = budget < 4096.0f ? 4096u : (unsigned)budget;
        const char* l = getenv("SHAR_SUN_LOG");
        s_logLevel = l ? atoi(l) : 0;
        s_log = s_logLevel > 0;
        const char* dbg = getenv("SHAR_SUN_DEBUG");
        s_debug = dbg ? atoi(dbg) : 0;
    }
    return s_wanted > 0;
}

void pvkSunSetLevel(int level)
{
    s_level = level;
}

void pvkSunSetSkyDirection(const float* direction)
{
    s_sky = direction && direction[1] > 0.0f;
    if(!s_sky)
        return;
    memcpy(s_skyDirection, direction, sizeof(s_skyDirection));
    Normalise(s_skyDirection);
    if(pvkSunWanted() && s_log)
        fprintf(stderr, "PDDI sun: sky's sun %.3f %.3f %.3f (%.1f degrees up)\n", s_skyDirection[0], s_skyDirection[1],
                s_skyDirection[2], asinf(s_skyDirection[1]) * 180.0f / 3.14159265f);
}

// the sun raised (or lowered) to the level's height, the compass direction kept
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

void pvkSunSetEnabled(bool on)
{
    if(on != s_enabled)
        s_version++;
    s_enabled = on;
}

bool pvkSunEnabled()
{
    return s_enabled;
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
void pvkSunSetProjectionSource(const float* projection)
{
    s_projectionSource = projection;
}

bool pvkSunInView(const float* projection)
{
    bool in = !s_viewProjectionSet || memcmp(projection, s_viewProjection, sizeof(s_viewProjection)) == 0;
    s_notInView += !in;
    return in;
}

void pvkSetViewMatrix(const float* cameraToWorld)
{
    if(s_projectionSource)
    {
        memcpy(s_viewProjection, s_projectionSource, sizeof(s_viewProjection));
        s_viewProjectionSet = true;
    }
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

void pvkSunParameters(float* direction, float* levels, float* soft, float* range)
{
    memcpy(range, s_range, sizeof(s_range));
    memcpy(direction, s_direction, sizeof(s_direction));
    memcpy(levels, s_levels, sizeof(s_levels));
    soft[0] = s_softDegrees > 0.0f ? tanf(s_softDegrees * 3.14159265f / 180.0f) : 0.0f;
    soft[1] = s_softDegrees > 0.0f ? s_rays : 1.0f;
    if(s_debug)
        soft[1] = -1.0f;        // the shader's debug view: one ray, the blocker's id as the colour
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
    // per triangle its area-weighted normal; per vertex the sum of its own triangles' (the side it faces)
    float* face = (float*)malloc((triangles ? triangles : 1) * 12);
    for(unsigned t = 0; t < triangles; t++)
    {
        unsigned a = list[t * 3], b = list[t * 3 + 1], c = list[t * 3 + 2];
        float* n = &face[t * 3];
        if(a >= vertexCount || b >= vertexCount || c >= vertexCount)
        {
            n[0] = n[1] = n[2] = 0.0f;
            continue;
        }
        float e1[3], e2[3];
        for(int k = 0; k < 3; k++)
        {
            e1[k] = p[b * 3 + k] - p[a * 3 + k];
            e2[k] = p[c * 3 + k] - p[a * 3 + k];
        }
        n[0] = e1[1] * e2[2] - e1[2] * e2[1];
        n[1] = e1[2] * e2[0] - e1[0] * e2[2];
        n[2] = e1[0] * e2[1] - e1[1] * e2[0];
        for(int i = 0; i < 3; i++)
            for(int k = 0; k < 3; k++)
                normals[list[t * 3 + i] * 3 + k] += n[k];
    }
    // the triangles at each position (counting sort by group)
    unsigned* start = (unsigned*)calloc(vertexCount + 1, sizeof(unsigned));
    unsigned* at = (unsigned*)malloc((triangles ? triangles : 1) * 3 * sizeof(unsigned));
    for(unsigned i = 0; i < triangles * 3; i++)
        if(list[i] < vertexCount)
            start[group[list[i]] + 1]++;
    for(unsigned v = 0; v < vertexCount; v++)
        start[v + 1] += start[v];
    unsigned* fill = (unsigned*)malloc(vertexCount * sizeof(unsigned));
    memcpy(fill, start, vertexCount * sizeof(unsigned));
    for(unsigned i = 0; i < triangles * 3; i++)
        if(list[i] < vertexCount)
            at[fill[group[list[i]]]++] = i / 3;
    free(fill);
    free(list);
    // a vertex takes the triangles at its position within ~37 degrees of its own (either winding: the game draws
    // with culling off): a wall's normal does not bend towards the walls round a corner (a darkened band along it)
    float* own = (float*)malloc(vertexCount * 12);
    memcpy(own, normals, vertexCount * 12);
    for(unsigned v = 0; v < vertexCount; v++)
    {
        float* ref = &own[v * 3];
        Normalise(ref);
        float sum[3] = { 0.0f, 0.0f, 0.0f };
        unsigned g = group[v];
        for(unsigned i = start[g]; i < start[g + 1]; i++)
        {
            const float* n = &face[at[i] * 3];
            float length = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
            if(length <= 0.0f)
                continue;
            float d = (n[0] * ref[0] + n[1] * ref[1] + n[2] * ref[2]) / length;
            if(fabsf(d) < 0.8f)
                continue;
            float sign = d < 0.0f ? -1.0f : 1.0f;
            for(int k = 0; k < 3; k++)
                sum[k] += n[k] * sign;
        }
        float* out = &normals[v * 3];
        if(sum[0] != 0.0f || sum[1] != 0.0f || sum[2] != 0.0f)
            memcpy(out, sum, 12);
        else
            memcpy(out, ref, 12);       // its own triangles span a crease: their mean (the shader takes the face's)
        Normalise(out);
    }
    free(own);
    free(at);
    free(start);
    free(face);
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
    float size;                 // bounding box diagonal
    float lo[3], hi[3];         // the bounding box
    // attached to the camera (sky pieces, a ring round it): its offset from the camera unchanged while it moves
    float offset[3], camera[3];
    unsigned offsetFrame;
    int attachedFrames;
    bool attached;
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
static pvkBuffer s_mapBuffer;      // the baked map (s_bake), heights as floats: level, row, column
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
    if(!s_casterTable.buffer || !s_masks.buffer || !s_geometry.buffer || !s_mapBuffer.buffer)
        return false;
    const pvkBuffer* b[4] = { &s_casterTable, &s_masks, &s_geometry, &s_mapBuffer };
    for(int i = 0; i < 4; i++)
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
    b.size = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    memcpy(b.lo, lo, sizeof(lo));
    memcpy(b.hi, hi, sizeof(hi));
    b.huge = b.size > kHugeSize;

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

void pvkSunDrawing(const char* material, const char* file)
{
    s_drawingMaterial = material;
    s_drawingFile = file;
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
    // baked (s_bake): pre-lit, the same transform 3 frames running and not rebuilt often: in the world's scene, not
    // the draws' once its footprint is baked (ticket)
    bool world;
    unsigned ticket;
    int builds;                 // its structure's builds when it became world: rebuilt since, its footprint again
    unsigned missing;           // frames running in view, near and not drawn
    float centre[3], radius;    // world space bounding sphere
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
            if(s_logLevel >= 2 && b.builds == 1)
                fprintf(stderr, "PDDI sun: caster %u = %s (%s), %.1f m, %u vertices, %u indices, %s\n", *id,
                        s_drawingMaterial, s_drawingFile, b.size, vertexCount, count, keep ? "world" : "lit");
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
    // drawn at the camera, or kept at the same offset from it while it moves (a 19 m ring round it cast a ring
    // shadow that followed the player): not scenery. Only big things: a closely followed player is not caught.
    float offset[3] = { w[12] - e[12], w[13] - e[13], w[14] - e[14] };
    if(offset[0] * offset[0] + offset[1] * offset[1] + offset[2] * offset[2] < 0.25f)
        return;
    if(b.size > 8.0f && b.offsetFrame != s_frame)
    {
        float moved = fabsf(e[12] - b.camera[0]) + fabsf(e[13] - b.camera[1]) + fabsf(e[14] - b.camera[2]);
        float drift = fabsf(offset[0] - b.offset[0]) + fabsf(offset[1] - b.offset[1]) + fabsf(offset[2] - b.offset[2]);
        if(b.offsetFrame + 1 == s_frame && moved > 0.02f)
        {
            b.attachedFrames = drift < 0.002f ? b.attachedFrames + 1 : 0;
            if(b.attachedFrames >= 3 && !b.attached)
            {
                b.attached = true;
                if(s_log)
                    fprintf(stderr, "PDDI sun: caster %u (%.1f m) moves with the camera: not casting\n", *id, b.size);
            }
        }
        memcpy(b.offset, offset, sizeof(offset));
        b.camera[0] = e[12];
        b.camera[1] = e[13];
        b.camera[2] = e[14];
        b.offsetFrame = s_frame;
    }
    if(b.attached)
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
    float c[3] = { (b.lo[0] + b.hi[0]) * 0.5f, (b.lo[1] + b.hi[1]) * 0.5f, (b.lo[2] + b.hi[2]) * 0.5f };
    float scale = 0.0f;
    for(int row = 0; row < 3; row++)
    {
        in.centre[row] = in.transform[row * 4] * c[0] + in.transform[row * 4 + 1] * c[1] +
                         in.transform[row * 4 + 2] * c[2] + in.transform[row * 4 + 3];
        float column = sqrtf(in.transform[row] * in.transform[row] + in.transform[4 + row] * in.transform[4 + row] +
                             in.transform[8 + row] * in.transform[8 + row]);
        scale = column > scale ? column : scale;
    }
    in.radius = b.size * 0.5f * scale;

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
            o.missing = 0;
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

// the draws' (this frame's casters; baked: only what the map does not hold yet) and the world's (what the map is baked
// from; the draws trace it where the map cannot draw an edge)
struct Tlas
{
    VkAccelerationStructureKHR as;
    pvkBuffer storage, scratch, instances;
    VkDeviceAddress scratchAddress, instanceAddress;
};

struct Scene
{
    Tlas draws, world;
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

static bool MakeTlas(Tlas& t, const VkAccelerationStructureBuildSizesInfoKHR& sizes)
{
    if(!g_pvk->CreateBuffer(sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false, &t.storage) ||
       !g_pvk->CreateBuffer(sizes.buildScratchSize + 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                            VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false, &t.scratch) ||
       !g_pvk->CreateBuffer((VkDeviceSize)kMaxInstances * sizeof(VkAccelerationStructureInstanceKHR), kInputUsage,
                            true, &t.instances))
        return false;
    t.scratchAddress = (BufferAddress(t.scratch) + 255) & ~(VkDeviceAddress)255;
    t.instanceAddress = BufferAddress(t.instances);
    VkAccelerationStructureCreateInfoKHR create = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
    create.buffer = t.storage.buffer;
    create.size = sizes.accelerationStructureSize;
    create.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    if(vkCreateAccelerationStructureKHR(g_pvk->device, &create, NULL, &t.as) != VK_SUCCESS)
    {
        t.as = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

static void DestroyTlas(Tlas& t)
{
    if(t.as)
        vkDestroyAccelerationStructureKHR(g_pvk->device, t.as, NULL);
    g_pvk->DestroyNow(t.storage);
    g_pvk->DestroyNow(t.scratch);
    g_pvk->DestroyNow(t.instances);
    memset(&t, 0, sizeof(t));
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
    if(!g_pvk->CreateBuffer((VkDeviceSize)kMaxCasterIds * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, &s_casterTable) ||
       !g_pvk->CreateBuffer((VkDeviceSize)kMaxMasks * kMaskSize * kMaskSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true,
                            &s_masks) ||
       !g_pvk->CreateBuffer((VkDeviceSize)kGeometryWords * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, &s_geometry) ||
       !g_pvk->CreateBuffer(s_bake ? (VkDeviceSize)kMapLevels * kMapSize * kMapSize * 4 : 16,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, false, &s_mapBuffer))
        return false;
    s_geometryFreeCount = 0;
    GeometryFree(0, kGeometryWords);
    for(int f = 0; f < pvkFrames; f++)
        if(!MakeTlas(s_scenes[f].draws, sizes) || (s_bake && !MakeTlas(s_scenes[f].world, sizes)))
            return false;
    return true;
}

// the draws' scene
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
    return s_scenes[g_pvk->FrameSerial() % pvkFrames].draws.as;
}

VkAccelerationStructureKHR pvkSunWorldScene()
{
    VkAccelerationStructureKHR draws = pvkSunScene();
    const Tlas& world = s_scenes[g_pvk->FrameSerial() % pvkFrames].world;
    return draws && world.as ? world.as : draws;
}

static void FillInstance(VkAccelerationStructureInstanceKHR& out, const Instance& in)
{
    memcpy(out.transform.matrix, in.transform, sizeof(out.transform.matrix));
    const Blas& b = s_blas[in.id];
    out.instanceCustomIndex = in.id;    // the caster table's row (alpha-tested), SHAR_SUN_DEBUG's colour
    out.mask = in.keep ? 0x01 : 0x02;     // the world; lit things (cars), which lit draws' rays skip
    out.instanceShaderBindingTableRecordOffset = 0;
    out.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR |
                (b.alpha ? 0 : VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR);
    out.accelerationStructureReference = s_blas[in.id].address;
}

//-----------------------------------------------------------------------------
// The baked map (s_bake): the world's shadow as the sun sees it
//-----------------------------------------------------------------------------
// kMapLevels levels of kMapSize x kMapSize texels on the plane facing the sun (u along s_basis[0], v along
// s_basis[1], the shaders' disc axes), each level's texel twice the last's, centred on the camera and moved in steps.
// Toroidal: texel (x, y) is stored at (x & (kMapSize - 1), y & (kMapSize - 1)), so a move bakes only the strips that
// came in. A texel holds the height towards the sun (dot(p, sun)) of the first surface its ray from the sun meets in
// the world's scene (static pre-lit instances, -1e30: none); draws compare their own height with it (glshaders.cpp).
// Baked in the upload command buffer, a budget of texels a frame: first the strips (until a level's are done its
// valid window is what was baked before), then the tiles (kMapTile texels square) under the footprints of casters
// that came, went or changed. Each mark has a ticket; a caster drawn this frame stays in the draws' scene until no
// tile marked by then is left.

struct MapRect
{
    int x0, y0, x1, y1;         // texel indices, x1 and y1 past the end
};

static const int kMapTile = 64;
static const int kMapTiles = kMapSize / kMapTile;   // across a level

struct MapLevel
{
    MapRect window, valid;
    MapRect strips[4];
    int stripCount;
    bool placed;
    // tiles to bake again, toroidal as the texels: the tile (x, y) a slot was marked for
    int tile[kMapTiles * kMapTiles][2];
    bool dirty[kMapTiles * kMapTiles];
};

static MapLevel s_map[kMapLevels];
static float s_basis[2][3];
static unsigned s_dirtyTiles;
static unsigned s_ticket, s_bakedTicket;
static VkDescriptorSetLayout s_bakeSetLayout;
static VkPipelineLayout s_bakeLayout;
static VkPipeline s_bakePipeline;
static bool s_bakeFailed;
static unsigned s_bakedTexels;

static const char* const kBakeSource =
    "#version 460\n"
    "#extension GL_EXT_ray_query : require\n"
    "// the sun's baked map (vksun.cpp): a ray from above along each texel, the height of the first surface it meets\n"
    "layout(local_size_x = 8, local_size_y = 8) in;\n"
    "layout(binding = 0) uniform accelerationStructureEXT world;\n"
    "layout(std430, binding = 1) readonly buffer SunCasters { uvec4 sunCaster[]; };\n"
    "layout(std430, binding = 2) readonly buffer SunMasks { uint sunMask[]; };\n"
    "layout(std430, binding = 3) readonly buffer SunGeometry { uint sunGeometry[]; };\n"
    "layout(std430, binding = 4) writeonly buffer SunMap { float sunMap[]; };\n"
    "layout(push_constant) uniform Bake\n"
    "{\n"
    "    ivec4 rect;         // first texel, texels across and down\n"
    "    ivec2 level;        // level, texels across a level\n"
    "    vec2 texel;         // texel (m), the rays' start height\n"
    "    vec4 u, v;          // the plane's axes\n"
    "    vec4 sun;           // towards the sun; w: the rays' length\n"
    "} bake;\n"
    "vec2 SunUV(uint base, uint vertex)\n"
    "{\n"
    "    return vec2(uintBitsToFloat(sunGeometry[base + vertex * 2u]), uintBitsToFloat(sunGeometry[base + vertex * 2u + 1u]));\n"
    "}\n"
    "// as glshaders.cpp's: a candidate hit on an alpha-tested caster counts where its alpha test passes\n"
    "bool SunOpaque(int caster, int primitive, vec2 b)\n"
    "{\n"
    "    uvec4 c = sunCaster[caster];\n"
    "    uint t = c.y + uint(primitive) * 3u;\n"
    "    vec2 uv = SunUV(c.x, sunGeometry[t]) * (1.0 - b.x - b.y) + SunUV(c.x, sunGeometry[t + 1u]) * b.x +\n"
    "              SunUV(c.x, sunGeometry[t + 2u]) * b.y;\n"
    "    uvec2 texel = uvec2(ivec2(floor(fract(uv) * 64.0)) & 63);\n"
    "    uint i = (c.z & 0xffffu) * 4096u + texel.y * 64u + texel.x;\n"
    "    float alpha = float((sunMask[i >> 2] >> ((i & 3u) * 8u)) & 255u) / 255.0;\n"
    "    float ref = uintBitsToFloat(c.w);\n"
    "    return (c.z >> 16) == 4u ? alpha > ref : alpha >= ref;\n"
    "}\n"
    "void main()\n"
    "{\n"
    "    uvec2 g = gl_GlobalInvocationID.xy;\n"
    "    if(g.x >= uint(bake.rect.z) || g.y >= uint(bake.rect.w))\n"
    "        return;\n"
    "    ivec2 i = bake.rect.xy + ivec2(g);\n"
    "    vec2 p = (vec2(i) + 0.5) * bake.texel.x;\n"
    "    vec3 origin = bake.u.xyz * p.x + bake.v.xyz * p.y + bake.sun.xyz * bake.texel.y;\n"
    "    rayQueryEXT q;\n"
    "    rayQueryInitializeEXT(q, world, gl_RayFlagsNoneEXT, 0xff, origin, 0.0, -bake.sun.xyz, bake.sun.w);\n"
    "    while(rayQueryProceedEXT(q))\n"
    "    {\n"
    "        if(SunOpaque(rayQueryGetIntersectionInstanceCustomIndexEXT(q, false),\n"
    "                     rayQueryGetIntersectionPrimitiveIndexEXT(q, false),\n"
    "                     rayQueryGetIntersectionBarycentricsEXT(q, false)))\n"
    "            rayQueryConfirmIntersectionEXT(q);\n"
    "    }\n"
    "    float h = rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT ? -1e30 :\n"
    "              bake.texel.y - rayQueryGetIntersectionTEXT(q, true);\n"
    "    int n = bake.level.y;\n"
    "    sunMap[(bake.level.x * n + (i.y & (n - 1))) * n + (i.x & (n - 1))] = h;\n"
    "}\n";

struct BakePush
{
    int rect[4];
    int level[2];
    float texel[2];
    float u[4], v[4], sun[4];
};

static bool MakeBake()
{
    unsigned* words = NULL;
    size_t count = 0;
    char* log = NULL;
    bool ok = shar_spirv_compile("sun bake", 2, kBakeSource, &words, &count, &log) != 0;
    if(log)
        fprintf(stderr, "PDDI: %s\n", log);
    VkShaderModule module = ok ? pvkCreateShaderModule(words, count) : VK_NULL_HANDLE;
    shar_spirv_free(words);
    shar_spirv_free(log);
    if(!module)
        return false;
    VkDescriptorSetLayoutBinding bindings[5];
    memset(bindings, 0, sizeof(bindings));
    for(int i = 0; i < 5; i++)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo set = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    set.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    set.bindingCount = 5;
    set.pBindings = bindings;
    VkPushConstantRange range = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(BakePush) };
    VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &s_bakeSetLayout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    VkComputePipelineCreateInfo info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = module;
    info.stage.pName = "main";
    ok = vkCreateDescriptorSetLayout(g_pvk->device, &set, NULL, &s_bakeSetLayout) == VK_SUCCESS &&
         vkCreatePipelineLayout(g_pvk->device, &pl, NULL, &s_bakeLayout) == VK_SUCCESS;
    info.layout = s_bakeLayout;
    ok = ok && vkCreateComputePipelines(g_pvk->device, VK_NULL_HANDLE, 1, &info, NULL, &s_bakePipeline) == VK_SUCCESS;
    vkDestroyShaderModule(g_pvk->device, module, NULL);
    return ok;
}

static bool BakeReady()
{
    if(!s_bakePipeline && !s_bakeFailed)
    {
        s_bakeFailed = !MakeBake();
        if(s_bakeFailed)
            fprintf(stderr, "PDDI: cannot make the sun's bake: every shadow ray traced per pixel\n");
    }
    return s_bakePipeline != VK_NULL_HANDLE;
}

static bool Baking()
{
    return s_bake && !s_bakeFailed;
}

static bool RectEmpty(const MapRect& r)
{
    return r.x0 >= r.x1 || r.y0 >= r.y1;
}

static MapRect Intersect(const MapRect& a, const MapRect& b)
{
    MapRect r = { a.x0 > b.x0 ? a.x0 : b.x0, a.y0 > b.y0 ? a.y0 : b.y0, a.x1 < b.x1 ? a.x1 : b.x1, a.y1 < b.y1 ? a.y1 : b.y1 };
    if(RectEmpty(r))
        memset(&r, 0, sizeof(r));
    return r;
}

static float MapTexel(int level)
{
    return s_mapTexel * (float)(1 << level);
}

// the sun moved (or a level started): everything again
static void MapReset()
{
    const float* d = s_direction;
    float up[3] = { 0.0f, 1.0f, 0.0f };
    if(fabsf(d[1]) >= 0.99f)
    {
        up[0] = 1.0f;
        up[1] = 0.0f;
    }
    float* t = s_basis[0];
    float* b = s_basis[1];
    t[0] = up[1] * d[2] - up[2] * d[1];
    t[1] = up[2] * d[0] - up[0] * d[2];
    t[2] = up[0] * d[1] - up[1] * d[0];
    Normalise(t);
    b[0] = d[1] * t[2] - d[2] * t[1];
    b[1] = d[2] * t[0] - d[0] * t[2];
    b[2] = d[0] * t[1] - d[1] * t[0];
    memset(s_map, 0, sizeof(s_map));
    s_dirtyTiles = 0;
    s_bakedTicket = s_ticket;
    s_version++;
}

// a caster's footprint to bake again (it came, went or changed): the tiles under it that are baked; its ticket
static unsigned MarkDirty(const Instance& in)
{
    float u = in.centre[0] * s_basis[0][0] + in.centre[1] * s_basis[0][1] + in.centre[2] * s_basis[0][2];
    float v = in.centre[0] * s_basis[1][0] + in.centre[1] * s_basis[1][1] + in.centre[2] * s_basis[1][2];
    for(int l = 0; l < kMapLevels; l++)
    {
        MapLevel& m = s_map[l];
        float texel = MapTexel(l);
        MapRect f = { (int)floorf((u - in.radius) / texel) - 1, (int)floorf((v - in.radius) / texel) - 1,
                      (int)ceilf((u + in.radius) / texel) + 1, (int)ceilf((v + in.radius) / texel) + 1 };
        f = Intersect(f, m.valid);      // (the strips not baked yet will see it)
        if(RectEmpty(f))
            continue;
        for(int ty = f.y0 >> 6; ty <= (f.y1 - 1) >> 6; ty++)
            for(int tx = f.x0 >> 6; tx <= (f.x1 - 1) >> 6; tx++)
            {
                int slot = (ty & (kMapTiles - 1)) * kMapTiles + (tx & (kMapTiles - 1));
                if(m.dirty[slot] && m.tile[slot][0] == tx && m.tile[slot][1] == ty)
                    continue;
                s_dirtyTiles += !m.dirty[slot];
                m.dirty[slot] = true;
                m.tile[slot][0] = tx;
                m.tile[slot][1] = ty;
            }
    }
    return ++s_ticket;
}

// each level's window round the camera, moved when it is an eighth of the way off; what came in is to bake
static void MapPlace()
{
    const float* e = s_eyeToWorld;
    float u = e[12] * s_basis[0][0] + e[13] * s_basis[0][1] + e[14] * s_basis[0][2];
    float v = e[12] * s_basis[1][0] + e[13] * s_basis[1][1] + e[14] * s_basis[1][2];
    for(int l = 0; l < kMapLevels; l++)
    {
        MapLevel& m = s_map[l];
        int x = (int)floorf(u / MapTexel(l)) - kMapSize / 2;
        int y = (int)floorf(v / MapTexel(l)) - kMapSize / 2;
        if(m.placed && abs(x - m.window.x0) <= kMapSize / 8 && abs(y - m.window.y0) <= kMapSize / 8)
            continue;
        MapRect w = { x, y, x + kMapSize, y + kMapSize };
        if(m.placed)
            m.valid = Intersect(m.valid, w);
        m.window = w;
        m.placed = true;
        m.stripCount = 0;
        const MapRect& o = m.valid;
        if(RectEmpty(o))
            m.strips[m.stripCount++] = w;
        else
        {
            MapRect below = { w.x0, w.y0, w.x1, o.y0 }, above = { w.x0, o.y1, w.x1, w.y1 };
            MapRect left = { w.x0, o.y0, o.x0, o.y1 }, right = { o.x1, o.y0, w.x1, o.y1 };
            const MapRect* parts[4] = { &below, &above, &left, &right };
            for(int i = 0; i < 4; i++)
                if(!RectEmpty(*parts[i]))
                    m.strips[m.stripCount++] = *parts[i];
        }
        s_version++;
    }
}

static bool BakeWork()
{
    for(int l = 0; l < kMapLevels; l++)
        if(s_map[l].stripCount)
            return true;
    return s_dirtyTiles != 0;
}

// rows from the top of r, up to the budget's texels (at least one row); they come off r. Texels baked.
static unsigned BakeRows(VkCommandBuffer cmd, int level, MapRect& r, unsigned budget, float top)
{
    int across = r.x1 - r.x0;
    int rows = (int)(budget / (unsigned)across);
    rows = rows < 1 ? 1 : rows > r.y1 - r.y0 ? r.y1 - r.y0 : rows;
    BakePush pc;
    pc.rect[0] = r.x0;
    pc.rect[1] = r.y0;
    pc.rect[2] = across;
    pc.rect[3] = rows;
    pc.level[0] = level;
    pc.level[1] = kMapSize;
    pc.texel[0] = MapTexel(level);
    pc.texel[1] = top;
    for(int k = 0; k < 3; k++)
    {
        pc.u[k] = s_basis[0][k];
        pc.v[k] = s_basis[1][k];
        pc.sun[k] = s_direction[k];
    }
    pc.u[3] = pc.v[3] = 0.0f;
    pc.sun[3] = 4000.0f;
    vkCmdPushConstants(cmd, s_bakeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdDispatch(cmd, (unsigned)(across + 7) / 8, (unsigned)(rows + 7) / 8, 1);
    r.y0 += rows;
    s_bakedTexels += (unsigned)(across * rows);
    return (unsigned)(across * rows);
}

static void Bake(VkCommandBuffer cmd, VkAccelerationStructureKHR world)
{
    VkDescriptorBufferInfo buffers[4];
    if(!pvkSunBuffers(buffers))
        return;
    VkWriteDescriptorSetAccelerationStructureKHR sceneInfo = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR };
    sceneInfo.accelerationStructureCount = 1;
    sceneInfo.pAccelerationStructures = &world;
    VkWriteDescriptorSet writes[5];
    memset(writes, 0, sizeof(writes));
    for(int i = 0; i < 5; i++)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        if(i == 0)
            writes[i].pNext = &sceneInfo;
        else
            writes[i].pBufferInfo = &buffers[i - 1];
    }
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_bakePipeline);
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_bakeLayout, 0, 5, writes);

    // the rays start well above the camera
    const float* e = s_eyeToWorld;
    float top = e[12] * s_direction[0] + e[13] * s_direction[1] + e[14] * s_direction[2] + 2000.0f;
    unsigned budget = s_bakeBudget;
    for(int l = 0; l < kMapLevels; l++)
    {
        MapLevel& m = s_map[l];
        while(budget && m.stripCount)
        {
            MapRect& r = m.strips[m.stripCount - 1];
            unsigned done = BakeRows(cmd, l, r, budget, top);
            budget -= done < budget ? done : budget;
            if(RectEmpty(r))
                m.stripCount--;
        }
        if(m.placed && m.stripCount == 0 && memcmp(&m.valid, &m.window, sizeof(MapRect)) != 0)
        {
            m.valid = m.window;
            s_version++;
        }
    }
    // dirty tiles in the window, runs of them along a row baked together
    for(int l = 0; l < kMapLevels && budget && s_dirtyTiles; l++)
    {
        MapLevel& m = s_map[l];
        for(int slot = 0; slot < kMapTiles * kMapTiles; slot++)
        {
            if(!m.dirty[slot])
                continue;
            int tx = m.tile[slot][0], ty = m.tile[slot][1];
            MapRect t = { tx << 6, ty << 6, (tx + 1) << 6, (ty + 1) << 6 };
            if(!RectEmpty(Intersect(t, m.window)))
            {
                if(!budget)
                    continue;
                int next = tx + 1;
                for(;;)
                {
                    int s2 = (ty & (kMapTiles - 1)) * kMapTiles + (next & (kMapTiles - 1));
                    if(next - tx >= kMapTiles || !m.dirty[s2] || m.tile[s2][0] != next || m.tile[s2][1] != ty)
                        break;
                    m.dirty[s2] = false;
                    s_dirtyTiles--;
                    next++;
                }
                t.x1 = next << 6;
                t = Intersect(t, m.window);
                while(!RectEmpty(t))
                {
                    unsigned done = BakeRows(cmd, l, t, kMapTile * (t.x1 - t.x0), top);
                    budget -= done < budget ? done : budget;
                }
            }
            m.dirty[slot] = false;
            s_dirtyTiles--;
        }
    }
    if(!s_dirtyTiles)
        s_bakedTicket = s_ticket;
}

// a baked caster that should be drawn (near, in view): not drawn for kMissingFrames, it has gone (knocked down)
static bool InView(const Instance& in)
{
    if(!s_viewProjectionSet)
        return false;
    const float* e = s_eyeToWorld;
    float d[3] = { in.centre[0] - e[12], in.centre[1] - e[13], in.centre[2] - e[14] };
    float x = d[0] * e[0] + d[1] * e[1] + d[2] * e[2];
    float y = d[0] * e[4] + d[1] * e[5] + d[2] * e[6];
    float depth = -(d[0] * e[8] + d[1] * e[9] + d[2] * e[10]);     // GL eye space looks down -z
    if(depth < in.radius + 1.0f || depth > kMissingRange)
        return false;
    float px = s_viewProjection[0], py = s_viewProjection[5];
    return (fabsf(x) + in.radius) * px < depth && (fabsf(y) + in.radius) * py < depth;
}

void pvkSunMap(float* windows, float* info)
{
    bool on = Baking() && s_bakePipeline;
    for(int l = 0; l < kMapLevels; l++)
    {
        const MapRect& r = s_map[l].valid;
        windows[l * 4] = on ? (float)r.x0 : 0.0f;
        windows[l * 4 + 1] = on ? (float)r.y0 : 0.0f;
        windows[l * 4 + 2] = on ? (float)r.x1 : 0.0f;
        windows[l * 4 + 3] = on ? (float)r.y1 : 0.0f;
    }
    info[0] = s_mapTexel;
    info[1] = (float)kMapSize;
    info[2] = on ? 1.0f : 0.0f;
    info[3] = 0.0f;
}

static void BuildTlas(VkCommandBuffer cmd, Tlas& t, unsigned count)
{
    VkAccelerationStructureGeometryKHR geometry;
    VkAccelerationStructureBuildGeometryInfoKHR build;
    TopLevelGeometry(geometry, build, t.instanceAddress);
    build.dstAccelerationStructure = t.as;
    build.scratchData.deviceAddress = t.scratchAddress;
    VkAccelerationStructureBuildRangeInfoKHR range = { count, 0, 0, 0 };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build, &ranges);
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
    bool baking = scene && on && Baking() && BakeReady();
    if(baking && !s_map[0].placed)
        MapReset();
    bool bake = baking && BakeWork();
    unsigned kept = 0, used = 0, worldUsed = 0, current = 0;
    Scene* sc = scene ? &s_scenes[g_pvk->FrameSerial() % pvkFrames] : NULL;
    VkAccelerationStructureInstanceKHR* out = sc ? (VkAccelerationStructureInstanceKHR*)sc->draws.instances.memory.mapped : NULL;
    VkAccelerationStructureInstanceKHR* worldOut = baking ?
        (VkAccelerationStructureInstanceKHR*)sc->world.instances.memory.mapped : NULL;
    for(unsigned i = 0; i < s_instanceCount; i++)
    {
        Instance& in = s_instances[i];
        Blas& b = s_blas[in.id];
        bool thisFrame = in.lastFrame == s_frame;
        bool alive = on && b.live && b.as && b.address;
        if(!baking)
        {
            in.world = false;
            if(!alive || !(thisFrame || (in.keep && in.streak >= 3 && now - in.lastMs < kKeepMs)))
                continue;
            s_instances[kept++] = in;
            if(out && used < (unsigned)kMaxInstances)
            {
                FillInstance(out[used++], in);
                current += thisFrame;
            }
            continue;
        }
        if(!alive)
            continue;
        // the static world goes in the map (not things rebuilt often: animated; not things only paused a moment)
        if(!in.world && in.keep && thisFrame && in.streak >= kWorldStreak && b.builds <= 3)
        {
            in.world = true;
            in.builds = b.builds;
            in.ticket = MarkDirty(in);
        }
        else if(in.world && b.builds != in.builds)
        {
            in.world = b.builds <= 3;
            in.builds = b.builds;
            in.ticket = MarkDirty(in);
        }
        if(in.world && !thisFrame)
            in.missing = InView(in) ? in.missing + 1 : 0;
        if(!thisFrame && !(in.world && now - in.lastMs < kWorldKeepMs && in.missing < kMissingFrames))
        {
            if(in.world && in.missing >= kMissingFrames)
                MarkDirty(in);      // gone from where it stood: its shadow too
            continue;
        }
        s_instances[kept++] = in;
        if(thisFrame && (!in.world || in.ticket > s_bakedTicket) && used < (unsigned)kMaxInstances)
        {
            FillInstance(out[used++], in);
            current++;
        }
        if(in.world && worldOut && worldUsed < (unsigned)kMaxInstances)
            FillInstance(worldOut[worldUsed++], in);
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

    // the sun: the sky's, else the frame's strongest directional light
    if(!s_fixedDirection && (s_sky || s_bestLight > 0.0f))
    {
        if(s_sky)
            memcpy(s_bestDirection, s_skyDirection, sizeof(s_bestDirection));
        Elevate(s_bestDirection);
        float d = fabsf(s_bestDirection[0] - s_direction[0]) + fabsf(s_bestDirection[1] - s_direction[1]) +
                  fabsf(s_bestDirection[2] - s_direction[2]);
        if(d > 1e-4f)
        {
            memcpy(s_direction, s_bestDirection, sizeof(s_direction));
            s_version++;
            if(baking)
            {
                MapReset();
                bake = false;   // the world's instances were filled for the old map: next frame
            }
        }
    }
    s_bestLight = 0.0f;

    if(sc)
    {
        VkCommandBuffer cmd = g_pvk->UploadCmd();
        // the bottom levels built this frame, before the top levels read them
        VkMemoryBarrier2 b = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        b.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        b.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        b.dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
        b.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
        BuildTlas(cmd, sc->draws, used);
        // the world's every frame (the draws trace it where the map cannot draw an edge; empty when not baking)
        if(sc->world.as)
            BuildTlas(cmd, sc->world, worldUsed);
        if(bake)
        {
            // the world's scene built; the map's texels free of the draws of frames before (still running)
            b.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
            b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            vkCmdPipelineBarrier2(cmd, &dep);
            Bake(cmd, sc->world.as);
        }
        // (pvkCore::Present's barrier after the uploads makes them visible to the draws)
    }
    // where the next frames' map is (after this frame's bake: its draws were recorded with the windows before)
    if(baking)
        MapPlace();

    if(s_log)
    {
        static unsigned s_lastLog = 0;
        static unsigned s_builds = 0;
        s_builds += s_buildsThisFrame;
        if(now - s_lastLog > 5000)
        {
            fprintf(stderr, "PDDI sun: %s, direction %.2f %.2f %.2f, %u instances drawn (%u this frame), %u baked, "
                    "%u structures, %u built, %u draws not in the view, %u texels baked\n", on ? "on" : "off",
                    s_direction[0], s_direction[1], s_direction[2], used, current, worldUsed,
                    s_blasCount - s_freeCount, s_builds, s_notInView, s_bakedTexels);
            s_notInView = 0;
            s_lastLog = now;
            s_builds = 0;
            s_bakedTexels = 0;
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
        DestroyTlas(s_scenes[f].draws);
        DestroyTlas(s_scenes[f].world);
    }
    s_scenesMade = false;
    g_pvk->DestroyNow(s_mapBuffer);
    if(s_bakePipeline)
        vkDestroyPipeline(g_pvk->device, s_bakePipeline, NULL);
    if(s_bakeLayout)
        vkDestroyPipelineLayout(g_pvk->device, s_bakeLayout, NULL);
    if(s_bakeSetLayout)
        vkDestroyDescriptorSetLayout(g_pvk->device, s_bakeSetLayout, NULL);
    s_bakePipeline = VK_NULL_HANDLE;
    s_bakeLayout = VK_NULL_HANDLE;
    s_bakeSetLayout = VK_NULL_HANDLE;
    s_bakeFailed = false;
    memset(s_map, 0, sizeof(s_map));
    s_dirtyTiles = 0;
    s_instanceCount = 0;
    s_hashSize = 0;
    free(s_hash);
    s_hash = NULL;
}
