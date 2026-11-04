// Per-pixel lighting: the game's own lighting (its lights, its material colours, Direct3D 8's equations), worked out
// for every pixel instead of only at the corners of each polygon. Low-polygon characters and cars lose their blotchy,
// faceted shading, and point lights and highlights fall smoothly across surfaces. Unlit materials (most of the
// world, whose light is painted into their vertex colours) are drawn as the game draws them.
//
// For everything lit, in materials.ini:
//     [types]
//     simple = materials/pixellit.glsl
//     spheremap = materials/pixellit.glsl
//     environment = materials/pixellit.glsl
//
// With every setting below at its default the result is the game's lighting, only smoother. The extras go beyond it.

const float HIGHLIGHT = 0.0;            // extra: a sheen on every lit material (0 = only where the art asks for one)
const float HIGHLIGHT_GLOSS = 24.0;     // ... its sharpness
const float RIM = 0.0;                  // extra: light around the edges of lit objects (0 = off, 0.2 = subtle)
const float RIM_WIDTH = 3.0;            // ... how thin the rim is (higher = thinner)

vec4 ShadeMaterial()
{
    if(!uLit)
        return StandardColour();

    vec3 n = normalize(vNormal);
    vec3 toEye = -normalize(vEyePosition);

    // Direct3D 8's lighting (as the game's vertices get it), per pixel
    vec3 specular;
    vec3 colour = Lighting(n, 0.0, specular);
    if(HIGHLIGHT > 0.0)
    {
        for(int i = 0; i < uLightCount; i++)
        {
            float attenuation;
            vec3 l = LightVector(i, attenuation);
            if(dot(n, l) > 0.0)
                specular += attenuation * HIGHLIGHT * pow(max(dot(n, normalize(l + toEye)), 0.0), HIGHLIGHT_GLOSS) * uLightColour[i];
        }
    }
    if(RIM > 0.0)
        colour += RIM * pow(1.0 - max(dot(n, toEye), 0.0), RIM_WIDTH) * (uGlobalAmbient.rgb + uMatEmissive.rgb + 0.5);

    vec4 c = vec4(min(colour, 1.0), uMatDiffuse.a) * BaseTexture();

    // the reflection map of car paint and chrome, as the game adds it
    if(uReflectionTextured || uEnvBlend.a > 0.0)
    {
        vec4 e = uEnvBlend;
        if(uReflectionTextured)
            e *= texture(uReflection, vReflectionUV);
        c = min(c + e, 1.0);
    }

    c.rgb = min(c.rgb + min(specular, 1.0), 1.0);
    return c;
}
