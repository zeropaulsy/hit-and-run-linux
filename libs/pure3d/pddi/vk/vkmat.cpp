//=============================================================================
// Linux port: Vulkan renderer materials (see vkmat.hpp): glmat.cpp's pddi logic, with the GL state it set going to
// pvkPipeline. Keep the two in step.
//=============================================================================

#include <pddi/vk/vkmat.hpp>
#include <pddi/vk/vktex.hpp>
#include <pddi/vk/vkcon.hpp>
#include <pddi/gl/glcustom.hpp>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// Linux port: the parameters the D3D8 pddi's shaders take (dx8/shaders/shader.hpp PDDID3D_STDSHADERPARAM_*)
#define PVK_STDSHADERPARAM_COLOUR \
    {PDDI_SP_AMBIENT  , SHADE_COLOUR(&pvkMat::SetAmbient)},\
    {PDDI_SP_DIFFUSE  , SHADE_COLOUR(&pvkMat::SetDiffuse)},\
    {PDDI_SP_EMISSIVE , SHADE_COLOUR(&pvkMat::SetEmissive)},\
    {PDDI_SP_SPECULAR , SHADE_COLOUR(&pvkMat::SetSpecular)}

pddiShadeColourTable pvkMat::colourTable[] = 
{
    PVK_STDSHADERPARAM_COLOUR,
    {PDDI_SP_NULL , NULL}
};

