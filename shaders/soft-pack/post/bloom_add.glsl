// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Bloom, pass 4 of 4: adds the blurred glow (uScene) to the picture as it was before the bloom (uOriginal: the input
// of bloom_bright.glsl, which starts the effect).

const float STRENGTH = 0.4;

void main()
{
    vec3 original = texture(uOriginal, vUV).rgb;
    vec3 glow = texture(uScene, vUV).rgb;
    fragColour = vec4(original + glow * STRENGTH * (1.0 - original), 1.0);     // screen-like add, no clipping
}
