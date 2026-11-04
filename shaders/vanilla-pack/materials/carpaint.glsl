// Car paint: the game's paint and reflection, plus a sharp per-pixel highlight from each light and a stronger
// reflection at grazing angles (Fresnel). For spheremap / environment materials (car bodies, chrome).

const float HIGHLIGHT = 0.6;            // highlight strength
const float GLOSS = 60.0;               // highlight sharpness
const float FRESNEL = 0.5;              // extra reflection at grazing angles

vec4 ShadeMaterial()
{
    vec4 c = StandardColour();
    vec3 n = normalize(vNormal);
    vec3 toEye = -normalize(vEyePosition);

    float grazing = pow(1.0 - max(dot(n, toEye), 0.0), 5.0);
    if(uReflectionTextured)
        c.rgb += FRESNEL * grazing * texture(uReflection, vReflectionUV).rgb * uEnvBlend.rgb * 2.0;

    for(int i = 0; i < uLightCount; i++)
    {
        float attenuation;
        vec3 l = LightVector(i, attenuation);
        float highlight = pow(max(dot(n, normalize(l + toEye)), 0.0), GLOSS);
        c.rgb += HIGHLIGHT * highlight * attenuation * uLightColour[i];
    }
    return vec4(min(c.rgb, 1.0), c.a);
}
