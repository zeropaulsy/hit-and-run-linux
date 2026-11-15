// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

// A post pass written for Vulkan: standard Vulkan GLSL with its own declarations (no header is put in front of it),
// compiled ahead of time to SPIR-V (test.spv, the file post.ini names): python3 port/spirv.py
// shaders/post/vulkan/test.frag. Vulkan renderer only; the OpenGL renderer skips .spv passes.
// Effect: colour fringes towards the screen's edges (chromatic aberration) and a cool haze over distant things.
#version 450

#pragma shar_scale 1.0      // pragmas go in this source, beside the .spv (read from here, not from the SPIR-V)

const float FRINGE = 0.004;             // colour split at the corners (fraction of the screen)
const float HAZE_START = 60.0;          // metres
const float HAZE_END = 400.0;
const vec3 HAZE_COLOUR = vec3(0.62, 0.72, 0.85);
const float HAZE_AMOUNT = 0.35;

layout(location = 0) in vec2 vUV;       // (0,0) bottom left, (1,1) top right
layout(location = 0) out vec4 fragColour;

layout(set = 0, binding = 0, std140) uniform Post
{
    vec2 uResolution;
    float uTime;
    int uFrame;
    float uNear;
    float uFar;
    mat4 uProjection;
    mat4 uInverseProjection;
};
layout(set = 0, binding = 1) uniform sampler2D uScene;
layout(set = 0, binding = 2) uniform sampler2D uOriginal;
layout(set = 0, binding = 3) uniform sampler2D uDepth;

float LinearDepth(vec2 uv)
{
    float d = texture(uDepth, uv).r;
    if(uFar <= 0.0)
        return 0.0;
    float z = d * 2.0 - 1.0;
    return 2.0 * uNear * uFar / (uFar + uNear - z * (uFar - uNear));
}

void main()
{
    vec2 fromCentre = vUV - 0.5;
    vec2 shift = fromCentre * dot(fromCentre, fromCentre) * 4.0 * FRINGE;
    vec3 c;
    c.r = texture(uScene, vUV + shift).r;
    c.g = texture(uScene, vUV).g;
    c.b = texture(uScene, vUV - shift).b;

    float haze = smoothstep(HAZE_START, HAZE_END, LinearDepth(vUV)) * HAZE_AMOUNT;
    fragColour = vec4(mix(c, HAZE_COLOUR, haze), 1.0);
}
