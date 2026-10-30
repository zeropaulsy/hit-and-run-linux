//=============================================================================
// Linux port: Vulkan renderer materials: glmat.hpp's classes drawing through pvkPipeline (keep the two in step)
//=============================================================================


#ifndef _VKMAT_HPP_
#define _VKMAT_HPP_

#include <pddi/pddi.hpp>
#include <pddi/base/baseshader.hpp>
#include <pddi/vk/vkcon.hpp>
#include <pddi/vk/vkpipeline.hpp>
#include <pddi/gl/glcustom.hpp>
class pvkTexture;

const int pvkMaxPasses = 1;

struct pvkTextureEnv
{
    bool enabled;
    pvkTexture* texture;

    int uvSet;
    pddiTextureGen texGen;
    pddiUVMode uvMode;
    pddiFilterMode filterMode;

    bool alphaTest;
    pddiBlendMode alphaBlendMode;
    pddiCompareMode alphaCompareMode;
    float alphaRef;

    bool lit;
    bool twoSided;
    pddiShadeMode shadeMode;
    pddiColour diffuse;
    pddiColour specular;
    pddiColour ambient;
    pddiColour emissive;
    float shininess;
};

// Linux port: the shader names map to these classes as in the D3D8 pddi (pglInstallShaders, gldev.cpp); each draws
// with one of the pipeline's programs (pglProgramType, glpipeline.hpp)
class pvkMat : public pddiBaseShader
{
public:
    pvkMat(pvkContext*, int program = 0);
    ~pvkMat();

    static pddiShadeColourTable colourTable[];
    static pddiShadeTextureTable textureTable[];
    static pddiShadeIntTable intTable[];
    static pddiShadeFloatTable floatTable[];

    const char* GetType(void);
    int         GetPasses(void);
    void        SetPass(int pass);

    pddiShadeTextureTable* GetTextureTable(void) { return textureTable;}
    pddiShadeIntTable*     GetIntTable(void)     { return intTable;}
    pddiShadeFloatTable*   GetFloatTable(void)   { return floatTable;}
    pddiShadeColourTable*  GetColourTable(void)  { return colourTable;}

    // texture
    void SetTexture(pddiTexture* texture);
    void SetUVMode(int mode);
    void SetFilterMode(int mode);

    // shading
    void SetShadeMode(int shade);
    void SetTwoSided(int twoSided);

    // lighting
    void EnableLighting(int);

    void SetDiffuse(pddiColour colour);
    void SetAmbient(pddiColour colour);
    void SetEmissive(pddiColour);
    void SetEmissiveAlpha(int alpha);
    void SetSpecular(pddiColour);
    void SetShininess(float power);

    // alpha blending
    void SetBlendMode(int mode);
    void EnableAlphaTest(int);
    void SetAlphaCompare(int compare);
    void SetAlphaRef(float ref);

    // Linux port: HoverCarRefraction (geometryvehicle.cpp) sets these on the hover car's simple materials, as on the
    // Xbox's refract ones: a blend >= 0 draws them refracting (the Xbox's d3dRefractVertexShader)
    void SetHoverRefractionIndex(float index);
    void SetHoverRefractionBlend(float blend);

    int  CountDevPasses(void);
    virtual void SetDevPass(unsigned);

    // Linux port: what user material shaders (glcustom.cpp) are matched by: the pddi shader type the art asked for
    // ("spheremap", or a name only a custom shader knows), the material's own name and the art file it came from
    // (both set by p3d's shader loader)
    void SetTypeName(const char* name);
    void SetMaterialName(const char* name, const char* file);

protected:
    // the pipeline material and GL state for a draw pass (the standard part; shader kinds add theirs and set it)
    void SetupStandard(pvkPipeline::Material& m, unsigned renderPass);
    void LookUpCustomShaders(void);     // when the user material shaders' rules or files were reloaded

    pvkContext* context;
    int program;
    int pass;
    char typeName[32];
    char* materialName;
    char* fileName;
    int customSlots[pglMaxMaterialPasses];  // a pipeline custom program per pass (-1: built-in)
    int customPasses;
    unsigned customGeneration;
    pvkTextureEnv texEnv[pvkMaxPasses];
    float hoverRefractIndex;
    float hoverRefractBlend;    // < 0: not refracting
};

// "environment", "spheremap" (d3dReflShader): the base texture plus a reflection map looked up with the camera-space
// reflection vector, scaled by the environment blend colour
class pvkReflectionMat : public pvkMat
{
public:
    pvkReflectionMat(pvkContext*);
    ~pvkReflectionMat();

    static pddiShadeColourTable colourTable[];
    static pddiShadeTextureTable textureTable[];

    const char* GetType(void);
    pddiShadeTextureTable* GetTextureTable(void) { return textureTable;}
    pddiShadeColourTable*  GetColourTable(void)  { return colourTable;}

    void SetReflectionMap(pddiTexture* texture);
    void SetEnvBlend(pddiColour colour);

    void SetDevPass(unsigned);

private:
    pvkTexture* reflectionMap;
    pddiColour envBlend;
};

// "refract" (the PS2 pddi's ps2RefractionShader; installed for mods' RefractionShaderSupport): the frame drawn so
// far, looked up where the surface's normal bends it to, times the refraction colour; the base texture is unused
class pvkRefractionMat : public pvkMat
{
public:
    pvkRefractionMat(pvkContext*);

    static pddiShadeColourTable colourTable[];
    static pddiShadeFloatTable floatTable[];

    const char* GetType(void);
    pddiShadeColourTable* GetColourTable(void) { return colourTable;}
    pddiShadeFloatTable*  GetFloatTable(void)  { return floatTable;}

    void SetRefractionColour(pddiColour colour);
    void SetRefractionIndex(float index);
    void SetRefractionBlend(float blend);   // the PS2's simple-shader fallback only: kept, unused

    void SetDevPass(unsigned);

private:
    pddiColour refractColour;
    float refractIndex;
    float refractBlend;
};

// "error" (d3dErrorShader): drawn for shaders a file names but does not contain; lit, pink emissive, no texture
class pvkErrorMat : public pvkMat
{
public:
    pvkErrorMat(pvkContext*);
    const char* GetType(void);
    void SetDevPass(unsigned);
};

#endif
