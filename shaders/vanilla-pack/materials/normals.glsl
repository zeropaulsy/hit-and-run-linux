// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Debug view: each surface coloured by its normal (eye space: red = right, green = up, blue = towards the camera).

vec4 ShadeMaterial()
{
    vec3 n = normalize(vNormal);
    return vec4(n * 0.5 + 0.5, StandardColour().a);
}
