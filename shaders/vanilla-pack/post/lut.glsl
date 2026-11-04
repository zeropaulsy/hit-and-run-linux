// Colour grading with a lookup table (LUT): any grade made in an image editor, applied exactly. The table is an image
// of 32 x 32 x 32 colours laid out as a 1024 x 32 strip (the layout ReShade uses; lut/neutral.png changes nothing).
//
// To make one: take a screenshot of the game, paste lut/neutral.png onto it (anywhere), grade the whole image in
// GIMP, Photoshop, Krita or Resolve with colour adjustments only (curves, levels, hue, colour balance, ...; nothing
// that blurs or moves pixels), cut the strip back out at exactly 1024 x 32 and save it as a PNG in lut/. Then point
// the line below at it. Examples: lut/warm.png, lut/teal_orange.png, lut/cool.png.

#pragma shar_texture uLut lut/warm.png

const float AMOUNT = 1.0;               // 0 = off, 1 = the full grade
const float SIZE = 32.0;                // colours per channel in the table

vec3 Lookup(vec3 c)
{
    // blue picks the tile, red and green the texel in it; the two nearest blue tiles are blended
    c = clamp(c, 0.0, 1.0);
    float blue = c.b * (SIZE - 1.0);
    float tile0 = floor(blue);
    float tile1 = min(tile0 + 1.0, SIZE - 1.0);
    vec2 texel = vec2(1.0 / (SIZE * SIZE), 1.0 / SIZE);
    vec2 inTile = vec2((c.r * (SIZE - 1.0) + 0.5) * texel.x, (c.g * (SIZE - 1.0) + 0.5) * texel.y);
    vec3 a = texture(uLut, inTile + vec2(tile0 * SIZE * texel.x, 0.0)).rgb;
    vec3 b = texture(uLut, inTile + vec2(tile1 * SIZE * texel.x, 0.0)).rgb;
    return mix(a, b, blue - tile0);
}

void main()
{
    vec3 c = texture(uScene, vUV).rgb;
    fragColour = vec4(mix(c, Lookup(c), AMOUNT), 1.0);
}
