// Cel shading: lighting in a few flat bands with per-pixel lighting from the game's lights. Unlit materials (most of
// the world, whose lighting is baked into vertex colours) get their colours banded too.

const float BANDS = 3.0;                // number of light levels
const float BAND_SOFTNESS = 0.05;       // blur between bands (0 = hard)

float Band(float x)
{
    float scaled = x * BANDS;
    float f = fract(scaled);
    return (floor(scaled) + smoothstep(0.5 - BAND_SOFTNESS, 0.5 + BAND_SOFTNESS, f)) / BANDS;
}

vec4 ShadeMaterial()
{
    vec4 base = BaseTexture();
    if(!uLit)
    {
        vec4 c = vColour * base;
        float luma = max(dot(c.rgb, vec3(0.299, 0.587, 0.114)), 1e-4);
        c.rgb *= Band(luma) / luma;
        return vec4(min(c.rgb, 1.0), c.a);
    }

    vec3 n = normalize(vNormal);
    vec3 light = uMatEmissive.rgb + uMatAmbient.rgb * uGlobalAmbient.rgb;
    for(int i = 0; i < uLightCount; i++)
    {
        float attenuation;
        vec3 l = LightVector(i, attenuation);
        light += Band(max(dot(n, l), 0.0) * attenuation) * uMatDiffuse.rgb * uLightColour[i];
    }
    return vec4(min(light, 1.0) * base.rgb, uMatDiffuse.a * base.a);
}
