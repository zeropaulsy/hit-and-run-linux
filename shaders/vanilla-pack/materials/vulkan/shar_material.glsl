// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The game's side of a Vulkan (SPIR-V) material shader: #include "shar_material.glsl" after #version 450, then define
// vec4 ShadeMaterial(). Everything the GLSL material shaders get (shaders/README.md, Material shaders) is here, with
// the same names and meaning; main() below applies fog and the alpha test to what ShadeMaterial() returns, as the
// game does for GLSL ones. The game fits the compiled file to its own program by name (the locations, offsets and
// bindings below are placeholders it replaces), so keep the names and leave the SPIR-V unstripped.
// Same code as the GLSL prelude (libs/pure3d/pddi/gl/glshaders.cpp, pglFragmentPrelude): keep the two in step.

layout(location = 0) in vec4 vColour;           // the lit colour (unlit materials: their vertex colours)
layout(location = 1) in vec3 vSpecular;         // the game's highlight
layout(location = 2) in vec2 vUV;               // texture coordinates
layout(location = 3) in vec2 vReflectionUV;     // reflection map coordinates (car paint)
layout(location = 4) in vec3 vNormal;           // eye space (x right, y up, z towards the camera)
layout(location = 5) in vec3 vEyePosition;      // eye space (the camera at 0,0,0)
layout(location = 6) in float vFogDistance;
layout(location = 0) out vec4 fragColour;

layout(set = 0, binding = 0, std140) uniform Material
{
    bool uLit;                                  // the material is lit by the game's lights
    vec4 uMatAmbient;
    vec4 uMatDiffuse;
    vec4 uMatSpecular;
    vec4 uMatEmissive;
    float uShininess;
    vec4 uGlobalAmbient;
    int uLightCount;                            // the lights in use
    vec4 uLightPosition[8];
    vec3 uLightColour[8];
    vec3 uLightAtten[8];
    float uLightRange[8];
    float uTime;                                // seconds since the game started
    vec2 uResolution;
    bool uTextured;
    bool uReflectionTextured;
    vec4 uEnvBlend;
    bool uFog;
    vec3 uFogColour;
    float uFogEnd;
    float uFogScale;
    int uAlphaCompare;
    float uAlphaRef;
    bool uAlphaToCoverage;
    float uMipAlpha;
};
layout(set = 0, binding = 1) uniform sampler2D uTexture;
layout(set = 0, binding = 2) uniform sampler2D uReflection;

vec4 ShadeMaterial();

// the base texture; with mipmapping on (uMipAlpha: the anisotropy), an alpha-tested texture's alpha is raised with the
// mip level sampled, so averaged-down levels keep the coverage
vec4 SampleBase()
{
    vec4 t = texture(uTexture, vUV);
    if(uMipAlpha > 0.0 && (uAlphaCompare == 4 || uAlphaCompare == 5))
    {
        vec2 size = vec2(textureSize(uTexture, 0));
        vec2 dx = dFdx(vUV * size);
        vec2 dy = dFdy(vUV * size);
        float major = max(dot(dx, dx), dot(dy, dy));
        float minor = max(min(dot(dx, dx), dot(dy, dy)), 1e-8);
        float lod = 0.5 * log2(max(major, 1e-8)) - log2(min(sqrt(major / minor), uMipAlpha));
        t.a *= 1.0 + max(lod, 0.0) * 0.25;
    }
    return t;
}

// the material as the game draws it (lit colour x texture + reflection + highlight), before fog
vec4 StandardColour()
{
    vec4 c = vColour;
    if(uTextured)
        c *= SampleBase();
    vec4 e = uEnvBlend;
    if(uReflectionTextured)
        e *= texture(uReflection, vReflectionUV);
    c = min(c + e, 1.0);
    c.rgb = min(c.rgb + vSpecular, 1.0);
    return c;
}

// the base texture (white without one)
vec4 BaseTexture()
{
    return uTextured ? SampleBase() : vec4(1.0);
}

// eye-space direction from this pixel towards light i (< uLightCount), and its attenuation (0 out of range)
vec3 LightVector(int i, out float attenuation)
{
    attenuation = 1.0;
    if(uLightPosition[i].w == 0.0)
        return normalize(uLightPosition[i].xyz);
    vec3 d = uLightPosition[i].xyz / uLightPosition[i].w - vEyePosition;
    float distance = length(d);
    attenuation = distance > uLightRange[i] ? 0.0 :
        1.0 / (uLightAtten[i].x + uLightAtten[i].y * distance + uLightAtten[i].z * distance * distance);
    return d / max(distance, 1e-6);
}

// the game's lighting at this pixel for normal n (eye space, normalised): returns the light colour (multiply by
// BaseTexture()), the game's highlights in specular; wrap > 0 softens the fall-off ((n.l + wrap) / (1 + wrap))
vec3 Lighting(vec3 n, float wrap, out vec3 specular)
{
    vec3 toEye = -normalize(vEyePosition);
    vec3 colour = uMatEmissive.rgb + uMatAmbient.rgb * uGlobalAmbient.rgb;
    specular = vec3(0.0);
    for(int i = 0; i < uLightCount; i++)
    {
        float attenuation;
        vec3 l = LightVector(i, attenuation);
        float nl = dot(n, l);
        float diffuse = max((nl + wrap) / (1.0 + wrap), 0.0);
        colour += attenuation * diffuse * uMatDiffuse.rgb * uLightColour[i];
        if(nl > 0.0 && uShininess > 0.0)
            specular += attenuation * pow(max(dot(n, normalize(l + toEye)), 0.0), uShininess) * uMatSpecular.rgb * uLightColour[i];
    }
    return colour;
}

// fog, then the alpha test (or alpha-to-coverage with MSAA)
void main()
{
    vec4 c = ShadeMaterial();
    if(uFog)
        c.rgb = mix(uFogColour, c.rgb, clamp((uFogEnd - vFogDistance) * uFogScale, 0.0, 1.0));
    float alphaWidth = max(fwidth(c.a), 1e-4);
    if(uAlphaToCoverage)
    {
        c.a = clamp((c.a - uAlphaRef) / alphaWidth + 0.5, 0.0, 1.0);
        if(c.a <= 0.0)
            discard;
        fragColour = c;
        return;
    }
    bool pass = true;
    if(uAlphaCompare == 0) pass = false;
    else if(uAlphaCompare == 2) pass = c.a < uAlphaRef;
    else if(uAlphaCompare == 3) pass = c.a <= uAlphaRef;
    else if(uAlphaCompare == 4) pass = c.a > uAlphaRef;
    else if(uAlphaCompare == 5) pass = c.a >= uAlphaRef;
    else if(uAlphaCompare == 6) pass = c.a == uAlphaRef;
    else if(uAlphaCompare == 7) pass = c.a != uAlphaRef;
    if(!pass)
        discard;
    fragColour = c;
}
