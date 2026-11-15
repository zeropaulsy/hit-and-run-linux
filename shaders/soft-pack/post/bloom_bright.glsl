// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Bloom, pass 1 of 4: keeps only the bright parts of the picture.
// Use with bloom_blur_h.glsl, bloom_blur_v.glsl and bloom_add.glsl, in that order.

#pragma shar_original                  // the effect starts here: bloom_add adds the glow to this pass's input

const float THRESHOLD = 0.7;            // brightness above which things glow

void main()
{
    vec3 c = texture(uScene, vUV).rgb;
    float brightness = max(c.r, max(c.g, c.b));
    fragColour = vec4(c * smoothstep(THRESHOLD, 1.0, brightness), 1.0);
}
