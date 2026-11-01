// Soft lighting for low-polygon models (characters): per-pixel lighting that hides the polygons. With few polygons
// the lighting changes in steps from face to face (blotchy shading); this
//   - wraps the light further round the model (WRAP), so the fall-off from light to dark is wide and gentle instead
//     of a hard edge that shows every facet, and
//   - bends the normals partly towards the camera (SMOOTHING), so neighbouring faces differ less, while the overall
//     direction of the light still reads.
// Optionally a rim light (RIM) brightens the model's outline against the background, in the colour of the scene's
// light (dim at night, warm under warm lights).
// For the characters, in materials.ini:
//     [files]
//     *chars/* = materials/softlit.glsl
//     (or *chars/* = materials/softlit.glsl, materials/outline.glsl)

const float WRAP = 0.6;                 // 0 = the game's fall-off, 1 = light reaches all the way round
const float SMOOTHING = 0.35;           // 0 = the model's normals, 1 = every point lit as if facing the camera
const float BRIGHTNESS = 0.8;          // wrapped light is brighter overall; this pulls it back (0.8: as bright as the game)
const float RIM = 0.2;                  // rim light strength (0 = off, 0.3 = noticeable, 0.6 = strong)
const float RIM_WIDTH = 6.0;            // how thin the rim is (higher = thinner, only the very edge)

vec4 ShadeMaterial()
{
    if(!uLit)
        return StandardColour();

    vec3 toEye = -normalize(vEyePosition);
    vec3 n = normalize(mix(normalize(vNormal), toEye, SMOOTHING));

    vec3 specular;
    vec3 colour = Lighting(n, WRAP, specular) * BRIGHTNESS;

    if(RIM > 0.0)
    {
        // where the model's own surface turns away from the camera (its normals: the smoothed ones have no edge),
        // lit by the light around it
        float edge = pow(1.0 - max(dot(normalize(vNormal), toEye), 0.0), RIM_WIDTH);
        vec3 sceneLight = uGlobalAmbient.rgb;
        for(int i = 0; i < uLightCount; i++)
        {
            float attenuation;
            LightVector(i, attenuation);
            sceneLight += attenuation * uLightColour[i];
        }
        colour += RIM * edge * min(sceneLight, 1.0);
    }

    vec4 c = vec4(min(colour, 1.0), uMatDiffuse.a) * BaseTexture();
    c.rgb = min(c.rgb + min(specular, 1.0), 1.0);
    return c;
}
