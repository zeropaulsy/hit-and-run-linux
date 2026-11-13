// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

//=============================================================================
// Linux port: sun lighting with ray-traced shadows (prototype; Vulkan renderer only, SHAR_SUN=1, F9 toggles)
//=============================================================================

// The world is pre-lit (vertex colours, no normals) and the game only turns its lights on around what it lights, so
// the pddi works out the sun itself: the strongest directional light it has seen, in world space (the camera comes
// from p3d's tContext::LoadViewMatrix). Every opaque prim buffer drawn becomes a bottom-level acceleration structure
// (from its CPU copy) and each draw an instance of it in the frame's top-level one; static pre-lit instances (the
// world: the same transform three frames running) stay in it for a while after they leave the view, so off-screen
// buildings still cast; lit ones (cars, characters) only cast where they are drawn. Alpha-tested casters (foliage,
// fences) are not opaque to the ray: it reads their UVs and triangles (the geometry buffer, offsets in the caster
// table) and a 64x64 copy of their texture's alpha (the masks) and goes through where the alpha test would discard.
// Baked (default): the static world's shadow is baked into a map round the camera as seen from the sun (pvkSunMap),
// and the draws' rays only meet what it does not hold (cars, characters, moving things).
// Low (Options > Graphics Shadows Low; the only kind without ray queries): no rays at all. The map is rasterised
// from the static world's triangles (an orthographic depth pass per chunk of texels, vksun.cpp RasterRows) and the
// draws read only the map; cars and characters keep the game's blob shadows.
// Fragments of perspective, depth-writing, unblended draws trace one ray towards the sun (glshaders.cpp, PVK_SUN):
// pre-lit draws are scaled between a shadow and a sunlit level by n.l and the ray, lit ones (cars, characters) only
// darkened when the ray is blocked. Normals: smooth ones made from the positions (vkcon.cpp), the polygon's own at
// creases.

#ifndef _VKSUN_HPP_
#define _VKSUN_HPP_

#include <pddi/vk/vkcore.hpp>

class pvkTexture;

bool pvkSunWanted();            // not SHAR_SUN=0: programs are built with the sun's code
bool pvkSunWantRays();          // ... and the device is made with ray queries where it has them (not SHAR_SUN_RT=0)
bool pvkSunReady();             // wanted, on a Vulkan device: programs are built with the sun's code
bool pvkSunRays();              // ray queries on the device: the programs' rays (PVK_SUN_RAYS) and scenes
bool pvkSunPrepare();           // the buffers (and scenes) made; false when they cannot be
bool pvkSunOn();                // shading with it now (the setting, low or high)
bool pvkSunRaysOn();            // high: ray traced, lit things (cars, characters) cast too
void pvkSunToggle();            // F9: off, low, high
// the setting (Options > Graphics Shadows, simpsons.ini shadows=; off by default): 0 off, 1 low, 2 high (low on a
// device without ray queries)
void pvkSunSetQuality(int quality);
int pvkSunQuality();
void pvkSunSetSuppressed(bool suppressed);  // the game's, each frame: off whatever the setting (interiors, front end)

// p3d's camera (row vectors, translation in row 3)
void pvkSetViewMatrix(const float* cameraToWorld);
// the pipeline's projection, read when the camera is set: draws with another projection (the HUD's 3D radar, drawn
// with the world's camera matrix still loaded) are not in the world (pvkSunInView)
void pvkSunSetProjectionSource(const float* projection);
bool pvkSunInView(const float* projection);
// GL eye space -> world (row vectors), and a counter that changes with it or with the sun's settings
const float* pvkSunEyeToWorld();
unsigned pvkSunVersion();
// towards the sun, world space; levels: shadow, sunlit (pre-lit draws), shadow (lit draws); soft: tangent of the
// sun disc's radius (penumbra), rays per pixel; range: full rays to (one at twice it), no shadow past (0: everywhere),
// ray length, metres
void pvkSunParameters(float* direction, float* levels, float* soft, float* range);
// the sunlight's and the shade's tints (luminance 1) for the sun's height and the level
void pvkSunColours(float* sun, float* shade);

// the level being played (RenderEnums::LevelEnum: 0 = level 1), for its sun's height
void pvkSunSetLevel(int level);

// a directional light the pipeline set (eye space, towards the light), for picking the sun
void pvkSunNoteLight(const float* eyeDirection, const float* colour);

// a draw that casts: its prim buffer's positions, UVs (may be NULL) and triangles (strip: a triangle strip), the
// draw's GL model-view matrix; keep: may stay in the scene out of view (static world). alphaTexture: alpha-tested
// with that texture (pddiCompareMode alphaCompare, alphaRef), else NULL. *id is the prim buffer's (0 until it has
// one); version changes when its contents do.
void pvkSunAddCaster(unsigned* id, unsigned version, const float* positions, const float* uvs, unsigned vertexCount,
                     const unsigned short* indices, unsigned count, bool strip, const float* modelView, bool keep,
                     pvkTexture* alphaTexture, int alphaCompare, float alphaRef);
void pvkSunForget(unsigned id);     // the prim buffer is gone (any thread)
// the material of the prim buffer about to be drawn (SHAR_SUN_LOG=2 names each caster as it is built)
void pvkSunDrawing(const char* material, const char* file);
void pvkSunForgetTexture(pvkTexture* texture);  // any thread

// the alpha-tested casters' table, the masks and their geometry, the baked map (storage buffers for the programs,
// bindings 6-9)
bool pvkSunBuffers(VkDescriptorBufferInfo* buffers);
// the baked map (SHAR_SUN_BAKE, on by default): the static world's shadow, as heights towards the sun of the first
// surface on each texel of planes facing it round the camera (vksun.cpp). windows: per level, the texels baked (x0,
// y0, x1, y1, ends past); info: level 0's texel (m), texels across a level, 1 when the map is used (0: every shadow
// ray traced per pixel through the whole scene), 1 when it is all there is (low: no rays)
void pvkSunMap(float* windows, float* info);
// the sky grid (high, with the map; SHAR_SUN_SKY): x its cell (m; 0 none), shadow from none where a surface sees y of
// the sky to full at z; w: debug view (the openness)
void pvkSunSky(float* sky);
// the game's blob shadows under cover (high): how covered the ground at p is (the sky grid's, as the draws' shade
// takes it: 0 open, 1 no sun shade), a few frames late; 0 without the grid or until known
float pvkSunCover(const float* p);

// the scene the frame's draws trace (built by pvkSunEndFrame, before them): with the map, only what is not in it
// (cars, characters, moving things, static ones not baked yet). None without ray queries; empty in low.
VkAccelerationStructureKHR pvkSunScene();
// with the map, the static world (traced where the map cannot draw an edge); else the draws' scene
VkAccelerationStructureKHR pvkSunWorldScene();
void pvkSunEndFrame();
void pvkSunShutdown();

// smooth normals for a prim buffer without them: per vertex, the area-weighted sum of the faces at its position
// that are within a crease angle of its own
void pvkSunSmoothNormals(const float* positions, unsigned vertexCount, const unsigned short* indices, unsigned count,
                         bool strip, float* normals);

#endif
