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
static const unsigned kLowWorldStreak = 8;      // low: nothing else casts meanwhile
// the sky grid (high): how open to the sky each cell round the camera is, after the map in s_mapBuffer as (open,
// key) floats; glshaders.cpp's SunSkyOpen has the same sizes
static const int kSkyCells[3] = { 128, 32, 128 };  // across x, y, z (powers of two)
static const unsigned kSkyCount = 128 * 32 * 128;
static const VkDeviceSize kMapBytes = (VkDeviceSize)kMapLevels * kMapSize * kMapSize * 4;

static int s_wanted = -1;
static bool s_wantRays = true;     // SHAR_SUN_RT=0: the device made without ray queries (Low only, as on an iGPU)
static int s_quality = 0;          // Options > Graphics Shadows (simpsons.ini shadows=), F9, SHAR_SUN=1/low: 0 off,
                                   // 1 low (the map rasterised), 2 high (ray traced; low without ray queries)
static bool s_suppressed = false;  // the game's: no sun here (interiors, the front end; pvkSunSetSuppressed)
static int s_casterMode = 0;       // what the casters were built for (1 meshes, 2 acceleration structures)
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
// SHAR_SUN_SKY=lo,hi (0 off): no shadow where a surface sees lo of the sky or less (under a roof: its colours were lit
// without the sun), full from hi; x: the grid's cell (SHAR_SUN_SKY_CELL, m), w: SHAR_SUN_SKY_DEBUG
static float s_skyGrid[4] = { 2.0f, 0.15f, 0.5f, 0.0f };
static int s_skyRays = 24;              // SHAR_SUN_SKY_RAYS: per cell
static float s_skyReach = 60.0f;        // SHAR_SUN_SKY_REACH: their length (m)
static unsigned s_skyBudget = 4096;     // SHAR_SUN_SKY_BUDGET: cells baked again a frame (world streamed in since), on
                                        // top of those new to the window
static const float* s_projectionSource;
static float s_viewProjection[16];
static bool s_viewProjectionSet = false;
// the world's camera: the one the last caster was drawn with (the frame's last camera can be another view's, the HUD
// radar's 150 m up): the map is placed round it, its baked casters checked against its view
static float s_worldEye[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
static float s_worldProjection[16];
static unsigned s_notInView;
static bool s_log = false;
static int s_logLevel = 0;
static int s_debug = 0;            // SHAR_SUN_DEBUG=1: shadowed pixels show the caster's id (red low, green high byte)
static const char* s_drawingMaterial = "-";
static const char* s_drawingFile = "-";

// the sun's height per level, degrees (0: as it is), the compass direction kept. Tuned per map; the levels' own
// (SHAR_SUN_ELEVATION=0, SHAR_SUN_LOG=1): L1 14.2, L2 30, L3 1.7, L4 19.4, L5 30, L6 9.9, L7 20.
static const float kLevelElevation[] = {
    35.0f,  // L1: the sky's sun (lens flare)
    50.0f,  // L2: the key light (no sun in the sky)
    10.0f,  // L3: the sky's sunset sun
    19.4f,  // L4: the sky's moon (MoonShape)
    30.0f,  // L5: the key light
    15.0f,  // L6: the sky's moon (sunset_bb_bbgShape billboards)
    20.0f,  // L7: the sky's moon (MoonShape)
};
static int s_level = -1;
static float s_elevation = -1.0f;      // SHAR_SUN_ELEVATION: every level

// the light's colour by the sun's height: warm low, near white high; the shade a little blue (sky light). Night
// levels (the moon) cool both. Luminance kept at 1 (the levels set the brightness). SHAR_SUN_TINT: strength (0 off),
// SHAR_SUN_COLOUR / SHAR_SUN_SHADE_COLOUR=r,g,b: fixed tints (normalised the same)
static const bool kLevelNight[] = { false, false, false, true, false, true, true };
static float s_tint = 1.0f;
static float s_fixedSunColour[3], s_fixedShadeColour[3];
static bool s_sunColourFixed = false, s_shadeColourFixed = false;

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
        if(e && (e[0] == '1' || e[0] == 'h'))
            s_quality = 2;
        else if(e && e[0] == 'l')
            s_quality = 1;
        const char* rt = getenv("SHAR_SUN_RT");
        s_wantRays = !(rt && rt[0] == '0');
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
        s_tint = EnvFloat("SHAR_SUN_TINT", s_tint);
        const char* sc = getenv("SHAR_SUN_COLOUR");
        s_sunColourFixed = sc && sscanf(sc, "%f,%f,%f", &s_fixedSunColour[0], &s_fixedSunColour[1],
                                        &s_fixedSunColour[2]) == 3;
        const char* hc = getenv("SHAR_SUN_SHADE_COLOUR");
        s_shadeColourFixed = hc && sscanf(hc, "%f,%f,%f", &s_fixedShadeColour[0], &s_fixedShadeColour[1],
                                          &s_fixedShadeColour[2]) == 3;
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
        const char* sky = getenv("SHAR_SUN_SKY");
        if(sky && sky[0] == '0' && !sky[1])
            s_skyGrid[0] = 0.0f;
        else if(sky)
            sscanf(sky, "%f,%f", &s_skyGrid[1], &s_skyGrid[2]);
        if(s_skyGrid[0] > 0.0f)
            s_skyGrid[0] = EnvFloat("SHAR_SUN_SKY_CELL", s_skyGrid[0]);
        s_skyGrid[0] = s_skyGrid[0] < 0.0f ? 0.0f : s_skyGrid[0];
        s_skyGrid[2] = s_skyGrid[2] > s_skyGrid[1] + 0.01f ? s_skyGrid[2] : s_skyGrid[1] + 0.01f;
        s_skyGrid[3] = EnvFloat("SHAR_SUN_SKY_DEBUG", 0.0f);
        s_skyRays = (int)EnvFloat("SHAR_SUN_SKY_RAYS", (float)s_skyRays);
        s_skyRays = s_skyRays < 1 ? 1 : s_skyRays > 256 ? 256 : s_skyRays;
        s_skyReach = EnvFloat("SHAR_SUN_SKY_REACH", s_skyReach);
        float skyBudget = EnvFloat("SHAR_SUN_SKY_BUDGET", (float)s_skyBudget);
        s_skyBudget = skyBudget < 64.0f ? 64u : skyBudget > (float)kSkyCount ? kSkyCount : (unsigned)skyBudget;
        const char* dbg = getenv("SHAR_SUN_DEBUG");
        s_debug = dbg ? atoi(dbg) : 0;
    }
    return s_wanted > 0;
}

