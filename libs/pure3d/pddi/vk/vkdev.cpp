//=============================================================================
// Linux port: Vulkan renderer device (see vkdev.hpp; as gldev.cpp)
//=============================================================================

#include <pddi/vk/vkdev.hpp>
#include <pddi/vk/vkcon.hpp>
#include <pddi/vk/vkdisplay.hpp>
#include <pddi/vk/vkmat.hpp>
#include <pddi/vk/vktex.hpp>

#include <pddi/base/debug.hpp>
#include <shar_mods.h>
#include <stdio.h>
#include <string.h>

#define PDDI_VK_BUILD 1

static pvkDevice gblDevice;
pddiDisplayInfo pvkDisplayInfo[1];
static pddiModeInfo displayModes[1024];

int pddiCreateVulkan(int versionMajor, int versionMinor, pddiDevice** device)
{
    if((versionMajor != PDDI_VERSION_MAJOR) || (versionMinor != PDDI_VERSION_MINOR))
    {
        *device = NULL;
        return PDDI_VERSION_ERROR;
    }
    *device = &gblDevice;
    return PDDI_OK;
}

pvkDevice::pvkDevice()
{
    context = NULL;
}

pvkDevice::~pvkDevice()
{
}

void pvkDevice::GetLibraryInfo(pddiLibInfo* info)
{
    info->versionMajor = PDDI_VERSION_MAJOR;
    info->versionMinor = PDDI_VERSION_MINOR;
    info->versionBuild = PDDI_VK_BUILD;
    info->libID = PDDI_LIBID_OPENGL;        // the closest the pddi has; nothing tests it
}

unsigned pvkDevice::GetCaps()
{
    return 0;
}

unsigned pglFillDisplayModes(pddiModeInfo* modes);     // as the GL pddi lists them (gldisplay.cpp)

int pvkDevice::GetDisplayInfo(pddiDisplayInfo** info)
{
    static bool generated = false;
    *info = pvkDisplayInfo;
    if(generated)
        return 1;
    generated = true;
    pvkDisplayInfo[0].id = 0;
    strcpy(pvkDisplayInfo[0].description, "Vulkan Device");
    pvkDisplayInfo[0].pci = 0;
    pvkDisplayInfo[0].vendor = 0;
    pvkDisplayInfo[0].fullscreenOnly = false;
    pvkDisplayInfo[0].caps = 0;
    pvkDisplayInfo[0].nDisplayModes = pglFillDisplayModes(displayModes);
    pvkDisplayInfo[0].modeInfo = displayModes;
    return 1;
}

const char* pvkDevice::GetDeviceDescription()
{
    return "Vulkan";
}

void pvkDevice::SetCurrentContext(pddiRenderContext* c)
{
    context = c;
}

pddiRenderContext* pvkDevice::GetCurrentContext(void)
{
    return context;
}

pddiDisplay* pvkDevice::NewDisplay(int id)
{
    pddiDisplayInfo* dummy;
    GetDisplayInfo(&dummy);
    PDDIASSERT(id < 1);
    pvkDisplay* display = new pvkDisplay;
    if(display->GetLastError() != PDDI_OK)
    {
        delete display;
        return NULL;
    }
    return display;
}

pddiRenderContext* pvkDevice::NewRenderContext(pddiDisplay* display)
{
    pvkContext* c = new pvkContext(this, (pvkDisplay*)display);
    if(c->GetLastError() != PDDI_OK)
    {
        delete c;
        return NULL;
    }
    return c;
}

pddiTexture* pvkDevice::NewTexture(pddiTextureDesc* desc)
{
    pvkTexture* tex = new pvkTexture((pvkContext*)context);
    if(!tex->Create(desc->GetSizeX(), desc->GetSizeY(), desc->GetBitDepth(), desc->GetAlphaDepth(),
                    desc->GetMipMapCount(), desc->GetType(), desc->GetUsage()))
    {
        lastError = tex->GetLastError();
        delete tex;
        return NULL;
    }
    return tex;
}

// shader names as gldev.cpp (the D3D8 pddi's table)
static pddiBaseShader* AllocSimpleShader(pddiRenderContext* c, const char*, const char*)     { return new pvkMat((pvkContext*)c); }
static pddiBaseShader* AllocReflectionShader(pddiRenderContext* c, const char*, const char*) { return new pvkReflectionMat((pvkContext*)c); }
static pddiBaseShader* AllocErrorShader(pddiRenderContext* c, const char*, const char*)      { return new pvkErrorMat((pvkContext*)c); }
static pddiBaseShader* AllocRefractionShader(pddiRenderContext* c, const char*, const char*) { return new pvkRefractionMat((pvkContext*)c); }

static void pvkInstallShaders()
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
    if(shar_ModHackRequired("RefractionShaderSupport"))
        pddiBaseShader::InstallShader("refract", AllocRefractionShader, NULL);
}

pddiShader* pvkDevice::NewShader(const char* name, const char* aux)
{
    pvkInstallShaders();
    pddiBaseShader* mat = pddiBaseShader::AllocateShader(context, name, aux);
    if(mat->GetLastError() != PDDI_OK)
    {
        delete mat;
        return NULL;
    }
    ((pvkMat*)mat)->SetTypeName(name);
    return mat;
}

pddiPrimBuffer* pvkDevice::NewPrimBuffer(pddiPrimBufferDesc* desc)
{
    return new pvkPrimBuffer((pvkContext*)context, desc->GetPrimType(), desc->GetVertexFormat(), desc->GetVertexCount(),
                             desc->GetIndexCount());
}

void pvkDevice::AddCustomShader(const char* name, const char* aux)
{
}

void pvkDevice::Release(void)
{
}
