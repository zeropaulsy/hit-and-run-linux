// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Draws a material exactly as the game does. Useful as the first pass when a later one adds something (an outline).

vec4 ShadeMaterial()
{
    return StandardColour();
}
