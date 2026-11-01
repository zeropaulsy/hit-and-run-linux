//=============================================================================
// Linux port: Vulkan renderer device (as pglDevice, gl/gldev.hpp)
//=============================================================================

#ifndef _VKDEV_HPP_
#define _VKDEV_HPP_

#include <pddi/pddi.hpp>

class pvkDevice : public pddiDevice
{
public:
    pvkDevice();
    ~pvkDevice();

    void        GetLibraryInfo(pddiLibInfo* info);
    const char* GetDeviceDescription();
    int         GetDisplayInfo(pddiDisplayInfo** info);
    unsigned    GetCaps();

    void SetCurrentContext(pddiRenderContext* context);
    pddiRenderContext* GetCurrentContext();

    pddiDisplay* NewDisplay(int id);
    pddiRenderContext* NewRenderContext(pddiDisplay* display);
    pddiTexture* NewTexture(pddiTextureDesc* desc);
    pddiPrimBuffer* NewPrimBuffer(pddiPrimBufferDesc* desc);
    pddiShader* NewShader(const char* name, const char* aux = NULL);

    void AddCustomShader(const char* name, const char* aux = NULL);
    void Release(void);

protected:
    pddiRenderContext* context;
};

// the Vulkan pddi (the platform layer calls it instead of pddiCreate when the window is a Vulkan one)
int pddiCreateVulkan(int versionMajor, int versionMinor, pddiDevice** device);

#endif