void pvkSunSetLevel(int level)
{
    if(level != s_level)
        s_version++;        // the tints
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

bool pvkSunWantRays()
{
    return pvkSunWanted() && s_wantRays;
}

bool pvkSunReady()
{
    return g_pvk && pvkSunWanted();
}

bool pvkSunRays()
{
    return pvkSunReady() && g_pvk->rayQuery;
}

// what shades now: 0 off, 1 the map rasterised, 2 rays
static int Mode()
{
    if(!pvkSunReady() || s_quality == 0 || s_suppressed)
        return 0;
    return s_quality >= 2 && g_pvk->rayQuery ? 2 : 1;
}

bool pvkSunOn()
{
    return Mode() > 0;
}

bool pvkSunRaysOn()
{
    return Mode() == 2;
}

void pvkSunSetQuality(int quality)
{
    quality = quality < 0 ? 0 : quality > 2 ? 2 : quality;
    if(quality != s_quality)
        s_version++;
    s_quality = quality;
}

void pvkSunSetSuppressed(bool suppressed)
{
    if(suppressed == s_suppressed)
        return;
    s_version++;
    s_suppressed = suppressed;
    if(s_log)
        fprintf(stderr, "PDDI sun: %s\n", suppressed ? "suppressed (interior or front end)" : "back");
}

int pvkSunQuality()
{
    return s_quality;
}

void pvkSunToggle()
{
    if(!pvkSunReady())
        return;
    // off, low, high (without ray queries: off, low)
    s_quality = s_quality == 0 ? 1 : s_quality == 1 && g_pvk->rayQuery ? 2 : 0;
    s_version++;
    static const char* const kNames[3] = { "off", "low", "high" };
    fprintf(stderr, "PDDI: sun %s\n", kNames[s_quality]);
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
    if(Mode() == 1)
        soft[1] = 4.0f;         // low: no rays; the map's taps (twice this, 8 at least)
    if(s_debug)
        soft[1] = -1.0f;        // the shader's debug view: one ray, the blocker's id as the colour
}

// scaled to luminance 1, then towards white by 1 - SHAR_SUN_TINT
static void Tint(const float* from, float* out)
{
    float l = 0.3f * from[0] + 0.59f * from[1] + 0.11f * from[2];
    for(int c = 0; c < 3; c++)
        out[c] = 1.0f + ((l > 0.0f ? from[c] / l : 1.0f) - 1.0f) * s_tint;
}

static void Mix(const float* a, const float* b, float t, float* out)
{
    for(int c = 0; c < 3; c++)
        out[c] = a[c] + (b[c] - a[c]) * t;
}

void pvkSunColours(float* sun, float* shade)
{
    pvkSunWanted();
    float elevation = asinf(s_direction[1] < -1.0f ? -1.0f : s_direction[1] > 1.0f ? 1.0f : s_direction[1]) *
                      180.0f / 3.14159265f;
    float t = (elevation - 5.0f) / 40.0f;      // 5 degrees up: the warmest, 45: the whitest
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    t = t * t * (3.0f - 2.0f * t);
    bool night = s_level >= 0 && s_level < (int)(sizeof(kLevelNight) / sizeof(kLevelNight[0])) && kLevelNight[s_level];
    static const float kLowSun[3] = { 1.0f, 0.84f, 0.66f }, kHighSun[3] = { 1.0f, 0.97f, 0.92f };
    static const float kLowShade[3] = { 0.86f, 0.90f, 1.08f }, kHighShade[3] = { 0.93f, 0.96f, 1.04f };
    static const float kMoon[3] = { 0.86f, 0.93f, 1.08f }, kNightShade[3] = { 0.84f, 0.90f, 1.10f };
    float s[3], h[3];
    if(night)
    {
        memcpy(s, kMoon, sizeof(s));
        memcpy(h, kNightShade, sizeof(h));
    }
    else
    {
        Mix(kLowSun, kHighSun, t, s);
        Mix(kLowShade, kHighShade, t, h);
    }
    Tint(s_sunColourFixed ? s_fixedSunColour : s, sun);
    Tint(s_shadeColourFixed ? s_fixedShadeColour : h, shade);
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
    // low (Mode 1): no structure, the triangles for rasterising the map: positions, UVs when alpha-tested, uint32
    // triangles (host-visible)
    pvkBuffer mesh;
    unsigned meshVertices, meshTriangles;
    int meshMask, meshCompare;  // the alpha test (meshMask >= 0: alpha-tested)
    float meshRef;
    struct Skin* skin;          // rebuilt every frame: updated instead (b.as is one of its structures)
};

// A caster rebuilt every frame (skinned characters; b.builds > 3): while its triangles stay the same, a new shape
// updates (refits) the last structure into the next instead of a build from scratch. pvkFrames structures, inputs
// (positions, then the triangles) and scratches, used in turn: the one written was last read by a frame that is done
// (or by an earlier upload, ordered before this frame's by its closing barrier).
struct Skin
{
    unsigned short* indices;    // as drawn: the same ones keep the structure
    unsigned count, vertexCount;
    bool strip;
    pvkTexture* alphaTexture;
    int alphaCompare;
    float alphaRef;
    unsigned triangles;
    unsigned first, last;       // the vertices the triangles use (the bounds): first..last, or those in used
    unsigned* used;
    unsigned usedCount;
    pvkBuffer input[pvkFrames], storage[pvkFrames], scratch[pvkFrames];
    VkAccelerationStructureKHR as[pvkFrames];
    VkDeviceAddress address[pvkFrames], scratchAddress[pvkFrames];
    int current;                // b.as's
    unsigned long long serial;  // the frame it was last built or updated in
    unsigned updates;           // since the last full build
};

static const unsigned kSkinRebuild = 120;   // updates between full builds (a refitted tree loosens as the shape moves)
static const VkBuildAccelerationStructureFlagsKHR kSkinFlags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_BUILD_BIT_KHR |
                                                              VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_UPDATE_BIT_KHR;

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

static void FreeSkin(Blas& b, bool now)
{
    Skin* s = b.skin;
    if(!s)
        return;
    for(int f = 0; f < pvkFrames; f++)
    {
        if(now)
        {
            if(s->as[f])
                vkDestroyAccelerationStructureKHR(g_pvk->device, s->as[f], NULL);
            g_pvk->DestroyNow(s->input[f]);
            g_pvk->DestroyNow(s->storage[f]);
            g_pvk->DestroyNow(s->scratch[f]);
        }
        else
        {
            g_pvk->RetireAccel(s->as[f]);
            g_pvk->Retire(s->input[f]);
            g_pvk->Retire(s->storage[f]);
            g_pvk->Retire(s->scratch[f]);
        }
    }
    free(s->indices);
    free(s->used);
    free(s);
    b.skin = NULL;
    b.as = VK_NULL_HANDLE;
}

static void ReleaseBlas(Blas& b)
{
    if(b.skin)
        FreeSkin(b, false);
    g_pvk->RetireAccel(b.as);
    g_pvk->Retire(b.storage);
    g_pvk->Retire(b.mesh);
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

static void SetBounds(Blas& b, const float* lo, const float* hi)
{
    float d[3] = { hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2] };
    b.size = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    memcpy(b.lo, lo, 12);
    memcpy(b.hi, hi, 12);
    b.huge = b.size > kHugeSize;
}

// the bounds of the triangles in list into b
static void CasterBounds(Blas& b, const float* positions, const unsigned* list, unsigned triangles)
{
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    for(unsigned t = 0; t < triangles * 3; t++)
        for(int k = 0; k < 3; k++)
        {
            float v = positions[list[t] * 3 + k];
            lo[k] = v < lo[k] ? v : lo[k];
            hi[k] = v > hi[k] ? v : hi[k];
        }
    SetBounds(b, lo, hi);
}

// a caster's triangles as a list (those in range; NULL: none), its bounds into b; malloc'd
static unsigned* CasterTriangles(Blas& b, const float* positions, unsigned vertexCount, const unsigned short* indices,
                                 unsigned count, bool strip, unsigned* triangles)
{
    unsigned* list = Triangles(indices, count, strip, triangles);
    unsigned kept = 0;
    for(unsigned t = 0; t < *triangles; t++)
    {
        unsigned* tri = &list[t * 3];
        if(tri[0] >= vertexCount || tri[1] >= vertexCount || tri[2] >= vertexCount)
            continue;
        memmove(&list[kept * 3], tri, 12);
        kept++;
    }
    *triangles = kept;
    if(kept == 0)
    {
        free(list);
        return NULL;
    }
    CasterBounds(b, positions, list, kept);
    return list;
}

// low: the triangles kept for rasterising the map (alphaSlot >= 0: alpha-tested, with the UVs)
static bool BuildMesh(Blas& b, const float* positions, const float* uvs, unsigned vertexCount,
                      const unsigned short* indices, unsigned count, bool strip, int alphaSlot, int alphaCompare,
                      float alphaRef)
{
    bool alpha = alphaSlot >= 0 && uvs;
    unsigned triangles;
    unsigned* list = CasterTriangles(b, positions, vertexCount, indices, count, strip, &triangles);
    if(!list)
        return false;
    VkDeviceSize vertexBytes = (VkDeviceSize)vertexCount * 12, uvBytes = alpha ? (VkDeviceSize)vertexCount * 8 : 0;
    if(!g_pvk->CreateBuffer(vertexBytes + uvBytes + (VkDeviceSize)triangles * 12,
                            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, true, &b.mesh))
    {
        free(list);
        return false;
    }
    memcpy(b.mesh.memory.mapped, positions, vertexBytes);
    if(alpha)
        memcpy(b.mesh.memory.mapped + vertexBytes, uvs, uvBytes);
    memcpy(b.mesh.memory.mapped + vertexBytes + uvBytes, list, (size_t)triangles * 12);
    free(list);
    b.meshVertices = vertexCount;
    b.meshTriangles = triangles;
    b.meshMask = alpha ? alphaSlot : -1;
    b.meshCompare = alphaCompare;
    b.meshRef = alphaRef;
    s_buildsThisFrame++;
    return true;
}

static void BlasGeometry(VkAccelerationStructureGeometryKHR& geometry, VkAccelerationStructureBuildGeometryInfoKHR& build,
                         VkDeviceAddress input, unsigned vertexCount, bool alpha, VkBuildAccelerationStructureFlagsKHR flags)
{
    memset(&geometry, 0, sizeof(geometry));
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.flags = alpha ? 0 : VK_GEOMETRY_OPAQUE_BIT_KHR;   // alpha-tested: the shaders decide
    VkAccelerationStructureGeometryTrianglesDataKHR& tri = geometry.geometry.triangles;
    tri.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    tri.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    tri.vertexData.deviceAddress = input;
    tri.vertexStride = 12;
    tri.maxVertex = vertexCount - 1;
    tri.indexType = VK_INDEX_TYPE_UINT32;
    tri.indexData.deviceAddress = input + (VkDeviceSize)vertexCount * 12;
    memset(&build, 0, sizeof(build));
    build.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    build.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    build.flags = flags;
    build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    build.geometryCount = 1;
    build.pGeometries = &geometry;
}

// the skin's next structure from positions: an update of the current one, or every kSkinRebuild a full build
static void SkinBuild(Blas& b, const float* positions, bool update)
{
    Skin* s = b.skin;
    int next = (s->current + 1) % pvkFrames;
    memcpy(s->input[next].memory.mapped, positions, (size_t)s->vertexCount * 12);
    VkAccelerationStructureGeometryKHR geometry;
    VkAccelerationStructureBuildGeometryInfoKHR build;
    BlasGeometry(geometry, build, BufferAddress(s->input[next]), s->vertexCount, b.alpha, kSkinFlags);
    if(update && s->updates < kSkinRebuild)
    {
        build.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_UPDATE_KHR;
        build.srcAccelerationStructure = s->as[s->current];
        s->updates++;
    }
    else
        s->updates = 0;
    build.dstAccelerationStructure = s->as[next];
    build.scratchData.deviceAddress = s->scratchAddress[next];
    VkAccelerationStructureBuildRangeInfoKHR range = { s->triangles, 0, 0, 0 };
    const VkAccelerationStructureBuildRangeInfoKHR* ranges = &range;
    vkCmdBuildAccelerationStructuresKHR(g_pvk->UploadCmd(), 1, &build, &ranges);
    s->current = next;
    s->serial = g_pvk->FrameSerial();
    b.as = s->as[next];
    b.address = s->address[next];
    s_buildsThisFrame++;
}

// b rebuilt every frame: its skin, built from positions (list freed)
static bool MakeSkin(Blas& b, const float* positions, unsigned vertexCount, unsigned* list, unsigned triangles,
                     const unsigned short* indices, unsigned count, bool strip, pvkTexture* alphaTexture,
                     int alphaCompare, float alphaRef)
{
    Skin* s = (Skin*)calloc(1, sizeof(Skin));
    b.skin = s;
    s->indices = (unsigned short*)malloc((size_t)count * 2);
    memcpy(s->indices, indices, (size_t)count * 2);
    s->count = count;
    s->vertexCount = vertexCount;
    s->strip = strip;
    s->alphaTexture = alphaTexture;
    s->alphaCompare = alphaCompare;
    s->alphaRef = alphaRef;
    s->triangles = triangles;
    // the vertices used, for the bounds: usually a run with none left out
    unsigned char* seen = (unsigned char*)calloc(vertexCount, 1);
    s->first = vertexCount;
    s->last = 0;
    for(unsigned t = 0; t < triangles * 3; t++)
    {
        unsigned v = list[t];
        seen[v] = 1;
        s->first = v < s->first ? v : s->first;
        s->last = v > s->last ? v : s->last;
    }
    unsigned n = 0;
    for(unsigned v = s->first; v <= s->last; v++)
        n += seen[v];
    if(n != s->last - s->first + 1)
    {
        s->used = (unsigned*)malloc(n * sizeof(unsigned));
        for(unsigned v = s->first; v <= s->last; v++)
            if(seen[v])
                s->used[s->usedCount++] = v;
    }
    free(seen);

    VkAccelerationStructureGeometryKHR geometry;
    VkAccelerationStructureBuildGeometryInfoKHR build;
    BlasGeometry(geometry, build, 0, vertexCount, b.alpha, kSkinFlags);
    VkAccelerationStructureBuildSizesInfoKHR sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(g_pvk->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build,
                                            &triangles, &sizes);
    VkDeviceSize scratchSize = sizes.buildScratchSize > sizes.updateScratchSize ? sizes.buildScratchSize :
                                                                                  sizes.updateScratchSize;
    VkDeviceSize vertexBytes = (VkDeviceSize)vertexCount * 12;
    for(int f = 0; f < pvkFrames; f++)
    {
        if(!g_pvk->CreateBuffer(vertexBytes + (VkDeviceSize)triangles * 12, kInputUsage, true, &s->input[f]) ||
           !g_pvk->CreateBuffer(sizes.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false, &s->storage[f]) ||
           !g_pvk->CreateBuffer(scratchSize + 256, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, false, &s->scratch[f]))
        {
            free(list);
            FreeSkin(b, false);
            return false;
        }
        memcpy(s->input[f].memory.mapped + vertexBytes, list, (size_t)triangles * 12);
        s->scratchAddress[f] = (BufferAddress(s->scratch[f]) + 255) & ~(VkDeviceAddress)255;
        VkAccelerationStructureCreateInfoKHR create = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR };
        create.buffer = s->storage[f].buffer;
        create.size = sizes.accelerationStructureSize;
        create.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
        if(vkCreateAccelerationStructureKHR(g_pvk->device, &create, NULL, &s->as[f]) != VK_SUCCESS)
        {
            s->as[f] = VK_NULL_HANDLE;
            free(list);
            FreeSkin(b, false);
            return false;
        }
        VkAccelerationStructureDeviceAddressInfoKHR ai = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR };
        ai.accelerationStructure = s->as[f];
        s->address[f] = vkGetAccelerationStructureDeviceAddressKHR(g_pvk->device, &ai);
    }
    free(list);
    SkinBuild(b, positions, false);
    return true;
}