pddiShadeTextureTable pvkMat::textureTable[] = 
{
    {PDDI_SP_BASETEX , SHADE_TEXTURE(&pvkMat::SetTexture)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeIntTable pvkMat::intTable[] = 
{
    {PDDI_SP_UVMODE , SHADE_INT(&pvkMat::SetUVMode)},
    {PDDI_SP_FILTER , SHADE_INT(&pvkMat::SetFilterMode)},
    {PDDI_SP_SHADEMODE , SHADE_INT(&pvkMat::SetShadeMode)},
    {PDDI_SP_ISLIT , SHADE_INT(&pvkMat::EnableLighting)},
    {PDDI_SP_BLENDMODE , SHADE_INT(&pvkMat::SetBlendMode)},
    {PDDI_SP_ALPHATEST , SHADE_INT(&pvkMat::EnableAlphaTest)},
    {PDDI_SP_ALPHACOMPARE , SHADE_INT(&pvkMat::SetAlphaCompare)},
    {PDDI_SP_TWOSIDED , SHADE_INT(&pvkMat::SetTwoSided)},              // Linux port: as the D3D8 pddi
    {PDDI_SP_EMISSIVEALPHA , SHADE_INT(&pvkMat::SetEmissiveAlpha)},    // Linux port: fades (traffic, characters)
    {PDDI_SP_NULL , NULL}
};

pddiShadeFloatTable pvkMat::floatTable[] = 
{
    {PDDI_SP_SHININESS , SHADE_FLOAT(&pvkMat::SetShininess)},
    {PDDI_SP_ALPHACOMPARE_THRESHOLD , SHADE_FLOAT(&pvkMat::SetAlphaRef)},   // Linux port: as the D3D8 pddi
    {PDDI_SP_REFRACTINDEX , SHADE_FLOAT(&pvkMat::SetHoverRefractionIndex)}, // Linux port: HoverCarRefraction
    {PDDI_SP_REFRACTBLEND , SHADE_FLOAT(&pvkMat::SetHoverRefractionBlend)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeColourTable pvkReflectionMat::colourTable[] = 
{
    PVK_STDSHADERPARAM_COLOUR,
    {PDDI_SP_ENVBLEND , SHADE_COLOUR(&pvkReflectionMat::SetEnvBlend)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeTextureTable pvkReflectionMat::textureTable[] = 
{
    {PDDI_SP_BASETEX , SHADE_TEXTURE(&pvkMat::SetTexture)},
    {PDDI_SP_REFLMAP , SHADE_TEXTURE(&pvkReflectionMat::SetReflectionMap)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeColourTable pvkRefractionMat::colourTable[] = 
{
    PVK_STDSHADERPARAM_COLOUR,
    {PDDI_SP_REFRACTCOLOUR , SHADE_COLOUR(&pvkRefractionMat::SetRefractionColour)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeFloatTable pvkRefractionMat::floatTable[] = 
{
    {PDDI_SP_SHININESS , SHADE_FLOAT(&pvkMat::SetShininess)},
    {PDDI_SP_ALPHACOMPARE_THRESHOLD , SHADE_FLOAT(&pvkMat::SetAlphaRef)},
    {PDDI_SP_REFRACTINDEX , SHADE_FLOAT(&pvkRefractionMat::SetRefractionIndex)},
    {PDDI_SP_REFRACTBLEND , SHADE_FLOAT(&pvkRefractionMat::SetRefractionBlend)},
    {PDDI_SP_NULL , NULL}
};

static inline void FillColour(pddiColour c, float* f)
{
    f[0] = float(c.Red()) / 255;
    f[1] = float(c.Green()) / 255;
    f[2] = float(c.Blue()) / 255;
    f[3] = float(c.Alpha()) / 255;
}

pvkMat::pvkMat(pvkContext* c, int prog) 
{
    context = c;
    program = prog;
    typeName[0] = 0;
    materialName = NULL;
    fileName = NULL;
    customSlots[0] = -1;
    customPasses = 1;
    customGeneration = 0;

    for(int i = 0; i < pvkMaxPasses; i++)
    {
        texEnv[i].enabled = false;
        texEnv[i].texture = NULL;
        texEnv[i].uvSet = i;
        texEnv[i].texGen = PDDI_TEXGEN_NONE;
        texEnv[i].uvMode = PDDI_UV_CLAMP;
        texEnv[i].filterMode = PDDI_FILTER_BILINEAR;

        // Linux port: the D3D8 pddi's defaults (d3dShader): black diffuse (the GL pddi had white)
        texEnv[i].lit = false;
        texEnv[i].twoSided = false;
        texEnv[i].shadeMode = PDDI_SHADE_GOURAUD;
        texEnv[i].ambient.Set(255,255,255);
        texEnv[i].diffuse.Set(0,0,0);
        texEnv[i].specular.Set(0,0,0);
        texEnv[i].emissive.Set(0,0,0);
        texEnv[i].shininess = 0.0f;

    //   srcBlend = PDDI_BF_ONE;
    //   destBlend = PDDI_BF_ZERO;

        texEnv[i].alphaTest = false;
        texEnv[i].alphaCompareMode = PDDI_COMPARE_GREATEREQUAL;
        texEnv[i].alphaBlendMode = PDDI_BLEND_NONE;
        // uninit fix (Linux port): glAlphaFunc got leftover memory as the cut-off, so alpha-tested objects
        // (fences, grated ramps) vanished when it was >= 1 or NaN. 0.5 is the D3D8 pddi's default.
        texEnv[i].alphaRef = 0.5f;
    }
    texEnv[0].enabled = true;
    pass = 0;
    hoverRefractIndex = 10.0f;      // the Xbox's default
    hoverRefractBlend = -1.0f;
}

pvkMat::~pvkMat() 
{
    free(materialName);
    free(fileName);
    for(int i = 0; i < pvkMaxPasses; i++)
        if(texEnv[i].texture)
            texEnv[i].texture->Release();
}


const char* pvkMat::GetType(void)
{
    static char simple[] = "simple";
    return simple;
}

int pvkMat::GetPasses(void)
{
    LookUpCustomShaders();
    return customPasses;        // Linux port: a user material shader rule can list several (glcustom.cpp)
}

void pvkMat::SetPass(int pass)
{
    SetDevPass(pass);
}

void pvkMat::SetTexture(pddiTexture* t) 
{
    if(t == texEnv[pass].texture)
        return;

    if(texEnv[pass].texture)
        texEnv[pass].texture->Release();

    texEnv[pass].texture = (pvkTexture*)t;

    if(texEnv[pass].texture)
        texEnv[pass].texture->AddRef();
}

void pvkMat::SetUVMode(int mode) 
{
    texEnv[pass].uvMode = (pddiUVMode)mode;
}

void pvkMat::SetFilterMode(int mode) 
{
    texEnv[pass].filterMode = (pddiFilterMode)mode;
}

void pvkMat::SetShadeMode(int shade) 
{
    texEnv[pass].shadeMode = (pddiShadeMode)shade;
}

void pvkMat::EnableLighting(int b)
{
    texEnv[pass].lit = b != 0;
}

void pvkMat::SetAmbient(pddiColour a) 
{
    texEnv[pass].ambient = a;
}

void pvkMat::SetDiffuse(pddiColour colour) 
{
    texEnv[pass].diffuse = colour;
}

void pvkMat::SetSpecular(pddiColour c) 
{
    texEnv[pass].specular = c;
}

void pvkMat::SetEmissive(pddiColour c) 
{
    texEnv[pass].emissive = c;
    SetEmissiveAlpha(c.Alpha());
}

// Linux port: as the D3D8 pddi, the emissive alpha is the material's alpha (the diffuse one): fading objects set it
void pvkMat::SetEmissiveAlpha(int alpha)
{
    texEnv[pass].diffuse.SetAlpha(alpha);
    int others = alpha < 255 ? 0 : 255;
    texEnv[pass].specular.SetAlpha(others);
    texEnv[pass].ambient.SetAlpha(others);
    texEnv[pass].emissive.SetAlpha(others);
}

void pvkMat::SetTwoSided(int twoSided)
{
    texEnv[pass].twoSided = twoSided != 0;
}

void pvkMat::SetShininess(float power) 
{
    texEnv[pass].shininess = power;
}

void pvkMat::SetBlendMode(int mode) 
{
    texEnv[pass].alphaBlendMode = (pddiBlendMode)mode;
}

void pvkMat::EnableAlphaTest(int b) 
{
    texEnv[pass].alphaTest = b != 0;
}

void pvkMat::SetAlphaCompare(int compare) 
{
    texEnv[pass].alphaCompareMode = pddiCompareMode(compare);
}

void pvkMat::SetAlphaRef(float ref) 
{
    texEnv[pass].alphaRef = ref > 0.0f ? (ref < 1.0f ? ref : 1.0f) : 0.0f;  // clamped as in the D3D8 pddi
}

int pvkMat::CountDevPasses(void) 
{
    return 1;
}

void pvkMat::SetHoverRefractionIndex(float index)
{
    hoverRefractIndex = index;
}

void pvkMat::SetHoverRefractionBlend(float blend)
{
    hoverRefractBlend = blend;
}

void pvkMat::SetDevPass(unsigned pass)
{
    pvkPipeline::Material m;
    SetupStandard(m, pass);
    pvkPipeline* pipeline = context->GetPipeline();
    if(hoverRefractBlend >= 0.0f)
    {
        // the Xbox's d3dRefractVertexShader: the lit, textured material crossfaded with the frame drawn so far, offset
        // by the normal times the index in pixels of a 480 line screen; opaque, no alpha test, no custom shader
        m.program = PGL_PROGRAM_REFRACTION;
        m.custom = -1;
        m.alphaCompare = -1;
        m.blended = false;
        pipeline->state.blend = PDDI_BLEND_NONE;
        m.refractColour[0] = m.refractColour[1] = m.refractColour[2] = m.refractColour[3] = 1.0f;
        m.refractIndex = hoverRefractIndex * 448.0f / 480.0f;
        m.refractBlend = hoverRefractBlend;
        pipeline->CaptureScreen();
    }
    pipeline->SetMaterial(m);
}

void pvkMat::LookUpCustomShaders(void)
{
    if(customGeneration == pglCustomGeneration())
        return;
    static int s_log = -1;      // SHAR_MATERIAL_LOG=1: each material's name and type when it is first drawn
    if(s_log < 0)
    {
        const char* e = getenv("SHAR_MATERIAL_LOG");
        s_log = e && e[0] == '1';
    }
    if(s_log && customGeneration == 0)
        fprintf(stderr, "PDDI: material %s (type %s, file %s)\n", materialName ? materialName : "(unnamed)", typeName,
                fileName ? fileName : "-");
    customGeneration = pglCustomGeneration();
    customPasses = pglCustomShadersFor(typeName, materialName, fileName, customSlots);
}

void pvkMat::SetupStandard(pvkPipeline::Material& m, unsigned renderPass)
{
    // the lighting, texturing and alpha-test settings go to the pipeline's programs, blending and culling to its
    // render state
    pvkPipeline* pipeline = context->GetPipeline();
    pipeline->current.colour[0] = pipeline->current.colour[1] = pipeline->current.colour[2] = pipeline->current.colour[3] = 255;

    int i = 0;
    memset(&m, 0, sizeof(m));       // compared whole by SetMaterial
    m.program = program;

    // a user material shader for this pass (rules and files can reload: looked up again then)
    LookUpCustomShaders();
    m.custom = customSlots[renderPass < (unsigned)customPasses ? renderPass : 0];

    m.textured = texEnv[i].texture != NULL;
    pipeline->SetTextures(texEnv[i].texture, NULL, texEnv[i].filterMode, texEnv[i].uvMode);

    m.alphaCompare = texEnv[i].alphaTest ? (int)texEnv[i].alphaCompareMode : -1;
    m.alphaRef = texEnv[i].alphaRef;

    m.blended = texEnv[i].alphaBlendMode != PDDI_BLEND_NONE;
    pipeline->state.blend = texEnv[i].alphaBlendMode;     // Direct3D 8's table (vkpipeline.cpp)

    // two-sided materials draw every face, as in the D3D8 pddi; a custom shader's #pragma shar_cull overrides it
    int cull = texEnv[i].twoSided ? PVK_CULL_NONE : PVK_CULL_GAME;
    if(pipeline->CustomCull(m.custom) != PVK_CULL_GAME)
        cull = pipeline->CustomCull(m.custom);
    context->SetMaterialCull(cull);
 
    m.lit = texEnv[i].lit;
    m.shininess = texEnv[i].shininess;
    FillColour(texEnv[i].ambient, m.ambient);
    FillColour(texEnv[i].emissive, m.emissive);
    FillColour(texEnv[i].diffuse, m.diffuse);
    FillColour(texEnv[i].specular, m.specular);
}

//-------------------------------------------------------------------
// reflection (d3dReflShader)
//-------------------------------------------------------------------

pvkReflectionMat::pvkReflectionMat(pvkContext* c) : pvkMat(c, PGL_PROGRAM_REFLECTION)
{
    reflectionMap = NULL;
    envBlend.Set(128,128,128,128);
}

pvkReflectionMat::~pvkReflectionMat()
{
    if(reflectionMap)
        reflectionMap->Release();
}

const char* pvkReflectionMat::GetType(void)
{
    static char type[] = "reflection";
    return type;
}

void pvkReflectionMat::SetReflectionMap(pddiTexture* t)
{
    if(t == reflectionMap)
        return;
    if(t)
        t->AddRef();
    if(reflectionMap)
        reflectionMap->Release();
    reflectionMap = (pvkTexture*)t;
}

void pvkReflectionMat::SetEnvBlend(pddiColour c)
{
    envBlend = c;
}

void pvkReflectionMat::SetDevPass(unsigned pass)
{
    pvkPipeline::Material m;
    SetupStandard(m, pass);

    // stage 1: the reflection map, sampled like the base texture
    m.reflectionTextured = reflectionMap != NULL;
    context->GetPipeline()->SetTextures(texEnv[0].texture, reflectionMap, texEnv[0].filterMode, texEnv[0].uvMode);
    FillColour(envBlend, m.envBlend);

    context->GetPipeline()->SetMaterial(m);
}

//-------------------------------------------------------------------
// refraction (ps2RefractionShader)
//-------------------------------------------------------------------

pvkRefractionMat::pvkRefractionMat(pvkContext* c) : pvkMat(c, PGL_PROGRAM_REFRACTION)
{
    refractColour.Set(255,255,255,255);     // the PS2's defaults
    refractIndex = 10.0f;
    refractBlend = 0.0f;
}

const char* pvkRefractionMat::GetType(void)
{
    static char type[] = "refract";
    return type;
}

void pvkRefractionMat::SetRefractionColour(pddiColour c)
{
    refractColour = c;
}

void pvkRefractionMat::SetRefractionIndex(float index)
{
    refractIndex = index;
}

void pvkRefractionMat::SetRefractionBlend(float blend)
{
    refractBlend = blend;
}

void pvkRefractionMat::SetDevPass(unsigned pass)
{
    pvkPipeline::Material m;
    SetupStandard(m, pass);
    FillColour(refractColour, m.refractColour);
    m.refractIndex = refractIndex;
    m.refractBlend = 1.0f;

    pvkPipeline* pipeline = context->GetPipeline();
    pipeline->CaptureScreen();
    pipeline->SetMaterial(m);
}

//-------------------------------------------------------------------
// error (d3dErrorShader)
//-------------------------------------------------------------------

pvkErrorMat::pvkErrorMat(pvkContext* c) : pvkMat(c)
{
    texEnv[0].lit = true;
    texEnv[0].ambient.Set(0,0,0);
    texEnv[0].diffuse.Set(0,0,0);
    texEnv[0].specular.Set(0,0,0);
    texEnv[0].emissive.Set(255,128,255);
}

const char* pvkErrorMat::GetType(void)
{
    static char type[] = "error";
    return type;
}

void pvkErrorMat::SetDevPass(unsigned pass)
{
    pvkPipeline::Material m;
    pvkTexture* texture = texEnv[0].texture;
    texEnv[0].texture = NULL;           // no texture stage
    SetupStandard(m, pass);
    texEnv[0].texture = texture;
    context->GetPipeline()->SetMaterial(m);
}

//-------------------------------------------------------------------
// names for user material shaders
//-------------------------------------------------------------------

void pvkMat::SetTypeName(const char* name)
{
    snprintf(typeName, sizeof(typeName), "%s", name ? name : "");
    customGeneration = 0;
}

void pvkMat::SetMaterialName(const char* name, const char* file)
{
    free(materialName);
    free(fileName);
    materialName = name ? strdup(name) : NULL;
    fileName = file ? strdup(file) : NULL;
    if(fileName)
    {
        for(char* c = fileName; *c; c++)        // "art\chars\homer_m.p3d" -> "art/chars/homer_m.p3d"
        {
            if(*c == '\\')
                *c = '/';
        }
    }
    customGeneration = 0;
}

// for p3d's shader loader (tShaderLoader, through glmat.cpp's pglSetMaterialName)
void pvkSetMaterialName(pddiShader* shader, const char* name, const char* file)
{
    if(shader)
        ((pvkMat*)shader)->SetMaterialName(name, file);
}
