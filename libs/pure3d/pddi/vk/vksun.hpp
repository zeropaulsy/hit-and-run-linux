//=============================================================================
// Linux port: sun lighting with ray-traced shadows (prototype; Vulkan renderer only, SHAR_SUN=1, F9 toggles)
//=============================================================================

// The world is pre-lit (vertex colours, no normals) and the game only turns its lights on around what it lights, so
// the pddi works out the sun itself: the strongest directional light it has seen, in world space (the camera comes
// from p3d's tContext::LoadViewMatrix). Every opaque prim buffer drawn becomes a bottom-level acceleration structure
// (from its CPU copy) and each draw an instance of it in the frame's top-level one; static instances (the same
// transform two frames running) stay in it for a while after they leave the view, so off-screen buildings still cast.
// Fragments of perspective, depth-writing, unblended draws trace one ray towards the sun (glshaders.cpp, PVK_SUN):
// pre-lit draws are scaled between a shadow and a sunlit level by n.l and the ray, lit ones (cars, characters) only
// darkened when the ray is blocked. Normals: smooth ones made from the positions (vkcon.cpp), the polygon's own at
// creases.

#ifndef _VKSUN_HPP_
#define _VKSUN_HPP_

#include <pddi/vk/vkcore.hpp>

bool pvkSunWanted();            // SHAR_SUN set: the device is made with ray queries
bool pvkSunReady();             // ray queries on the device: programs are built with the sun's code
bool pvkSunOn();                // shading with it now (F9)
void pvkSunToggle();

// p3d's camera (row vectors, translation in row 3)
void pvkSetViewMatrix(const float* cameraToWorld);
// GL eye space -> world (row vectors), and a counter that changes with it or with the sun's settings
const float* pvkSunEyeToWorld();
unsigned pvkSunVersion();
// towards the sun, world space; levels: shadow, sunlit (pre-lit draws), shadow (lit draws)
void pvkSunParameters(float* direction, float* levels);

// a directional light the pipeline set (eye space, towards the light), for picking the sun
void pvkSunNoteLight(const float* eyeDirection, const float* colour);

// a draw that casts: its prim buffer's positions and triangles (strip: a triangle strip), the draw's GL model-view
// matrix. *id is the prim buffer's (0 until it has one); version changes when its contents do.
void pvkSunAddCaster(unsigned* id, unsigned version, const float* positions, unsigned vertexCount,
                     const unsigned short* indices, unsigned count, bool strip, const float* modelView);
void pvkSunForget(unsigned id);     // the prim buffer is gone (any thread)

// the scene the frame's draws trace (built by pvkSunEndFrame, before them)
VkAccelerationStructureKHR pvkSunScene();
void pvkSunEndFrame();
void pvkSunShutdown();

// smooth normals for a prim buffer without them: per vertex, the area-weighted sum of the faces at its position
void pvkSunSmoothNormals(const float* positions, unsigned vertexCount, const unsigned short* indices, unsigned count,
                         bool strip, float* normals);

#endif
