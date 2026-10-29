//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include "p3d/shader.hpp"
#include "constants/chunks.h"
#include "constants/chunkids.hpp"
#include "p3d/utility.hpp"
#include "p3d/chunkfile.hpp"
#include "p3d/texture.hpp"
#include <string.h>

#ifdef RAD_LINUX
#include <shar_modshaders.h>
#include <shar_mods.h>
void pglSetMaterialName(pddiShader* shader, const char* name, const char* file);     // pddi/gl/glmat.cpp
bool radMovieSetVideoTexture(pddiShader* shader, unsigned param, const char* path);  // radmovie binkvideotexture.cpp

// Linux port: mods' VideoTextureSupport: a texture named by a virtual path ("/GameData/x.bik") is a Bink video
static bool SetModVideoTexture(tShader* shader, unsigned param, const char* name)
{
    static int s_Enabled = -1;
    if(s_Enabled < 0)
        s_Enabled = shar_ModHackRequired("VideoTextureSupport");
    return s_Enabled && name[0] == '/' && radMovieSetVideoTexture(shader->GetShader(), param, name);
}

// Linux port: mods' HackSupport.ini [Miscellaneous] OverrideTexture: one texture on every shader (animated ones
// set their own each frame)
static void ApplyModOverrideTexture(tShader* shader, tEntityStore* store)
{
    static int s_Read = 0;
    static char s_Texture[256];
    if(!s_Read)
    {
        if(!shar_ModHackRequired("HackSupport") ||
           !shar_ModConfigValue("HackSupport.ini", "Miscellaneous", "OverrideTexture", s_Texture, sizeof(s_Texture)))
            s_Texture[0] = '\0';
        s_Read = 1;
    }
    if(s_Texture[0] == '\0' || SetModVideoTexture(shader, PDDI_SP_BASETEX, s_Texture))
        return;
    tTexture* tex = p3d::find<tTexture>(store, s_Texture);
    if(!tex)
        tex = p3d::find<tTexture>(s_Texture);
    if(tex)
        shader->SetTexture(PDDI_SP_BASETEX, tex);
}

// Linux port: mods' OverrideShaderParameters.xml (port/compat/shar_modshaders.h), as the shader loads
static void ApplyModShaderOverrides(tShader* shader, const char* name, tEntityStore* store)
{
    SharModShaderParam params[32];
    int count = shar_ModShaderOverrides(name, params, 32);
    bool ambient = false;
    const pddiColour* diffuse = NULL;
    pddiColour diffuseColour;
    for(int i = 0; i < count; i++)
    {
        const SharModShaderParam& p = params[i];
        if(p.param == PDDI_SP_AMBIENT)
            ambient = true;
        if(!p.hasValue)
            continue;
        switch(p.type)
        {
            case SHAR_SHADERPARAM_INT:
                shader->SetInt(p.param, p.intValue);
                if(p.param == PDDI_SP_BLENDMODE)
                    shader->mFileBlendMode = p.intValue;
                break;
            case SHAR_SHADERPARAM_FLOAT:
                shader->SetFloat(p.param, p.floatValue);
                break;
            case SHAR_SHADERPARAM_COLOUR:
                shader->SetColour(p.param, pddiColour(p.colour));
                if(p.param == PDDI_SP_DIFFUSE)
                {
                    diffuseColour = pddiColour(p.colour);
                    diffuse = &diffuseColour;
                }
                break;
            case SHAR_SHADERPARAM_VECTOR:
                {
                    pddiVector v;
                    v.x = p.vector[0];
                    v.y = p.vector[1];
                    v.z = p.vector[2];
                    shader->SetVector(p.param, v);
                }
                break;
            case SHAR_SHADERPARAM_TEXTURE:
                {
                    if(SetModVideoTexture(shader, p.param, p.texture))
                        break;
                    tTexture* tex = p3d::find<tTexture>(store, p.texture);
                    if(!tex)
                        tex = p3d::find<tTexture>(p.texture);
                    if(tex)
                        shader->SetTexture(p.param, tex);
                    else
                        rReleasePrintf("mods: shader %s: no texture %s\n", name, p.texture);
                }
                break;
        }
    }
    // the loader sets ambient with diffuse
    if(diffuse != NULL && !ambient)
        shader->SetColour(PDDI_SP_AMBIENT, *diffuse);
}
#endif

