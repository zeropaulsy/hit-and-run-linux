//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include <pddi/gl/gldev.hpp>
#include <pddi/gl/glcon.hpp>
#include <pddi/gl/gltex.hpp>
#include <pddi/gl/glmat.hpp>

#if defined(RAD_WIN32) && !defined(RAD_LINUX)
    #include <pddi/gl/display_win32/gldisplay.hpp>
    #include <pddi/gl/display_win32/gl.hpp>
#endif

#ifdef RAD_LINUX
    #include <pddi/gl/display_linux/gldisplay.hpp>
    #include <pddi/gl/display_linux/gl.hpp>
#endif


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <shar_mods.h>
// #include <io.h>
#include <pddi/base/debug.hpp>

#define PDDI_GL_BUILD 36

static pglDevice gblDevice;
static int nDisplays;
pddiDisplayInfo displayInfo[2];
static pddiModeInfo    displayModes[1024];

char libName [] = "OpenGL";

DLLEXPORT int pddiCreate(int versionMajor, int versionMinor, pddiDevice** device)
{
    if((versionMajor != PDDI_VERSION_MAJOR) || (versionMinor != PDDI_VERSION_MINOR))
    {
        *device = NULL;
        return PDDI_VERSION_ERROR;
    }

    *device = &gblDevice;
    return PDDI_OK;
}

//--------------------------------------------------------------
pglDevice::pglDevice() 
{
}

//--------------------------------------------------------------
pglDevice::~pglDevice()
{
}

//--------------------------------------------------------------
void pglDevice::GetLibraryInfo(pddiLibInfo* info)
{
    info->versionMajor = PDDI_VERSION_MAJOR;
    info->versionMinor = PDDI_VERSION_MINOR;
    info->versionBuild = PDDI_GL_BUILD;
    info->libID = PDDI_LIBID_OPENGL;
}

unsigned pglDevice::GetCaps()
{
    return 0;
}

int pglDevice::GetDisplayInfo(pddiDisplayInfo** info)
{
    static int generatedInfo = 0;

    *info = displayInfo;

    if(generatedInfo)
    {
        return nDisplays;
    }

    nDisplays = 1;
    generatedInfo = 1;

    displayInfo[0].id = 0;
    strcpy(displayInfo[0].description,"OpenGL Device");
    displayInfo[0].pci = 0;
    displayInfo[0].vendor = 0;
    displayInfo[0].fullscreenOnly = false;
    displayInfo[0].caps = 0;

    displayInfo[0].nDisplayModes = pglDisplay::FillDisplayModes(displayModes);
    displayInfo[0].modeInfo = displayModes;

    return nDisplays;
}

const char* pglDevice::GetDeviceDescription()
{
    return libName;
}

void pglDevice::SetCurrentContext(pddiRenderContext* c)
{
    context = c;
}

pddiRenderContext* pglDevice::GetCurrentContext(void)
{
    return context;
}

pddiDisplay *pglDevice::NewDisplay(int id)
{
    pddiDisplayInfo* dummy;
    GetDisplayInfo(&dummy);

    PDDIASSERT(id < nDisplays);
    pglDisplay* display = new pglDisplay;

    if(display->GetLastError() != PDDI_OK)
    {
        delete display;
        return NULL;
    }

    return (pddiDisplay *)display;
}
//--------------------------------------------------------------
pddiRenderContext *pglDevice::NewRenderContext(pddiDisplay* display)
{
    pglContext* context = new pglContext(this, (pglDisplay*)display);

    if(context->GetLastError() != PDDI_OK)
    {
        delete context;
        return NULL;
    }
    return context;
}

//--------------------------------------------------------------
pddiTexture* pglDevice::NewTexture(pddiTextureDesc* desc)
{
    pglTexture* tex = new pglTexture((pglContext*)context);
    if(!tex->Create(desc->GetSizeX(), desc->GetSizeY(), desc->GetBitDepth(), 
                     desc->GetAlphaDepth(), desc->GetMipMapCount(),desc->GetType(),desc->GetUsage()))
    {
        lastError = tex->GetLastError();
        delete tex;
        return NULL;
    }
    return tex;
}
//--------------------------------------------------------------
// Linux port: shader names map to shader kinds as in the D3D8 pddi (dx8/shaders/shadersetup.cpp), which installed
// them in pddiBaseShader's table; an unknown name gets the first one ("simple"). The retail data uses simple,
// spheremap (car paint) and environment. The D3D8 pddi's lightmap, layered, layeredlmap, projtex, pointsprite and
// toon/hctune shaders are not ported (nothing in the game's data or code asks for them): they draw as simple.
static pddiBaseShader* AllocSimpleShader(pddiRenderContext* c, const char*, const char*)     { return new pglMat((pglContext*)c); }
static pddiBaseShader* AllocReflectionShader(pddiRenderContext* c, const char*, const char*) { return new pglReflectionMat((pglContext*)c); }
static pddiBaseShader* AllocErrorShader(pddiRenderContext* c, const char*, const char*)      { return new pglErrorMat((pglContext*)c); }
static pddiBaseShader* AllocRefractionShader(pddiRenderContext* c, const char*, const char*) { return new pglRefractionMat((pglContext*)c); }

static void pglInstallShaders()
{
    static bool installed = false;
    if(installed)
        return;
    installed = true;
    pddiBaseShader::InstallShader("simple",      AllocSimpleShader, NULL);
    pddiBaseShader::InstallShader("lightweight", AllocSimpleShader, NULL);
    pddiBaseShader::InstallShader("error",       AllocErrorShader, NULL);
    pddiBaseShader::InstallShader("environment", AllocReflectionShader, NULL);
    pddiBaseShader::InstallShader("spheremap",   AllocReflectionShader, NULL);
    // the PS2's refraction: on the PC (D3D8) it drew as simple, as it does here unless a mod asks for it
    if(shar_ModHackRequired("RefractionShaderSupport"))
        pddiBaseShader::InstallShader("refract", AllocRefractionShader, NULL);
}

pddiShader *pglDevice::NewShader(const char* name, const char* aux) 
{ 
    pglInstallShaders();
    pddiBaseShader* mat = pddiBaseShader::AllocateShader(context, name, aux);
    if(mat->GetLastError() != PDDI_OK) {
        delete mat;
        return NULL;
    }
    ((pglMat*)mat)->SetTypeName(name);     // for user material shaders (glcustom.cpp)

    return mat;
}

pddiPrimBuffer *pglDevice::NewPrimBuffer(pddiPrimBufferDesc* desc) 
{ 
    return new pglPrimBuffer((pglContext*)context, desc->GetPrimType(), desc->GetVertexFormat(), desc->GetVertexCount(), desc->GetIndexCount());;
}

void pglDevice::AddCustomShader(const char* name, const char* aux)
{
}

void pglDevice::Release(void)
{
}

