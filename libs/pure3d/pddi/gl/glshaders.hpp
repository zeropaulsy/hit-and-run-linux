//=============================================================================
// Linux port: the GLSL of the renderer's programs (glshaders.cpp), shared by the OpenGL and Vulkan pddis
//=============================================================================

#ifndef _GLSHADERS_HPP_
#define _GLSHADERS_HPP_

// the programs, one per kind of pddi shader (glmat.cpp)
enum pglProgramType
{
    PGL_PROGRAM_SIMPLE,         // "simple": diffuse * texture
    PGL_PROGRAM_REFLECTION,     // "environment", "spheremap": diffuse * texture + envBlend * reflection map
    PGL_PROGRAM_REFRACTION,     // "refract" (the PS2 pddi's; mods' RefractionShaderSupport), HoverCarRefraction's
                                // materials (pglMat): the frame drawn so far, looked up offset by the normal, times
                                // the refraction colour, over the material by refractBlend
    PGL_PROGRAM_COUNT
};

// the programs' uniforms (pglUniformNames)
enum
{
    U_PROJECTION, U_MODELVIEW, U_NORMALMATRIX,
    U_LIT, U_MATAMBIENT, U_MATDIFFUSE, U_MATSPECULAR, U_MATEMISSIVE, U_SHININESS,
    U_GLOBALAMBIENT, U_LIGHTCOUNT, U_LIGHTPOSITION, U_LIGHTCOLOUR, U_LIGHTATTEN, U_LIGHTRANGE,
    U_TEXTURED, U_TEXTURE, U_FOG, U_FOGCOLOUR, U_FOGEND, U_FOGSCALE, U_ALPHACOMPARE, U_ALPHAREF,
    U_REFLECTIONTEXTURED, U_REFLECTION, U_ENVBLEND, U_ALPHATOCOVERAGE, U_TIME, U_RESOLUTION, U_MIPALPHA,
    U_SCREEN, U_REFRACTCOLOUR, U_REFRACTINDEX, U_REFRACTBLEND,
    U_COUNT
};

struct pglProgramSource { const char* name; const char* header; };

// vertex stage: the declarations, a custom shader's MaterialVertex part, main; fragment stage: the prelude, then
// main (built-in programs) or a custom shader's ShadeMaterial and the custom main
extern const char* const pglVertexDeclarations;
extern const char* const pglVertexMain;
extern const char* const pglFragmentPrelude;
extern const char* const pglFragmentMain;
extern const char* const pglCustomFragmentMain;
extern const pglProgramSource pglProgramSources[PGL_PROGRAM_COUNT];   // #version and #defines per program
extern const char* const pglUniformNames[U_COUNT];

#endif