static void SkinBounds(Blas& b, const float* positions)
{
    const Skin* s = b.skin;
    float lo[3] = { 1e30f, 1e30f, 1e30f }, hi[3] = { -1e30f, -1e30f, -1e30f };
    unsigned n = s->used ? s->usedCount : s->last - s->first + 1;
    for(unsigned i = 0; i < n; i++)
    {
        const float* p = &positions[(s->used ? s->used[i] : s->first + i) * 3];
        for(int k = 0; k < 3; k++)
        {
            lo[k] = p[k] < lo[k] ? p[k] : lo[k];
            hi[k] = p[k] > hi[k] ? p[k] : hi[k];
        }
    }
    SetBounds(b, lo, hi);
}

// a new shape for a skinned caster with the same triangles: its structure updated (false: build it again)
static bool UpdateSkin(Blas& b, const float* positions, unsigned vertexCount, const unsigned short* indices,
                       unsigned count, bool strip, pvkTexture* alphaTexture, int alphaCompare, float alphaRef)
{
    Skin* s = b.skin;
    if(!s || !b.live || s->vertexCount != vertexCount || s->count != count || s->strip != strip ||
       s->alphaTexture != alphaTexture || s->alphaCompare != alphaCompare || s->alphaRef != alphaRef ||
       memcmp(s->indices, indices, (size_t)count * 2) != 0)
        return false;
    SkinBounds(b, positions);
    if(s->serial == g_pvk->FrameSerial())
        // a second shape this frame: the build recorded for the first reads it when the frame is submitted
        memcpy(s->input[s->current].memory.mapped, positions, (size_t)vertexCount * 12);
    else
        SkinBuild(b, positions, true);
    return true;
}

// alphaSlot >= 0: alpha-tested (uvs, the mask and the test written to the table at id). Rebuilt often (builds > 3):
// made as a skin (alphaTexture and the test kept to tell when it changes)
static bool BuildBlas(Blas& b, unsigned id, const float* positions, const float* uvs, unsigned vertexCount,
                      const unsigned short* indices, unsigned count, bool strip, int alphaSlot, pvkTexture* alphaTexture,
                      int alphaCompare, float alphaRef)
{
    bool alpha = alphaSlot >= 0 && uvs && id < kMaxCasterIds && s_casterTable.memory.mapped;
    b.alpha = false;
    unsigned triangles;
    unsigned* list = CasterTriangles(b, positions, vertexCount, indices, count, strip, &triangles);
    if(!list)
        return false;

    VkDeviceSize vertexBytes = (VkDeviceSize)vertexCount * 12;
    VkDeviceSize triangleBytes = (VkDeviceSize)triangles * 12;
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
        return false;
    }
    if(b.builds > 3)
        return MakeSkin(b, positions, vertexCount, list, triangles, indices, count, strip, alphaTexture, alphaCompare,
                        alphaRef);

    // the input in host-visible memory, read by the build straight from there
    pvkBuffer input;
    if(!g_pvk->CreateBuffer(vertexBytes + triangleBytes, kInputUsage, true, &input))
    {
        free(list);
        return false;
    }
    memcpy(input.memory.mapped, positions, vertexBytes);
    memcpy(input.memory.mapped + vertexBytes, list, triangleBytes);
    free(list);

    VkAccelerationStructureGeometryKHR geometry;
    VkAccelerationStructureBuildGeometryInfoKHR build;
    BlasGeometry(geometry, build, BufferAddress(input), vertexCount, alpha,
                 VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR);
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
static unsigned s_worldEyeFrame;

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
    int mode = Mode();
    // low: only the static world casts (into the map); lit things (cars, characters) keep their blob shadows
    if(mode == 0 || vertexCount == 0 || (mode == 1 && !keep) || mode != s_casterMode)
        return;
    if(!*id)
        *id = NewId();
    Blas& b = s_blas[*id];
    if(!b.live || b.version != version)
    {
        // rebuilt every frame (skinned characters, animated things): a budget per frame, the old shape meanwhile
        if(s_buildsThisFrame >= kMaxBuildsPerFrame && b.builds > 3)
        {
            if(!b.as && !b.mesh.buffer)
                return;
        }
        else if(UpdateSkin(b, positions, vertexCount, indices, count, strip, alphaTexture, alphaCompare, alphaRef))
        {
            b.version = version;
            b.builds++;
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
            if(mode == 1 ? !BuildMesh(b, positions, uvs, vertexCount, indices, count, strip, slot, alphaCompare, alphaRef) :
                           !BuildBlas(b, *id, positions, uvs, vertexCount, indices, count, strip, slot, alphaTexture,
                                      alphaCompare, alphaRef))
                return;
            if(s_logLevel >= 2 && b.builds == 1)
                fprintf(stderr, "PDDI sun: caster %u = %s (%s), %.1f m, %u vertices, %u indices, %s\n", *id,
                        s_drawingMaterial, s_drawingFile, b.size, vertexCount, count, keep ? "world" : "lit");
        }
    }
    if((!b.as && !b.mesh.buffer) || b.huge)
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
    // the frame's first: the world is drawn before the HUD (the radar's map, 150 m up, casts at the power plant)
    if(s_worldEyeFrame != s_frame)
    {
        memcpy(s_worldEye, s_eyeToWorld, sizeof(s_worldEye));
        memcpy(s_worldProjection, s_viewProjection, sizeof(s_worldProjection));
        s_worldEyeFrame = s_frame;
    }
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

