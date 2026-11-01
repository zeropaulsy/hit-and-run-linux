/* Mod Launcher OverrideShaderParameters: a mod's OverrideShaderParameters.xml (port/compat/shar_modshaders.cpp)
 *
 *   <OverrideShaderParameters>
 *     <Shader Name="famil_v*">                       (no Name: every shader; * and ? wildcards, any case)
 *       <Parameter Name="DIFF" Value="0xFFFF0000" />  colour: hex ARGB (6 digits: opaque) or Red/Green/Blue[/Alpha]
 *       <Parameter Name="2SID" Value="false" />       int: number, true, false
 *       <Parameter Name="SHIN" Value="0.5" />         float;  vector: Value="x,y,z" or X/Y/Z;  texture: its name
 *       <Parameter Name="2SID" />                     no value: the shader file's own
 *     </Shader>
 *   </OverrideShaderParameters>
 *
 * The parameter's type comes from its name (the pddi's shader parameters, pddishade.hpp). Later entries win.
 * Applied as shaders load (libs/pure3d/p3d/shader.cpp); the game's culling is off, so 2SID changes nothing
 * drawn (as on Windows). Only read when a mod requires the hack. Read once, on first use (any thread);
 * static storage only. */
#ifndef SHAR_COMPAT_SHAR_MODSHADERS_H
#define SHAR_COMPAT_SHAR_MODSHADERS_H

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    SHAR_SHADERPARAM_INT,
    SHAR_SHADERPARAM_FLOAT,
    SHAR_SHADERPARAM_COLOUR,
    SHAR_SHADERPARAM_VECTOR,
    SHAR_SHADERPARAM_TEXTURE
};

typedef struct SharModShaderParam
{
    unsigned param;             /* the pddi parameter (PDDI_FOURCC of the name) */
    int type;                   /* SHAR_SHADERPARAM_* */
    int hasValue;               /* 0: keep the shader file's value */
    int intValue;
    float floatValue;
    unsigned colour;            /* ARGB */
    float vector[ 3 ];
    char texture[ 64 ];
} SharModShaderParam;

/* The overrides for the shader named shaderName, one per parameter, into out (at most max); returns the count. */
int shar_ModShaderOverrides( const char* shaderName, SharModShaderParam* out, int max );

#ifdef __cplusplus
}
#endif

#endif
