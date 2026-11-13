// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

//=============================================================================
// Linux port: user post-processing shaders (OpenGL 3.3 core renderer).
//=============================================================================

// GLSL fragment shaders from the shaders folder (shaders/ in the working directory, or $SHAR_SHADERS), listed in
// shaders/post.ini, run over the frame in two stages: "scene" passes before the GUI layer (HUD, menus) is drawn,
// "final" passes over the whole frame before it is shown. Files reload when they change. Format: shaders/README.md.

#ifndef _GLPOST_HPP_
#define _GLPOST_HPP_

#include <pddi/gl/display_linux/gl.hpp>

class pglPipeline;

enum pglPostStage
{
    PGL_POST_SCENE,
    PGL_POST_FINAL,
    PGL_POST_STAGES
};

// GL state a screen pass changes, saved before and restored after it (the pddi caches its own state: textures on
// units 0-2, blending, culling, ... must be as it left them)
struct pglScreenPassState
{
    void Save();
    void Restore();

    GLboolean enabled[5];
    GLboolean colourMask[4];
    GLint viewport[4], polygonMode[2], textures[3], activeTexture, drawFramebuffer, readFramebuffer;
};

// runs a stage's passes over the back buffer (w x h); the render thread, between frames' draws
void pglPostRun(pglPostStage stage, int w, int h, pglPipeline* pipeline);

// for the game's render loop (RenderManager): the scene stage, just before the GUI layer is drawn
void pglPostProcessScene();

#endif
