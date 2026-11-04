// Flat colours, like the cartoon: each part is one even colour (its texture colour) instead of being shaded light to
// dark across the model. Meant for the characters ([files] *chars/* in materials.ini).
//
// By default the colours still follow the light around them as a whole (dimmer at night and indoors, tinted by
// coloured lights), so characters do not glow in the dark. Set SCENE_LIGHT to 0.0 for full colour everywhere.
// FLATNESS blends between these flat colours and the game's own smooth shading.

const float FLATNESS = 1.0;             // 1 = flat colours, 0 = the game's smooth shading, in between = a mix
const float BRIGHTNESS = 1.0;           // overall brightness (of the flat colours)
const float SCENE_LIGHT = 1.0;          // 1 = follow the scene's light level, 0 = ignore it (always full colour)
const float LIGHT_SHARE = 0.75;         // how much of each light counts (as if it hit every part at the same angle)

vec4 ShadeMaterial()
{
    vec4 base = BaseTexture();
    vec3 light = vec3(1.0);

    if(uLit)
    {
        // one light level for the whole surface: the ambient and emissive parts, plus each light as though it fell
        // on every point the same way (no direction, so no shading across the model)
        vec3 level = uMatEmissive.rgb + uMatAmbient.rgb * uGlobalAmbient.rgb;
        for(int i = 0; i < uLightCount; i++)
        {
            float attenuation;
            LightVector(i, attenuation);
            level += LIGHT_SHARE * attenuation * uMatDiffuse.rgb * uLightColour[i];
        }
        light = mix(vec3(1.0), min(level, vec3(1.0)), SCENE_LIGHT);
        base.a *= uMatDiffuse.a;        // fading characters fade through the material's alpha
    }
    else
    {
        base *= vColour;                // unlit parts keep their vertex colours (flat already)
    }

    vec3 flatColour = min(base.rgb * light * BRIGHTNESS, 1.0);
    vec3 smoothColour = StandardColour().rgb;    // as the game shades it: light to dark across the model
    return vec4(mix(smoothColour, flatColour, clamp(FLATNESS, 0.0, 1.0)), base.a);
}