tShader::tShader()
{
    shader = p3d::device->NewShader("simple");
    shader->AddRef();
#ifdef RAD_LINUX
    mFileBlendMode = -1;
#endif
}

tShader::tShader(char* shaderName, char* definition)
{
    shader = p3d::device->NewShader(shaderName, definition);
    shader->AddRef();
#ifdef RAD_LINUX
    mFileBlendMode = -1;
#endif
}

tShader::~tShader()
{
    if(shader)
        shader->Release();

    shader = NULL;
}

tShaderLoader::tShaderLoader() : tSimpleChunkHandler(Pure3D::Shader::SHADER)
{
}

static void DrawInventory(int index = -1)
{
    tInventory::Iterator<tEntity> it;
    tEntity* obj = it.First();
    p3d::print("inventory dump:\n");
    while(obj)
    {
        char cls[128];
        p3d::ClassName(obj, cls, 128);
#ifdef P3D_ALLOW_ENTITY_GETNAME
        p3d::printf("\t'%s' (%s)\n", obj->GetName(), cls);
#endif
        obj = it.Next();            
    }
}

tEntity* tShaderLoader::LoadObject(tChunkFile* f, tEntityStore* store)
{
    char name[128];
    char shaderName[128];
    
    
    char pure [ ]= "pure3";
    char tempname[8];
    char lambert[ ] ="lambe";
    char buffer [255];

    
    f->GetString(name);

    strncpy(tempname,name,5);    
    tempname[5]='\0';

#ifndef RAD_RELEASE 
     //Chuck: adding this to SO no shaders called pure3dsimpleshader and lambert ever get loaded into our game
    if (strcmp(tempname,pure)==0)
    {
        sprintf(buffer,"Error: A Shader with %s in the name found in File %s \n",name,f->GetFilename()); 
        rReleasePrintf(buffer);

        //rTuneAssertMsg(0,buffer);
        rAssertMsg(0,buffer);
        return NULL;
    }

    if (strcmp(tempname,lambert) ==0)
    {
        sprintf(buffer,"Error: A Shader with %s in the name found in File %s \n",name,f->GetFilename());
        rReleasePrintf(buffer);

        //rTuneAssertMsg(0,buffer);
        rAssertMsg(0,buffer);
        return NULL;
    }
    
#endif

    int version = f->GetInt();
    P3DASSERT(version == 0);
    f->GetString(shaderName);

    // Gamecube soln to having chainlink and gate_chlink fence shaders that 
    // become solid at certain mipmap levels
#ifdef RAD_GAMECUBE
    const char* s_HighThresholdNames[] = 
    {
        "gate_chlink_m",
        "chainlink_m"
    };
    const int NUM_HIGH_THRESHOLD_NAMES = sizeof( s_HighThresholdNames ) / sizeof( s_HighThresholdNames[0] );
    const float HIGH_ALPHA_THRESHOLD = 0.8f;
    bool useHighAlphaThreshold = false;
    for ( int i = 0 ; i < NUM_HIGH_THRESHOLD_NAMES ; i++ )
    {
        if ( strcmp( name, s_HighThresholdNames[i] ) == 0 )
        {
            useHighAlphaThreshold = true;          
            break;
        }             
    }
#endif


    bool     HasTranslucency = (f->GetLong() != 0);
    unsigned VertexNeeds = f->GetLong();
    unsigned VertexMask = f->GetLong();

    // nv: hack to fix badly mipmapped chainlink fence
    bool foundEvilChainlink = false;

    int count = f->GetInt();


    tShader* shader = NULL;

    while(f->ChunksRemaining())
    {
        f->BeginChunk();

        switch(f->GetCurrentID())
        {
            case Pure3D::Shader::SHADER_DEFINITION:
                {
                    P3DASSERT(!shader);
                    char defName[256];
                    f->GetString(defName); // not used when embeded
                    int size = f->GetInt();
                    char* buffer = (char*)p3d::MallocTemp(size+1);
                    f->GetData(buffer, size, tFile::BYTE);
                    buffer[size] = 0;
                    shader = new tShader(shaderName, buffer);
                    p3d::FreeTemp(buffer);
                }
                break;

            case Pure3D::Shader::TEXTURE_PARAM:
                {
                    char texName[256];

                    if(!shader)
                        shader = new tShader(shaderName);

                    unsigned param = f->GetInt();
                    f->GetPString(texName);

                    // oct15/2002 amb - todo fix this
                    tTexture* tex = p3d::find<tTexture>(store, texName);
                    if(!tex)
                    {                        // try a global find.
                        tex = p3d::find<tTexture>(texName);
                    }
                    
                    // nv:  If this shader uses the chainlink texture, I'm going to turn off mipmapping
                    if( strstr(texName, "chainlink") )
                    {
                        foundEvilChainlink = true;
                    }

#ifdef RAD_LINUX
                    if(SetModVideoTexture(shader, param, texName))
                        break;
#endif
                    shader->SetTexture(param, tex);
                }
                break;

            case Pure3D::Shader::INT_PARAM:
                {
                    if(!shader)
                        shader = new tShader(shaderName);

                    unsigned param = f->GetInt();
                    unsigned val = f->GetInt();

                    shader->SetInt(param, val);
#ifdef RAD_LINUX
                    if(param == PDDI_SP_BLENDMODE)
                        shader->mFileBlendMode = val;
#endif
                }
                break;

            case Pure3D::Shader::FLOAT_PARAM:
                {
                    if(!shader)
                        shader = new tShader(shaderName);

                    unsigned param = f->GetInt();
                    float val = f->GetFloat();

#ifdef RAD_GAMECUBE
                    // MKR - we are forcing the alpha compare threshold to a higher level
                    // to fix the problem with chainlink fences
                    if ( useHighAlphaThreshold && param == PDDI_SP_ALPHACOMPARE_THRESHOLD )
                    {
                        val = HIGH_ALPHA_THRESHOLD;
                    }
#endif

                    shader->SetFloat(param, val);
                }
                break;

            case Pure3D::Shader::COLOUR_PARAM:
                {
                    if(!shader)
                        shader = new tShader(shaderName);

                    unsigned param = f->GetInt();
                    unsigned val = f->GetInt();

                    if(param != PDDI_SP_AMBIENT)
                    {
                        shader->SetColour(param, pddiColour(val));
                    }

                    if(param == PDDI_SP_DIFFUSE)
                    {
                        shader->SetColour(PDDI_SP_AMBIENT, pddiColour(val));
                    }

                }
                break;

            case Pure3D::Shader::VECTOR_PARAM:
                {
                    if(!shader)
                        shader = new tShader(shaderName);

                    unsigned param = f->GetInt();

                    pddiVector v;
                    v.x = f->GetFloat();
                    v.y = f->GetFloat();
                    v.z = f->GetFloat();

                    shader->SetVector(param, v);
                }
                break;

            case Pure3D::Shader::MATRIX_PARAM:
                {
                    if(!shader)
                        shader = new tShader(shaderName);

                    unsigned param = f->GetInt();

                    pddiMatrix m;
                    f->GetData(&m, 16, tFile::DWORD);
                    shader->SetMatrix(param, m);
                }
                break;

        }

        f->EndChunk();
    }
    if(!shader)
        shader = new tShader(shaderName);

    // nv:  fix the solid chainlink fences
    if( foundEvilChainlink )
    {
        shader->SetFloat( PDDI_SP_MIPMAP_SHARPNESS, 0.0f );
    }
    shader->mTranslucent = HasTranslucency;
    shader->SetName(name);
#ifdef RAD_LINUX
    pglSetMaterialName(shader->GetShader(), name, f->GetFullFilename());    // for user material shaders (pddi/gl/glcustom.cpp)
    ApplyModOverrideTexture(shader, store);
    ApplyModShaderOverrides(shader, name, store);
#endif
    return shader;
}


bool tShaderDefinitionLoader::CheckChunkID(unsigned i)
{
    return i == Pure3D::Shader::SHADER_DEFINITION;
}

tLoadStatus tShaderDefinitionLoader::Load(tChunkFile* f, tEntityStore* store)
{
    P3DASSERT(f->GetCurrentID() == Pure3D::Shader::SHADER_DEFINITION);

    char defName[256];
    f->GetString(defName);

    int size = f->GetInt();
    char* buffer = (char*)p3d::MallocTemp(size+1);
    f->GetData(buffer, size, tFile::BYTE);
    buffer[size] = 0;

    p3d::device->AddCustomShader(defName, buffer);

    p3d::FreeTemp(buffer);

    return LOAD_OK;
}

