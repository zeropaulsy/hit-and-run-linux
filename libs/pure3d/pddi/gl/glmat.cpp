//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include <pddi/gl/glmat.hpp>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <pddi/gl/gltex.hpp>
#include <pddi/gl/glcon.hpp>

#if defined(RAD_WIN32) && !defined(RAD_LINUX)
    #include <pddi/gl/display_win32/gl.hpp>
#endif

#ifdef RAD_LINUX
    #include <pddi/gl/display_linux/gl.hpp>
    #include <pddi/gl/display_linux/gldisplay.hpp>
    #include <pddi/gl/glpipeline.hpp>
    #include <pddi/gl/glcustom.hpp>
#endif


// Linux port: the parameters the D3D8 pddi's shaders take (dx8/shaders/shader.hpp PDDID3D_STDSHADERPARAM_*)
#define PGL_STDSHADERPARAM_COLOUR \
    {PDDI_SP_AMBIENT  , SHADE_COLOUR(&pglMat::SetAmbient)},\
    {PDDI_SP_DIFFUSE  , SHADE_COLOUR(&pglMat::SetDiffuse)},\
    {PDDI_SP_EMISSIVE , SHADE_COLOUR(&pglMat::SetEmissive)},\
    {PDDI_SP_SPECULAR , SHADE_COLOUR(&pglMat::SetSpecular)}

pddiShadeColourTable pglMat::colourTable[] = 
{
    PGL_STDSHADERPARAM_COLOUR,
    {PDDI_SP_NULL , NULL}
};