// the buffers the programs read (bindings 6-9), and with ray queries the scenes
static bool MakeScenes()
{
    if(!g_pvk->CreateBuffer((VkDeviceSize)kMaxCasterIds * 16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, &s_casterTable) ||
       !g_pvk->CreateBuffer((VkDeviceSize)kMaxMasks * kMaskSize * kMaskSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true,
                            &s_masks) ||
       !g_pvk->CreateBuffer((VkDeviceSize)kGeometryWords * 4, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true, &s_geometry) ||
       !g_pvk->CreateBuffer(kMapBytes + (VkDeviceSize)kSkyCount * 8,
                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                            VK_BUFFER_USAGE_TRANSFER_DST_BIT, false, &s_mapBuffer))
        return false;
    s_geometryFreeCount = 0;
    GeometryFree(0, kGeometryWords);
    if(!g_pvk->rayQuery)
        return true;
    VkAccelerationStructureGeometryKHR geometry;
    VkAccelerationStructureBuildGeometryInfoKHR build;
    TopLevelGeometry(geometry, build, 0);
    unsigned max = kMaxInstances;
    VkAccelerationStructureBuildSizesInfoKHR sizes = { VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR };
    vkGetAccelerationStructureBuildSizesKHR(g_pvk->device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build, &max,
                                            &sizes);
    for(int f = 0; f < pvkFrames; f++)
        if(!MakeTlas(s_scenes[f].draws, sizes) || (s_bake && !MakeTlas(s_scenes[f].world, sizes)))
            return false;
    return true;
}

bool pvkSunPrepare()
{
    if(!pvkSunReady() || s_scenesFailed)
        return false;
    if(!s_scenesMade)
    {
        s_scenesMade = true;
        if(!MakeScenes())
        {
            fprintf(stderr, "PDDI: cannot make the sun's scene\n");
            s_scenesFailed = true;
            return false;
        }
    }
    return true;
}

// the draws' scene
VkAccelerationStructureKHR pvkSunScene()
{
    if(!pvkSunPrepare() || !g_pvk->rayQuery)
        return VK_NULL_HANDLE;
    return s_scenes[g_pvk->FrameSerial() % pvkFrames].draws.as;
}

VkAccelerationStructureKHR pvkSunWorldScene()
{
    VkAccelerationStructureKHR draws = pvkSunScene();
    const Tlas& world = s_scenes[g_pvk->FrameSerial() % pvkFrames].world;
    return draws && world.as ? world.as : draws;
}