pddiShadeTextureTable pglMat::textureTable[] = 
{
    {PDDI_SP_BASETEX , SHADE_TEXTURE(&pglMat::SetTexture)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeIntTable pglMat::intTable[] = 
{
    {PDDI_SP_UVMODE , SHADE_INT(&pglMat::SetUVMode)},
    {PDDI_SP_FILTER , SHADE_INT(&pglMat::SetFilterMode)},
    {PDDI_SP_SHADEMODE , SHADE_INT(&pglMat::SetShadeMode)},
    {PDDI_SP_ISLIT , SHADE_INT(&pglMat::EnableLighting)},
    {PDDI_SP_BLENDMODE , SHADE_INT(&pglMat::SetBlendMode)},
    {PDDI_SP_ALPHATEST , SHADE_INT(&pglMat::EnableAlphaTest)},
    {PDDI_SP_ALPHACOMPARE , SHADE_INT(&pglMat::SetAlphaCompare)},
    {PDDI_SP_TWOSIDED , SHADE_INT(&pglMat::SetTwoSided)},              // Linux port: as the D3D8 pddi
    {PDDI_SP_EMISSIVEALPHA , SHADE_INT(&pglMat::SetEmissiveAlpha)},    // Linux port: fades (traffic, characters)
    {PDDI_SP_NULL , NULL}
};

pddiShadeFloatTable pglMat::floatTable[] = 
{
    {PDDI_SP_SHININESS , SHADE_FLOAT(&pglMat::SetShininess)},
    {PDDI_SP_ALPHACOMPARE_THRESHOLD , SHADE_FLOAT(&pglMat::SetAlphaRef)},   // Linux port: as the D3D8 pddi
    {PDDI_SP_NULL , NULL}
};

pddiShadeColourTable pglReflectionMat::colourTable[] = 
{
    PGL_STDSHADERPARAM_COLOUR,
    {PDDI_SP_ENVBLEND , SHADE_COLOUR(&pglReflectionMat::SetEnvBlend)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeTextureTable pglReflectionMat::textureTable[] = 
{
    {PDDI_SP_BASETEX , SHADE_TEXTURE(&pglMat::SetTexture)},
    {PDDI_SP_REFLMAP , SHADE_TEXTURE(&pglReflectionMat::SetReflectionMap)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeColourTable pglRefractionMat::colourTable[] = 
{
    PGL_STDSHADERPARAM_COLOUR,
    {PDDI_SP_REFRACTCOLOUR , SHADE_COLOUR(&pglRefractionMat::SetRefractionColour)},
    {PDDI_SP_NULL , NULL}
};

pddiShadeFloatTable pglRefractionMat::floatTable[] = 
{
    {PDDI_SP_SHININESS , SHADE_FLOAT(&pglMat::SetShininess)},
    {PDDI_SP_ALPHACOMPARE_THRESHOLD , SHADE_FLOAT(&pglMat::SetAlphaRef)},
    {PDDI_SP_REFRACTINDEX , SHADE_FLOAT(&pglRefractionMat::SetRefractionIndex)},
    {PDDI_SP_REFRACTBLEND , SHADE_FLOAT(&pglRefractionMat::SetRefractionBlend)},
    {PDDI_SP_NULL , NULL}
};

GLenum filterMagTable[5] =
{
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST,
    GL_LINEAR,
    GL_LINEAR
};

// GL_NEAREST_MIPMAP_LINEAR not used
GLenum filterMinTable[5] =
{
    GL_NEAREST,
    GL_LINEAR,
    GL_NEAREST,//GL_NEAREST_MIPMAP_NEAREST,
    GL_LINEAR,//GL_LINEAR_MIPMAP_NEAREST,
    GL_LINEAR,//GL_LINEAR_MIPMAP_LINEAR
};

GLenum uvTable[3] =
{
    GL_REPEAT,
    GL_CLAMP_TO_EDGE,   // Linux port: D3D clamp = clamp to edge; GL_CLAMP blends in the (black) border
    GL_CLAMP_TO_EDGE
};

// Linux port: Direct3D 8's (dx8/state.cpp alphaBlendTable): the GL pddi had no blend equations, so subtract drew
// opaque and the modulate-alpha modes were wrong
static const struct { GLenum equation, src, dest; } alphaBlendTable[8] =
{
    { GL_FUNC_ADD,              GL_ONE,       GL_ZERO },                //PDDI_BLEND_NONE (blending off)
    { GL_FUNC_ADD,              GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA }, //PDDI_BLEND_ALPHA
    { GL_FUNC_ADD,              GL_ONE,       GL_ONE },                 //PDDI_BLEND_ADD
    { GL_FUNC_REVERSE_SUBTRACT, GL_ONE,       GL_ONE },                 //PDDI_BLEND_SUBTRACT
    { GL_FUNC_ADD,              GL_DST_COLOR, GL_ZERO },                //PDDI_BLEND_MODULATE
    { GL_FUNC_ADD,              GL_DST_COLOR, GL_SRC_COLOR },           //PDDI_BLEND_MODULATE2
    { GL_FUNC_ADD,              GL_DST_COLOR, GL_SRC_ALPHA },           //PDDI_BLEND_ADDMODULATEALPHA
    { GL_FUNC_REVERSE_SUBTRACT, GL_SRC_ALPHA, GL_SRC_ALPHA }            //PDDI_BLEND_SUBMODULATEALPHA
};

static inline void FillGLColour(pddiColour c, float* f)
{
    f[0] = float(c.Red()) / 255;
    f[1] = float(c.Green()) / 255;
    f[2] = float(c.Blue()) / 255;
    f[3] = float(c.Alpha()) / 255;
}

pglMat::pglMat(pglContext* c, int prog) 
{
    context = c;
    program = prog;
    typeName[0] = 0;
    materialName = NULL;
    fileName = NULL;
    customSlots[0] = -1;
    customPasses = 1;
    customGeneration = 0;

    for(int i = 0; i < pglMaxPasses; i++)
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
}

pglMat::~pglMat() 
{
    free(materialName);
    free(fileName);
    for(int i = 0; i < pglMaxPasses; i++)
        if(texEnv[i].texture)
            texEnv[i].texture->Release();
}


const char* pglMat::GetType(void)
{
    static char simple[] = "simple";
    return simple;
}

int pglMat::GetPasses(void)
{
    LookUpCustomShaders();
    return customPasses;        // Linux port: a user material shader rule can list several (glcustom.cpp)
}

void pglMat::SetPass(int pass)
{
    SetDevPass(pass);
}

void pglMat::SetTexture(pddiTexture* t) 
{
    if(t == texEnv[pass].texture)
        return;

    if(texEnv[pass].texture)
        texEnv[pass].texture->Release();

    texEnv[pass].texture = (pglTexture*)t;

    if(texEnv[pass].texture)
        texEnv[pass].texture->AddRef();
}

void pglMat::SetUVMode(int mode) 
{
    texEnv[pass].uvMode = (pddiUVMode)mode;
}

void pglMat::SetFilterMode(int mode) 
{
    texEnv[pass].filterMode = (pddiFilterMode)mode;
}

void pglMat::SetShadeMode(int shade) 
{
    texEnv[pass].shadeMode = (pddiShadeMode)shade;
}

void pglMat::EnableLighting(int b)
{
    texEnv[pass].lit = b != 0;
}

void pglMat::SetAmbient(pddiColour a) 
{
    texEnv[pass].ambient = a;
}

void pglMat::SetDiffuse(pddiColour colour) 
{
    texEnv[pass].diffuse = colour;
}

void pglMat::SetSpecular(pddiColour c) 
{
    texEnv[pass].specular = c;
}

void pglMat::SetEmissive(pddiColour c) 
{
    texEnv[pass].emissive = c;
    SetEmissiveAlpha(c.Alpha());
}

// Linux port: as the D3D8 pddi, the emissive alpha is the material's alpha (the diffuse one): fading objects set it
void pglMat::SetEmissiveAlpha(int alpha)
{
    texEnv[pass].diffuse.SetAlpha(alpha);
    int others = alpha < 255 ? 0 : 255;
    texEnv[pass].specular.SetAlpha(others);
    texEnv[pass].ambient.SetAlpha(others);
    texEnv[pass].emissive.SetAlpha(others);
}

void pglMat::SetTwoSided(int twoSided)
{
    texEnv[pass].twoSided = twoSided != 0;
}

void pglMat::SetShininess(float power) 
{
    texEnv[pass].shininess = power;
}

void pglMat::SetBlendMode(int mode) 
{
    texEnv[pass].alphaBlendMode = (pddiBlendMode)mode;
}

void pglMat::EnableAlphaTest(int b) 
{
    texEnv[pass].alphaTest = b != 0;
}

void pglMat::SetAlphaCompare(int compare) 
{
    texEnv[pass].alphaCompareMode = pddiCompareMode(compare);
}

void pglMat::SetAlphaRef(float ref) 
{
    texEnv[pass].alphaRef = ref > 0.0f ? (ref < 1.0f ? ref : 1.0f) : 0.0f;  // clamped as in the D3D8 pddi
}

int pglMat::CountDevPasses(void) 
{
    return 1;
}

void pglMat::SetDevPass(unsigned pass)
{
    pglPipeline::Material m;
    SetupStandard(m, pass);
    context->GetDisplay()->GetPipeline()->SetMaterial(m);
}

void pglMat::LookUpCustomShaders(void)
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

void pglMat::SetupStandard(pglPipeline::Material& m, unsigned renderPass)
{
    // Linux port: the lighting, texturing and alpha-test settings go to the pipeline's programs (glpipeline.cpp);
    // blending, culling and texture parameters are still GL state
    pglPipeline* pipeline = context->GetDisplay()->GetPipeline();
    pipeline->current.colour[0] = pipeline->current.colour[1] = pipeline->current.colour[2] = pipeline->current.colour[3] = 255;

    int i = 0;
    memset(&m, 0, sizeof(m));       // compared whole by SetMaterial
    m.program = program;

    // a user material shader for this pass (rules and files can reload: looked up again then)
    LookUpCustomShaders();
    m.custom = customSlots[renderPass < (unsigned)customPasses ? renderPass : 0];

    m.textured = texEnv[i].texture != NULL;
    if(texEnv[i].texture)
    {
        texEnv[i].texture->SetGLState();
        texEnv[i].texture->SetSampling(texEnv[i].filterMode, uvTable[texEnv[i].uvMode],
                                       filterMagTable[texEnv[i].filterMode], filterMinTable[texEnv[i].filterMode]);
    }

    m.alphaCompare = texEnv[i].alphaTest ? (int)texEnv[i].alphaCompareMode : -1;
    m.alphaRef = texEnv[i].alphaRef;

    m.blended = texEnv[i].alphaBlendMode != PDDI_BLEND_NONE;
    if(texEnv[i].alphaBlendMode == PDDI_BLEND_NONE)
    {
        glDisable(GL_BLEND);
    }
    else
    {
        glEnable(GL_BLEND);
        glBlendEquation(alphaBlendTable[texEnv[i].alphaBlendMode].equation);
        glBlendFunc(alphaBlendTable[texEnv[i].alphaBlendMode].src, alphaBlendTable[texEnv[i].alphaBlendMode].dest);
    }

    // two-sided materials draw every face, as in the D3D8 pddi; a custom shader's #pragma shar_cull overrides it
    int cull = texEnv[i].twoSided ? PGL_CULL_NONE : PGL_CULL_GAME;
    if(pipeline->CustomCull(m.custom) != PGL_CULL_GAME)
        cull = pipeline->CustomCull(m.custom);
    context->SetMaterialCull(cull);
 
    m.lit = texEnv[i].lit;
    m.shininess = texEnv[i].shininess;
    FillGLColour(texEnv[i].ambient, m.ambient);
    FillGLColour(texEnv[i].emissive, m.emissive);
    FillGLColour(texEnv[i].diffuse, m.diffuse);
    FillGLColour(texEnv[i].specular, m.specular);
}

//-------------------------------------------------------------------
// reflection (d3dReflShader)
//-------------------------------------------------------------------

pglReflectionMat::pglReflectionMat(pglContext* c) : pglMat(c, PGL_PROGRAM_REFLECTION)
{
    reflectionMap = NULL;
    envBlend.Set(128,128,128,128);
}

pglReflectionMat::~pglReflectionMat()
{
    if(reflectionMap)
        reflectionMap->Release();
}

const char* pglReflectionMat::GetType(void)
{
    static char type[] = "reflection";
    return type;
}

void pglReflectionMat::SetReflectionMap(pddiTexture* t)
{
    if(t == reflectionMap)
        return;
    if(t)
        t->AddRef();
    if(reflectionMap)
        reflectionMap->Release();
    reflectionMap = (pglTexture*)t;
}

void pglReflectionMat::SetEnvBlend(pddiColour c)
{
    envBlend = c;
}

void pglReflectionMat::SetDevPass(unsigned pass)
{
    pglPipeline::Material m;
    SetupStandard(m, pass);

    // stage 1: the reflection map, sampled like the base texture
    m.reflectionTextured = reflectionMap != NULL;
    if(reflectionMap)
    {
        glActiveTexture(GL_TEXTURE1);
        reflectionMap->SetGLState();
        reflectionMap->SetSampling(texEnv[0].filterMode, uvTable[texEnv[0].uvMode],
                                   filterMagTable[texEnv[0].filterMode], filterMinTable[texEnv[0].filterMode]);
        glActiveTexture(GL_TEXTURE0);
    }
    FillGLColour(envBlend, m.envBlend);

    context->GetDisplay()->GetPipeline()->SetMaterial(m);
}

//-------------------------------------------------------------------
// refraction (ps2RefractionShader)
//-------------------------------------------------------------------

pglRefractionMat::pglRefractionMat(pglContext* c) : pglMat(c, PGL_PROGRAM_REFRACTION)
{
    refractColour.Set(255,255,255,255);     // the PS2's defaults
    refractIndex = 10.0f;
    refractBlend = 0.0f;
}

const char* pglRefractionMat::GetType(void)
{
    static char type[] = "refract";
    return type;
}

void pglRefractionMat::SetRefractionColour(pddiColour c)
{
    refractColour = c;
}

void pglRefractionMat::SetRefractionIndex(float index)
{
    refractIndex = index;
}

void pglRefractionMat::SetRefractionBlend(float blend)
{
    refractBlend = blend;
}

void pglRefractionMat::SetDevPass(unsigned pass)
{
    pglPipeline::Material m;
    SetupStandard(m, pass);
    FillGLColour(refractColour, m.refractColour);
    m.refractIndex = refractIndex;

    pglPipeline* pipeline = context->GetDisplay()->GetPipeline();
    pipeline->CaptureScreen();
    pipeline->SetMaterial(m);
}

//-------------------------------------------------------------------
// error (d3dErrorShader)
//-------------------------------------------------------------------

pglErrorMat::pglErrorMat(pglContext* c) : pglMat(c)
{
    texEnv[0].lit = true;
    texEnv[0].ambient.Set(0,0,0);
    texEnv[0].diffuse.Set(0,0,0);
    texEnv[0].specular.Set(0,0,0);
    texEnv[0].emissive.Set(255,128,255);
}

const char* pglErrorMat::GetType(void)
{
    static char type[] = "error";
    return type;
}

void pglErrorMat::SetDevPass(unsigned pass)
{
    pglPipeline::Material m;
    pglTexture* texture = texEnv[0].texture;
    texEnv[0].texture = NULL;           // no texture stage
    SetupStandard(m, pass);
    texEnv[0].texture = texture;
    context->GetDisplay()->GetPipeline()->SetMaterial(m);
}

//-------------------------------------------------------------------
// names for user material shaders
//-------------------------------------------------------------------

void pglMat::SetTypeName(const char* name)
{
    snprintf(typeName, sizeof(typeName), "%s", name ? name : "");
    customGeneration = 0;
}

void pglMat::SetMaterialName(const char* name, const char* file)
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

// for p3d's shader loader (tShaderLoader): the material's name and the art file it is in
void pglSetMaterialName(pddiShader* shader, const char* name, const char* file)
{
    if(shader)
        ((pglMat*)shader)->SetMaterialName(name, file);
}