// out is in the mapped instance buffer (uncached: the bitfields' read-modify-writes read it back): filled here, then
// written in one copy
static void FillInstance(VkAccelerationStructureInstanceKHR& out, const Instance& in)
{
    VkAccelerationStructureInstanceKHR local;
    memcpy(local.transform.matrix, in.transform, sizeof(local.transform.matrix));
    const Blas& b = s_blas[in.id];
    local.instanceCustomIndex = in.id;    // the caster table's row (alpha-tested), SHAR_SUN_DEBUG's colour
    local.mask = in.keep ? 0x01 : 0x02;     // the world; lit things (cars), which lit draws' rays skip
    local.instanceShaderBindingTableRecordOffset = 0;
    local.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR |
                  (b.alpha ? 0 : VK_GEOMETRY_INSTANCE_FORCE_OPAQUE_BIT_KHR);
    local.accelerationStructureReference = b.address;
    memcpy(&out, &local, sizeof(local));
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
static bool s_rasterFailed;        // low's map pass (RasterReady)
static unsigned s_rasterDraws;     // SHAR_SUN_LOG: casters drawn into the map
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

// a compute program over the bake's bindings (the world's scene, the casters' table, masks, geometry, the map)
static bool MakeCompute(const char* name, const char* source, unsigned pushBytes, VkPipelineLayout* layout,
                        VkPipeline* pipeline)
{
    unsigned* words = NULL;
    size_t count = 0;
    char* log = NULL;
    bool ok = shar_spirv_compile(name, 2, source, &words, &count, &log) != 0;
    if(log)
        fprintf(stderr, "PDDI: %s\n", log);
    VkShaderModule module = ok ? pvkCreateShaderModule(words, count) : VK_NULL_HANDLE;
    shar_spirv_free(words);
    shar_spirv_free(log);
    if(!module)
        return false;
    ok = true;
    if(!s_bakeSetLayout)
    {
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
        ok = vkCreateDescriptorSetLayout(g_pvk->device, &set, NULL, &s_bakeSetLayout) == VK_SUCCESS;
    }
    VkPushConstantRange range = { VK_SHADER_STAGE_COMPUTE_BIT, 0, pushBytes };
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
    ok = ok && vkCreatePipelineLayout(g_pvk->device, &pl, NULL, layout) == VK_SUCCESS;
    info.layout = *layout;
    ok = ok && vkCreateComputePipelines(g_pvk->device, VK_NULL_HANDLE, 1, &info, NULL, pipeline) == VK_SUCCESS;
    vkDestroyShaderModule(g_pvk->device, module, NULL);
    return ok;
}

static bool MakeBake()
{
    return MakeCompute("sun bake", kBakeSource, sizeof(BakePush), &s_bakeLayout, &s_bakePipeline);
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

static bool Baking();

static const char* const kSkySource =
    "#version 460\n"
    "#extension GL_EXT_ray_query : require\n"
    "// the sky grid (vksun.cpp): per cell, the share of rays over the upper hemisphere (even in solid angle) from its\n"
    "// centre that leave the static world; hits nearer than the cell (the ground or a wall through it) ignored. Every\n"
    "// slot is looked at: traced when it holds another cell (the window moved), or in this frame's refresh slice\n"
    "layout(local_size_x = 64) in;\n"
    "layout(binding = 0) uniform accelerationStructureEXT world;\n"
    "layout(std430, binding = 1) readonly buffer SunCasters { uvec4 sunCaster[]; };\n"
    "layout(std430, binding = 2) readonly buffer SunMasks { uint sunMask[]; };\n"
    "layout(std430, binding = 3) readonly buffer SunGeometry { uint sunGeometry[]; };\n"
    "layout(std430, binding = 4) buffer SunMap { float sunMap[]; };\n"
    "layout(push_constant) uniform Sky\n"
    "{\n"
    "    ivec4 window;       // the grid's first cell (x, y, z); w: the refresh slice's first slot\n"
    "    ivec4 count;        // x: the slice's slots, y: rays per cell\n"
    "    vec4 size;          // x: cell (m), y: hits ignored nearer, z: the rays' length\n"
    "} sky;\n"
    "const ivec3 kCells = ivec3(128, 32, 128);\n"
    "const uint kBase = 16777216u;   // floats: after the map's 4 levels of 2048^2\n"
    "vec2 SunUV(uint base, uint vertex)\n"
    "{\n"
    "    return vec2(uintBitsToFloat(sunGeometry[base + vertex * 2u]), uintBitsToFloat(sunGeometry[base + vertex * 2u + 1u]));\n"
    "}\n"
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
    "    uint slot = gl_GlobalInvocationID.x;\n"
    "    uint total = uint(kCells.x * kCells.y * kCells.z);\n"
    "    if(slot >= total)\n"
    "        return;\n"
    "    ivec3 s = ivec3(slot % 128u, (slot / 128u) % 32u, slot / 4096u);\n"
    "    ivec3 c = sky.window.xyz + ((s - sky.window.xyz) & (kCells - 1));   // the window's cell in this slot\n"
    "    // which cell it holds (the slot is shared by cells 128 or 32 apart): exact as a float\n"
    "    float key = float(1 + (c.x & 255) + (c.y & 255) * 256 + (c.z & 255) * 65536);\n"
    "    if(sunMap[kBase + slot * 2u + 1u] == key && (slot - uint(sky.window.w)) % total >= uint(sky.count.x))\n"
    "        return;\n"
    "    vec3 origin = (vec3(c) + 0.5) * sky.size.x;\n"
    "    int rays = sky.count.y, open = 0;\n"
    "    float turn = 6.2831853 * fract(float(c.x * 73 + c.y * 151 + c.z * 311) * 0.6180340);\n"
    "    for(int r = 0; r < rays; r++)\n"
    "    {\n"
    "        float up = (float(r) + 0.5) / float(rays);\n"
    "        float out_ = sqrt(1.0 - up * up);\n"
    "        float a = float(r) * 2.3999632 + turn;\n"
    "        vec3 d = vec3(out_ * cos(a), up, out_ * sin(a));\n"
    "        rayQueryEXT q;\n"
    "        rayQueryInitializeEXT(q, world, gl_RayFlagsTerminateOnFirstHitEXT, 0xff, origin, sky.size.y, d, sky.size.z);\n"
    "        while(rayQueryProceedEXT(q))\n"
    "        {\n"
    "            if(SunOpaque(rayQueryGetIntersectionInstanceCustomIndexEXT(q, false),\n"
    "                         rayQueryGetIntersectionPrimitiveIndexEXT(q, false),\n"
    "                         rayQueryGetIntersectionBarycentricsEXT(q, false)))\n"
    "                rayQueryConfirmIntersectionEXT(q);\n"
    "        }\n"
    "        open += int(rayQueryGetIntersectionTypeEXT(q, true) == gl_RayQueryCommittedIntersectionNoneEXT);\n"
    "    }\n"
    "    sunMap[kBase + slot * 2u] = float(open) / float(rays);\n"
    "    sunMap[kBase + slot * 2u + 1u] = key;\n"
    "}\n";

struct SkyPush
{
    int window[4];
    int count[4];
    float size[4];
};

static VkPipelineLayout s_skyLayout;
static VkPipeline s_skyPipeline;
static bool s_skyFailed;
static bool s_skyCleared;       // the grid's keys zeroed (none held) once made
static unsigned s_skyCursor;    // the next slot to bake
static unsigned s_coverKnownCount;    // pvkSunCover's answers held (SkyBake's)

static bool SkyReady()
{
    if(s_skyGrid[0] <= 0.0f)
        return false;
    if(!s_skyPipeline && !s_skyFailed)
    {
        s_skyFailed = !MakeCompute("sun sky", kSkySource, sizeof(SkyPush), &s_skyLayout, &s_skyPipeline);
        if(s_skyFailed)
            fprintf(stderr, "PDDI: cannot make the sun's sky grid: shadows under cover as in the open\n");
        s_version++;
    }
    return s_skyPipeline != VK_NULL_HANDLE;
}

void pvkSunSky(float* sky)
{
    bool on = Mode() == 2 && Baking() && s_skyPipeline;
    sky[0] = on ? s_skyGrid[0] : 0.0f;
    sky[1] = s_skyGrid[1];
    sky[2] = s_skyGrid[2];
    sky[3] = s_skyGrid[3];
}

static bool Baking()
{
    return Mode() == 1 ? !s_rasterFailed : s_bake && !s_bakeFailed;
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
    s_skyCleared = false;   // (a level started: the last one's cells)
    s_coverKnownCount = 0;
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

//-----------------------------------------------------------------------------
// Low (Mode 1): the map rasterised, for devices without ray queries
//-----------------------------------------------------------------------------
// The same map, strips and tiles as the bake above; a chunk of a level's texels (up to kRasterRows rows) is drawn as
// the sun sees it: an orthographic depth pass of the world's instances whose footprint touches it (depth from the
// rays' start height down, over the bake's ray length), then a compute pass turns the depth into the map's heights.
// Alpha-tested casters discard where their mask does.

static const int kRasterRows = 512;
static const float kRasterLength = 4000.0f;     // as the bake's rays
static const unsigned kRasterChunks = 16;       // chunks a frame at most (each draws the casters over it)
static pvkImage s_rasterDepth;
static VkSampler s_rasterSampler;
static VkDescriptorSetLayout s_rasterSetLayout, s_copySetLayout;
static VkPipelineLayout s_rasterLayout, s_copyLayout;
static VkPipeline s_rasterPipelines[2];         // opaque, alpha-tested
static VkPipeline s_copyPipeline;

static const char* const kRasterVertex =
    "// the sun's map rasterised (vksun.cpp): object -> the chunk's texels, depth down from the rays' start\n"
    "layout(location = 0) in vec3 aPosition;\n"
    "#ifdef ALPHA\n"
    "layout(location = 1) in vec2 aUV;\n"
    "layout(location = 0) out vec2 vUV;\n"
    "#endif\n"
    "layout(push_constant) uniform Raster { mat4 transform; uvec4 test; } raster;\n"
    "void main()\n"
    "{\n"
    "#ifdef ALPHA\n"
    "    vUV = aUV;\n"
    "#endif\n"
    "    gl_Position = raster.transform * vec4(aPosition, 1.0);\n"
    "}\n";

static const char* const kRasterFragment =
    "#version 450\n"
    "// an alpha-tested caster: through where its alpha test fails (test: mask, compare, alpha ref)\n"
    "layout(location = 0) in vec2 vUV;\n"
    "layout(std430, binding = 0) readonly buffer SunMasks { uint sunMask[]; };\n"
    "layout(push_constant) uniform Raster { mat4 transform; uvec4 test; } raster;\n"
    "void main()\n"
    "{\n"
    "    uvec2 texel = uvec2(ivec2(floor(fract(vUV) * 64.0)) & 63);\n"
    "    uint i = raster.test.x * 4096u + texel.y * 64u + texel.x;\n"
    "    float alpha = float((sunMask[i >> 2] >> ((i & 3u) * 8u)) & 255u) / 255.0;\n"
    "    float ref = uintBitsToFloat(raster.test.z);\n"
    "    if(!(raster.test.y == 4u ? alpha > ref : alpha >= ref))\n"
    "        discard;\n"
    "}\n";

static const char* const kCopySource =
    "#version 450\n"
    "// the rasterised chunk's depth -> the map's heights (-1e30: nothing)\n"
    "layout(local_size_x = 8, local_size_y = 8) in;\n"
    "layout(binding = 0) uniform sampler2D depth;\n"
    "layout(std430, binding = 1) writeonly buffer SunMap { float sunMap[]; };\n"
    "layout(push_constant) uniform Copy\n"
    "{\n"
    "    ivec4 rect;         // first texel, texels across and down\n"
    "    ivec2 level;        // level, texels across a level\n"
    "    vec2 height;        // the rays' start height, depth 1's distance below it\n"
    "} copy;\n"
    "void main()\n"
    "{\n"
    "    uvec2 g = gl_GlobalInvocationID.xy;\n"
    "    if(g.x >= uint(copy.rect.z) || g.y >= uint(copy.rect.w))\n"
    "        return;\n"
    "    float d = texelFetch(depth, ivec2(g), 0).r;\n"
    "    ivec2 i = copy.rect.xy + ivec2(g);\n"
    "    int n = copy.level.y;\n"
    "    sunMap[(copy.level.x * n + (i.y & (n - 1))) * n + (i.x & (n - 1))] = d >= 1.0 ? -1e30 :\n"
    "                                                                        copy.height.x - d * copy.height.y;\n"
    "}\n";

struct RasterPush
{
    float transform[16];        // column-major
    unsigned test[4];           // mask, compare, alpha ref (bits)
};

struct CopyPush
{
    int rect[4];
    int level[2];
    float height[2];
};

static VkShaderModule Module(const char* name, int stage, const char* source)
{
    unsigned* words = NULL;
    size_t count = 0;
    char* log = NULL;
    bool ok = shar_spirv_compile(name, stage, source, &words, &count, &log) != 0;
    if(log)
        fprintf(stderr, "PDDI: %s\n", log);
    VkShaderModule module = ok ? pvkCreateShaderModule(words, count) : VK_NULL_HANDLE;
    shar_spirv_free(words);
    shar_spirv_free(log);
    return module;
}

static bool PushLayout(VkDescriptorType* types, int count, VkShaderStageFlags stages, unsigned pushBytes,
                       VkDescriptorSetLayout* set, VkPipelineLayout* layout)
{
    VkDescriptorSetLayoutBinding bindings[2];
    memset(bindings, 0, sizeof(bindings));
    for(int i = 0; i < count; i++)
    {
        bindings[i].binding = i;
        bindings[i].descriptorType = types[i];
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = stages;
    }
    VkDescriptorSetLayoutCreateInfo si = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    si.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
    si.bindingCount = count;
    si.pBindings = bindings;
    if(vkCreateDescriptorSetLayout(g_pvk->device, &si, NULL, set) != VK_SUCCESS)
        return false;
    VkPushConstantRange range = { stages, 0, pushBytes };
    VkPipelineLayoutCreateInfo pl = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
    pl.setLayoutCount = 1;
    pl.pSetLayouts = set;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &range;
    return vkCreatePipelineLayout(g_pvk->device, &pl, NULL, layout) == VK_SUCCESS;
}

static bool MakeRasterPipeline(bool alpha, VkPipeline* out)
{
    char source[2048];
    snprintf(source, sizeof(source), "#version 450\n%s%s", alpha ? "#define ALPHA 1\n" : "", kRasterVertex);
    VkShaderModule vertex = Module("sun map vertex", 0, source);
    VkShaderModule fragment = alpha ? Module("sun map fragment", 1, kRasterFragment) : VK_NULL_HANDLE;
    bool ok = vertex && (!alpha || fragment);
    VkPipelineShaderStageCreateInfo stages[2];
    memset(stages, 0, sizeof(stages));
    for(int i = 0; i < 2; i++)
    {
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i == 0 ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[i].module = i == 0 ? vertex : fragment;
        stages[i].pName = "main";
    }
    VkVertexInputBindingDescription vb[2] = { { 0, 12, VK_VERTEX_INPUT_RATE_VERTEX }, { 1, 8, VK_VERTEX_INPUT_RATE_VERTEX } };
    VkVertexInputAttributeDescription va[2] = { { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 }, { 1, 1, VK_FORMAT_R32G32_SFLOAT, 0 } };
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    vi.vertexBindingDescriptionCount = alpha ? 2 : 1;
    vi.pVertexBindingDescriptions = vb;
    vi.vertexAttributeDescriptionCount = alpha ? 2 : 1;
    vi.pVertexAttributeDescriptions = va;
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo ds = { VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
    ds.depthTestEnable = VK_TRUE;
    ds.depthWriteEnable = VK_TRUE;
    ds.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendStateCreateInfo cb = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkDynamicState dynamic[2] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dy = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dynamic;
    VkPipelineRenderingCreateInfo rendering = { VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
    rendering.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    VkGraphicsPipelineCreateInfo info = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    info.pNext = &rendering;
    info.stageCount = alpha ? 2 : 1;
    info.pStages = stages;
    info.pVertexInputState = &vi;
    info.pInputAssemblyState = &ia;
    info.pViewportState = &vp;
    info.pRasterizationState = &rs;
    info.pMultisampleState = &ms;
    info.pDepthStencilState = &ds;
    info.pColorBlendState = &cb;
    info.pDynamicState = &dy;
    info.layout = s_rasterLayout;
    ok = ok && vkCreateGraphicsPipelines(g_pvk->device, VK_NULL_HANDLE, 1, &info, NULL, out) == VK_SUCCESS;
    if(vertex)
        vkDestroyShaderModule(g_pvk->device, vertex, NULL);
    if(fragment)
        vkDestroyShaderModule(g_pvk->device, fragment, NULL);
    return ok;
}

static bool MakeRaster()
{
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(g_pvk->physical, VK_FORMAT_D32_SFLOAT, &fp);
    VkFormatFeatureFlags need = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    if((fp.optimalTilingFeatures & need) != need ||
       !g_pvk->CreateImage(kMapSize, kRasterRows, 1, VK_FORMAT_D32_SFLOAT, VK_SAMPLE_COUNT_1_BIT,
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_IMAGE_ASPECT_DEPTH_BIT, &s_rasterDepth))
        return false;
    VkSamplerCreateInfo si = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
    si.magFilter = si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if(vkCreateSampler(g_pvk->device, &si, NULL, &s_rasterSampler) != VK_SUCCESS)
        return false;
    VkDescriptorType rasterTypes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
    VkDescriptorType copyTypes[2] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER };
    if(!PushLayout(rasterTypes, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, sizeof(RasterPush),
                   &s_rasterSetLayout, &s_rasterLayout) ||
       !PushLayout(copyTypes, 2, VK_SHADER_STAGE_COMPUTE_BIT, sizeof(CopyPush), &s_copySetLayout, &s_copyLayout) ||
       !MakeRasterPipeline(false, &s_rasterPipelines[0]) || !MakeRasterPipeline(true, &s_rasterPipelines[1]))
        return false;
    VkShaderModule copy = Module("sun map copy", 2, kCopySource);
    if(!copy)
        return false;
    VkComputePipelineCreateInfo info = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = copy;
    info.stage.pName = "main";
    info.layout = s_copyLayout;
    bool ok = vkCreateComputePipelines(g_pvk->device, VK_NULL_HANDLE, 1, &info, NULL, &s_copyPipeline) == VK_SUCCESS;
    vkDestroyShaderModule(g_pvk->device, copy, NULL);
    return ok;
}

static bool RasterReady()
{
    if(!s_copyPipeline && !s_rasterFailed)
    {
        s_rasterFailed = !MakeRaster();
        if(s_rasterFailed)
            fprintf(stderr, "PDDI: cannot make the sun's map pass: no low shadows\n");
    }
    return s_copyPipeline != VK_NULL_HANDLE;
}

static void DestroyRaster()
{
    g_pvk->DestroyNow(s_rasterDepth);
    for(int i = 0; i < 2; i++)
        if(s_rasterPipelines[i])
            vkDestroyPipeline(g_pvk->device, s_rasterPipelines[i], NULL);
    if(s_copyPipeline)
        vkDestroyPipeline(g_pvk->device, s_copyPipeline, NULL);
    if(s_rasterSampler)
        vkDestroySampler(g_pvk->device, s_rasterSampler, NULL);
    VkPipelineLayout layouts[2] = { s_rasterLayout, s_copyLayout };
    VkDescriptorSetLayout sets[2] = { s_rasterSetLayout, s_copySetLayout };
    for(int i = 0; i < 2; i++)
    {
        if(layouts[i])
            vkDestroyPipelineLayout(g_pvk->device, layouts[i], NULL);
        if(sets[i])
            vkDestroyDescriptorSetLayout(g_pvk->device, sets[i], NULL);
    }
    memset(s_rasterPipelines, 0, sizeof(s_rasterPipelines));
    s_copyPipeline = VK_NULL_HANDLE;
    s_rasterSampler = VK_NULL_HANDLE;
    s_rasterLayout = s_copyLayout = VK_NULL_HANDLE;
    s_rasterSetLayout = s_copySetLayout = VK_NULL_HANDLE;
    s_rasterFailed = false;
}

static void DepthBarrier(VkCommandBuffer cmd, bool toAttachment)
{
    VkImageMemoryBarrier2 b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
    if(toAttachment)
    {
        // the last chunk's copy has read it; cleared
        b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        b.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    }
    else
    {
        b.srcStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        b.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = s_rasterDepth.image;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.layerCount = 1;
    VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
}

// rows from the top of r rasterised (up to the budget's texels, at least one row); they come off r. The budget used:
// at least a kRasterChunks'th of the frame's, as each chunk draws every caster over it.
static unsigned RasterRows(VkCommandBuffer cmd, int level, MapRect& r, unsigned budget, float top)
{
    VkDescriptorBufferInfo buffers[4];
    if(!pvkSunBuffers(buffers))
        return budget;
    int across = r.x1 - r.x0;
    int rows = (int)(budget / (unsigned)across);
    rows = rows < 1 ? 1 : rows > r.y1 - r.y0 ? r.y1 - r.y0 : rows;
    rows = rows > kRasterRows ? kRasterRows : rows;

    DepthBarrier(cmd, true);
    VkRenderingAttachmentInfo depth = { VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
    depth.imageView = s_rasterDepth.view;
    depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depth.clearValue.depthStencil.depth = 1.0f;
    VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO };
    ri.renderArea.extent.width = (unsigned)across;
    ri.renderArea.extent.height = (unsigned)rows;
    ri.layerCount = 1;
    ri.pDepthAttachment = &depth;
    vkCmdBeginRendering(cmd, &ri);
    VkViewport viewport = { 0.0f, 0.0f, (float)across, (float)rows, 0.0f, 1.0f };
    VkRect2D scissor = ri.renderArea;
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    VkWriteDescriptorSet write = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.pBufferInfo = &buffers[1];        // the masks
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_rasterLayout, 0, 1, &write);

    // world -> clip: x, y across the chunk's texels on the sun's plane, depth down from top
    double texel = MapTexel(level);
    double world[3][4];
    for(int k = 0; k < 3; k++)
    {
        world[0][k] = s_basis[0][k] / (texel * across) * 2.0;
        world[1][k] = s_basis[1][k] / (texel * rows) * 2.0;
        world[2][k] = -s_direction[k] / kRasterLength;
    }
    world[0][3] = -(double)r.x0 * 2.0 / across - 1.0;
    world[1][3] = -(double)r.y0 * 2.0 / rows - 1.0;
    world[2][3] = top / kRasterLength;
    // the chunk on the plane (m), for the casters' footprints
    float u0 = (float)(r.x0 * texel), u1 = (float)((r.x0 + across) * texel);
    float v0 = (float)(r.y0 * texel), v1 = (float)((r.y0 + rows) * texel);
    int bound = -1;
    for(unsigned i = 0; i < s_instanceCount; i++)
    {
        const Instance& in = s_instances[i];
        const Blas& b = s_blas[in.id];
        if(!in.world || !b.mesh.buffer)
            continue;
        float u = in.centre[0] * s_basis[0][0] + in.centre[1] * s_basis[0][1] + in.centre[2] * s_basis[0][2];
        float v = in.centre[0] * s_basis[1][0] + in.centre[1] * s_basis[1][1] + in.centre[2] * s_basis[1][2];
        if(u + in.radius < u0 || u - in.radius > u1 || v + in.radius < v0 || v - in.radius > v1)
            continue;
        bool alpha = b.meshMask >= 0;
        if(bound != (int)alpha)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, s_rasterPipelines[alpha]);
            bound = alpha;
        }
        RasterPush pc;
        for(int row = 0; row < 3; row++)
        {
            for(int c = 0; c < 4; c++)
            {
                double sum = c == 3 ? world[row][3] : 0.0;
                for(int k = 0; k < 3; k++)
                    sum += world[row][k] * in.transform[k * 4 + c];
                pc.transform[c * 4 + row] = (float)sum;
            }
        }
        pc.transform[3] = pc.transform[7] = pc.transform[11] = 0.0f;
        pc.transform[15] = 1.0f;
        pc.test[0] = alpha ? (unsigned)b.meshMask : 0u;
        pc.test[1] = (unsigned)b.meshCompare;
        memcpy(&pc.test[2], &b.meshRef, 4);
        pc.test[3] = 0;
        vkCmdPushConstants(cmd, s_rasterLayout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(pc), &pc);
        VkDeviceSize positions = (VkDeviceSize)b.meshVertices * 12, uvs = alpha ? (VkDeviceSize)b.meshVertices * 8 : 0;
        VkBuffer vertexBuffers[2] = { b.mesh.buffer, b.mesh.buffer };
        VkDeviceSize offsets[2] = { 0, positions };
        vkCmdBindVertexBuffers2(cmd, 0, alpha ? 2 : 1, vertexBuffers, offsets, NULL, NULL);
        vkCmdBindIndexBuffer(cmd, b.mesh.buffer, positions + uvs, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, b.meshTriangles * 3, 1, 0, 0, 0);
        s_rasterDraws++;
    }
    vkCmdEndRendering(cmd);
    DepthBarrier(cmd, false);

    VkDescriptorImageInfo image = { s_rasterSampler, s_rasterDepth.view, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL };
    VkWriteDescriptorSet writes[2];
    memset(writes, 0, sizeof(writes));
    for(int i = 0; i < 2; i++)
    {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
    }
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &image;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &buffers[3];    // the map
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_copyPipeline);
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, s_copyLayout, 0, 2, writes);
    CopyPush cp = { { r.x0, r.y0, across, rows }, { level, kMapSize }, { top, kRasterLength } };
    vkCmdPushConstants(cmd, s_copyLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(cp), &cp);
    vkCmdDispatch(cmd, (unsigned)(across + 7) / 8, (unsigned)(rows + 7) / 8, 1);
    r.y0 += rows;
    s_bakedTexels += (unsigned)(across * rows);
    unsigned used = (unsigned)(across * rows), least = s_bakeBudget / kRasterChunks;
    return used > least ? used : least;
}

// each level's window round the camera, moved when it is an eighth of the way off; what came in is to bake
static void MapPlace()
{
    const float* e = s_worldEye;
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

// rows from the top of r, up to the budget's texels (at least one row); they come off r. Budget used (texels baked;
// low: RasterRows's).
static unsigned BakeRows(VkCommandBuffer cmd, int level, MapRect& r, unsigned budget, float top)
{
    if(Mode() == 1)
        return RasterRows(cmd, level, r, budget, top);
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

static void BindBake(VkCommandBuffer cmd, VkAccelerationStructureKHR world, const VkDescriptorBufferInfo* buffers,
                     VkPipeline pipeline, VkPipelineLayout layout)
{
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
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    vkCmdPushDescriptorSetKHR(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 5, writes);
}

// world: the static world's scene (rays); VK_NULL_HANDLE: low (rasterised)
static void Bake(VkCommandBuffer cmd, VkAccelerationStructureKHR world)
{
    VkDescriptorBufferInfo buffers[4];
    if(!pvkSunBuffers(buffers))
        return;
    if(world)
        BindBake(cmd, world, buffers, s_bakePipeline, s_bakeLayout);
    else
    {
        // the map's texels free of the draws of frames before (still running)
        VkMemoryBarrier2 b = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        b.srcStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        b.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    // the rays start well above the camera
    const float* e = s_worldEye;
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

// the next s_skyBudget slots of the sky grid round the camera, through the static world's scene (built)
static void SkyBake(VkCommandBuffer cmd, VkAccelerationStructureKHR world)
{
    VkDescriptorBufferInfo buffers[4];
    if(!pvkSunBuffers(buffers))
        return;
    if(!s_skyCleared)
    {
        vkCmdFillBuffer(cmd, s_mapBuffer.buffer, kMapBytes, (VkDeviceSize)kSkyCount * 8, 0);
        VkMemoryBarrier2 b = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
        b.srcStageMask = VK_PIPELINE_STAGE_2_CLEAR_BIT;
        b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        b.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
        VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.memoryBarrierCount = 1;
        dep.pMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);
        s_skyCleared = true;
    }
    BindBake(cmd, world, buffers, s_skyPipeline, s_skyLayout);
    SkyPush pc;
    for(int k = 0; k < 3; k++)
        pc.window[k] = (int)floorf(s_worldEye[12 + k] / s_skyGrid[0]) - kSkyCells[k] / 2;
    pc.window[3] = (int)s_skyCursor;
    pc.count[0] = (int)s_skyBudget;
    pc.count[1] = s_skyRays;
    pc.count[2] = pc.count[3] = 0;
    pc.size[0] = s_skyGrid[0];
    pc.size[1] = 0.75f * s_skyGrid[0];
    pc.size[2] = s_skyReach;
    pc.size[3] = 0.0f;
    vkCmdPushConstants(cmd, s_skyLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
    vkCmdDispatch(cmd, kSkyCount / 64, 1, 1);
    s_skyCursor = (s_skyCursor + s_skyBudget) % kSkyCount;
}

// the game's blobs under cover (pvkSunCover): the grid's 8 cells round each place asked about this frame, copied back
// with the frame; answered from the frame in flight's copy when it comes round again
static const unsigned kCoverAsks = 128;
static const float kCoverMatch = 3.0f;              // an answer's place to the asker's (m): it moved since
static float s_coverAsk[kCoverAsks][3];             // this frame's
static unsigned s_coverAskCount;
static float s_coverAsked[pvkFrames][kCoverAsks][3];    // copied in each frame in flight
static unsigned s_coverAskedCount[pvkFrames];
static pvkBuffer s_coverReadback[pvkFrames];
static float s_coverKnown[kCoverAsks][4];           // the latest answers: the place, its cover

static bool SkyOn()
{
    return Mode() == 2 && Baking() && s_skyPipeline && s_skyGrid[0] > 0.0f;
}

// the cells round p that the draws' SunSkyOpen reads (n up, from 0.5 cell above the ground)
static void CoverCells(const float* p, int c0[3], float f[3])
{
    for(int k = 0; k < 3; k++)
    {
        float g = (p[k] + (k == 1 ? 0.5f * s_skyGrid[0] : 0.0f)) / s_skyGrid[0] - 0.5f;
        c0[k] = (int)floorf(g);
        f[k] = g - (float)c0[k];
    }
}

static unsigned CoverSlot(int x, int y, int z)
{
    return (unsigned)(((z & 127) * 32 + (y & 31)) * 128 + (x & 127));
}

// this frame's asks, the cells copied after the grid's bake
static void CoverCopy(VkCommandBuffer cmd, unsigned f)
{
    s_coverAskedCount[f] = 0;
    if(!s_coverAskCount)
        return;
    if(!s_coverReadback[f].buffer &&
       !g_pvk->CreateBuffer((VkDeviceSize)kCoverAsks * 64, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, &s_coverReadback[f]))
        return;
    static VkBufferCopy regions[kCoverAsks * 8];
    for(unsigned q = 0; q < s_coverAskCount; q++)
    {
        int c0[3];
        float t[3];
        CoverCells(s_coverAsk[q], c0, t);
        for(int k = 0; k < 8; k++)
        {
            VkBufferCopy& r = regions[q * 8 + k];
            r.srcOffset = kMapBytes + (VkDeviceSize)CoverSlot(c0[0] + (k & 1), c0[1] + ((k >> 1) & 1), c0[2] + (k >> 2)) * 8;
            r.dstOffset = (VkDeviceSize)(q * 8 + k) * 8;
            r.size = 8;
        }
    }
    VkMemoryBarrier2 b = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2 };
    b.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    b.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
    b.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
    b.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
    VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.memoryBarrierCount = 1;
    dep.pMemoryBarriers = &b;
    vkCmdPipelineBarrier2(cmd, &dep);
    vkCmdCopyBuffer(cmd, s_mapBuffer.buffer, s_coverReadback[f].buffer, s_coverAskCount * 8, regions);
    memcpy(s_coverAsked[f], s_coverAsk, s_coverAskCount * sizeof(s_coverAsk[0]));
    s_coverAskedCount[f] = s_coverAskCount;
}

// the frame in flight's copy (its fence waited) as answers, weighed as SunSkyOpen and SunShade do
static void CoverRead(unsigned f)
{
    if(!s_coverAskedCount[f])
        return;
    const float* cells = (const float*)s_coverReadback[f].memory.mapped;
    float size = s_skyGrid[0];
    s_coverKnownCount = s_coverAskedCount[f];
    for(unsigned q = 0; q < s_coverKnownCount; q++)
    {
        const float* p = s_coverAsked[f][q];
        int c0[3];
        float t[3];
        CoverCells(p, c0, t);
        float sum = 0.0f, weight = 0.0f;
        for(int k = 0; k < 8; k++)
        {
            int o[3] = { k & 1, (k >> 1) & 1, k >> 2 };
            int c[3] = { c0[0] + o[0], c0[1] + o[1], c0[2] + o[2] };
            if(((float)c[1] + 0.5f) * size - p[1] <= 0.0f)
                continue;       // under the ground
            const float* cell = cells + (q * 8 + k) * 2;
            if(cell[1] != (float)(1 + (c[0] & 255) + (c[1] & 255) * 256 + (c[2] & 255) * 65536))
                continue;
            float w = (o[0] ? t[0] : 1.0f - t[0]) * (o[1] ? t[1] : 1.0f - t[1]) * (o[2] ? t[2] : 1.0f - t[2]);
            sum += w * cell[0];
            weight += w;
        }
        float cover = 0.0f;     // (not baked: the draws shade as in the open)
        if(weight > 0.0f)
        {
            float x = (sum / weight - s_skyGrid[1]) / (s_skyGrid[2] - s_skyGrid[1]);
            x = x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x;
            cover = 1.0f - x * x * (3.0f - 2.0f * x);   // SunShade's share of the shade, the other way round
        }
        memcpy(s_coverKnown[q], p, sizeof(s_coverAsk[0]));
        s_coverKnown[q][3] = cover;
    }
    s_coverAskedCount[f] = 0;
}

float pvkSunCover(const float* p)
{
    if(!SkyOn())
        return 0.0f;
    if(s_coverAskCount < kCoverAsks)
        memcpy(s_coverAsk[s_coverAskCount++], p, sizeof(s_coverAsk[0]));
    float best = kCoverMatch * kCoverMatch, cover = 0.0f;
    for(unsigned i = 0; i < s_coverKnownCount; i++)
    {
        const float* k = s_coverKnown[i];
        float d = (k[0] - p[0]) * (k[0] - p[0]) + (k[1] - p[1]) * (k[1] - p[1]) + (k[2] - p[2]) * (k[2] - p[2]);
        if(d < best)
        {
            best = d;
            cover = k[3];
        }
    }
    return cover;
}

// a baked caster that should be drawn (near, in view): not drawn for kMissingFrames, it has gone (knocked down)
static bool InView(const Instance& in)
{
    if(!s_viewProjectionSet)
        return false;
    const float* e = s_worldEye;
    float d[3] = { in.centre[0] - e[12], in.centre[1] - e[13], in.centre[2] - e[14] };
    float x = d[0] * e[0] + d[1] * e[1] + d[2] * e[2];
    float y = d[0] * e[4] + d[1] * e[5] + d[2] * e[6];
    float depth = -(d[0] * e[8] + d[1] * e[9] + d[2] * e[10]);     // GL eye space looks down -z
    if(depth < in.radius + 1.0f || depth > kMissingRange)
        return false;
    float px = s_worldProjection[0], py = s_worldProjection[5];
    return (fabsf(x) + in.radius) * px < depth && (fabsf(y) + in.radius) * py < depth;
}

void pvkSunMap(float* windows, float* info)
{
    bool low = Mode() == 1;
    bool on = Baking() && (low ? s_copyPipeline : s_bakePipeline);
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
    info[3] = low ? 1.0f : 0.0f;      // low: the map alone, no rays
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

    int mode = Mode();
    if(mode && mode != s_casterMode)
    {
        // low <-> high: the casters made again as meshes or structures, the map from scratch
        for(unsigned i = 1; i <= s_blasCount; i++)
        {
            ReleaseBlas(s_blas[i]);
            s_blas[i].live = false;
            s_blas[i].builds = 0;   // (made again, not rebuilt: else the world's casters count as animated)
        }
        s_instanceCount = 0;
        RebuildHash();
        memset(s_map, 0, sizeof(s_map));
        s_dirtyTiles = 0;
        s_casterMode = mode;
        s_version++;
    }
    bool low = mode == 1;
    VkAccelerationStructureKHR scene = pvkSunScene();       // none without ray queries
    unsigned now = SDL_GetTicks();
    bool on = mode > 0 && pvkSunPrepare();
    bool baking = on && Baking() && (low ? RasterReady() : scene && BakeReady());
    if(baking && !s_map[0].placed)
        MapReset();
    bool bake = baking && BakeWork();
    unsigned kept = 0, used = 0, worldUsed = 0, current = 0, lowWorld = 0;
    Scene* sc = scene ? &s_scenes[g_pvk->FrameSerial() % pvkFrames] : NULL;
    // low: the scenes stay empty (the programs with ray queries still bind them)
    VkAccelerationStructureInstanceKHR* out = sc && !low ?
        (VkAccelerationStructureInstanceKHR*)sc->draws.instances.memory.mapped : NULL;
    VkAccelerationStructureInstanceKHR* worldOut = baking && out ?
        (VkAccelerationStructureInstanceKHR*)sc->world.instances.memory.mapped : NULL;
    for(unsigned i = 0; i < s_instanceCount; i++)
    {
        Instance& in = s_instances[i];
        Blas& b = s_blas[in.id];
        bool thisFrame = in.lastFrame == s_frame;
        bool alive = on && b.live && (low ? b.mesh.buffer != VK_NULL_HANDLE : b.as && b.address);
        if(!baking)
        {
            in.world = false;
            if(!alive || low || !(thisFrame || (in.keep && in.streak >= 3 && now - in.lastMs < kKeepMs)))
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
        if(!in.world && in.keep && thisFrame && in.streak >= (low ? kLowWorldStreak : kWorldStreak) &&
           b.builds <= 3)
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
        if(out && thisFrame && (!in.world || in.ticket > s_bakedTicket) && used < (unsigned)kMaxInstances)
        {
            FillInstance(out[used++], in);
            current++;
        }
        if(in.world && worldOut && worldUsed < (unsigned)kMaxInstances)
            FillInstance(worldOut[worldUsed++], in);
        else if(in.world && low)
            lowWorld++;         // (the log's; the world's scene stays empty)
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
            if(s_log)
            {
                float sun[3], shade[3];
                pvkSunColours(sun, shade);
                fprintf(stderr, "PDDI sun: %.1f degrees up, light %.2f %.2f %.2f, shade %.2f %.2f %.2f\n",
                        asinf(s_direction[1]) * 180.0f / 3.14159265f, sun[0], sun[1], sun[2], shade[0], shade[1],
                        shade[2]);
            }
            if(baking)
            {
                MapReset();
                bake = false;   // the world's instances were filled for the old map: next frame
            }
        }
    }
    s_bestLight = 0.0f;

    CoverRead((unsigned)(g_pvk->FrameSerial() % pvkFrames));
    if(!SkyOn())
        s_coverKnownCount = 0;
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
        bool sky = baking && !low && sc->world.as && SkyReady();
        if((bake && !low) || sky)
        {
            // the world's scene built; the map's texels free of the draws of frames before (still running); the sky
            // grid's earlier bakes written (it reads their keys)
            b.srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
                             VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            b.srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            b.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
            vkCmdPipelineBarrier2(cmd, &dep);
            if(bake && !low)
                Bake(cmd, sc->world.as);
            if(sky)
            {
                SkyBake(cmd, sc->world.as);
                CoverCopy(cmd, (unsigned)(g_pvk->FrameSerial() % pvkFrames));
            }
        }
        // (pvkCore::Present's barrier after the uploads makes them visible to the draws)
    }
    if(bake && low)
        Bake(g_pvk->UploadCmd(), VK_NULL_HANDLE);
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
                    "%u structures, %u built, %u draws not in the view, %u texels baked (%u casters drawn)\n", on ? "on" : "off",
                    s_direction[0], s_direction[1], s_direction[2], used, current, worldUsed + lowWorld,
                    s_blasCount - s_freeCount, s_builds, s_notInView, s_bakedTexels, s_rasterDraws);
            s_notInView = 0;
            s_lastLog = now;
            s_builds = 0;
            s_bakedTexels = 0;
            s_rasterDraws = 0;
        }
    }
    s_buildsThisFrame = 0;
    s_coverAskCount = 0;
    s_frame++;
}

void pvkSunShutdown()
{
    if(!g_pvk || !g_pvk->device)
        return;
    for(unsigned i = 1; i <= s_blasCount; i++)
    {
        FreeSkin(s_blas[i], true);
        if(s_blas[i].as)
            vkDestroyAccelerationStructureKHR(g_pvk->device, s_blas[i].as, NULL);
        g_pvk->DestroyNow(s_blas[i].storage);
        g_pvk->DestroyNow(s_blas[i].mesh);
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
    for(int f = 0; f < pvkFrames; f++)
    {
        g_pvk->DestroyNow(s_coverReadback[f]);
        s_coverAskedCount[f] = 0;
    }
    s_coverKnownCount = 0;
    if(s_bakePipeline)
        vkDestroyPipeline(g_pvk->device, s_bakePipeline, NULL);
    if(s_bakeLayout)
        vkDestroyPipelineLayout(g_pvk->device, s_bakeLayout, NULL);
    if(s_bakeSetLayout)
        vkDestroyDescriptorSetLayout(g_pvk->device, s_bakeSetLayout, NULL);
    if(s_skyPipeline)
        vkDestroyPipeline(g_pvk->device, s_skyPipeline, NULL);
    if(s_skyLayout)
        vkDestroyPipelineLayout(g_pvk->device, s_skyLayout, NULL);
    s_skyPipeline = VK_NULL_HANDLE;
    s_skyLayout = VK_NULL_HANDLE;
    s_skyFailed = false;
    s_skyCleared = false;
    s_bakePipeline = VK_NULL_HANDLE;
    s_bakeLayout = VK_NULL_HANDLE;
    s_bakeSetLayout = VK_NULL_HANDLE;
    s_bakeFailed = false;
    DestroyRaster();
    s_casterMode = 0;
    memset(s_map, 0, sizeof(s_map));
    s_dirtyTiles = 0;
    s_instanceCount = 0;
    s_hashSize = 0;
    free(s_hash);
    s_hash = NULL;
}
