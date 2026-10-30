//=============================================================================
// Linux port: Vulkan renderer context and prim buffers (as pglContext, glcon.hpp)
//=============================================================================

#ifndef _VKCON_HPP_
#define _VKCON_HPP_

#include <pddi/pddi.hpp>
#include <pddi/base/basecontext.hpp>
#include <pddi/vk/vkpipeline.hpp>

class pvkDisplay;
class pvkDevice;
class pvkExtGamma;

class pvkContext : public pddiBaseContext
{
public:
    pvkContext(pvkDevice* dev, pvkDisplay* disp);
    ~pvkContext();

    void BeginFrame();
    void EndFrame();
    void Clear(unsigned bufferMask);
    void SetScissor(pddiRect* rect);

    pddiPrimStream* BeginPrims(pddiShader* material, pddiPrimType primType, unsigned vertexType, int vertexCount = 0, unsigned pass = 0);
    void EndPrims(pddiPrimStream* stream);
    void DrawPrimBuffer(pddiShader* material, pddiPrimBuffer* buffer);

    int GetMaxLights();
    void SetAmbientLight(pddiColour col);

    void SetCullMode(pddiCullMode mode);
    void SetMaterialCull(int cull);     // PVK_CULL_*, as pglContext::SetMaterialCull

    void EnableZBuffer(bool enable);
    void SetZCompare(pddiCompareMode compareMode);
    void SetZWrite(bool);
    void SetColourWrite(bool red, bool green, bool blue, bool alpha);
    void SetZBias(float bias);
    void SetZRange(float n, float f);

    void EnableStencilBuffer(bool enable);
    void SetStencilCompare(pddiCompareMode compare);
    void SetStencilRef(int ref);
    void SetStencilMask(unsigned mask);
    void SetStencilWriteMask(unsigned mask);
    void SetStencilOp(pddiStencilOp failOp, pddiStencilOp zFailOp, pddiStencilOp zPassOp);

    void SetFillMode(pddiFillMode mode);

    void EnableFog(bool enable);
    void SetFog(pddiColour colour, float start, float end);

    int GetMaxTextureDimension(void);

    pddiExtension* GetExtension(unsigned extID);
    bool VerifyExtension(unsigned extID);

    pvkDisplay* GetDisplay(void) { return display; }
    pvkPipeline* GetPipeline(void) { return pipeline; }

protected:
    void LoadHardwareMatrix(pddiMatrixType id);
    void SetupHardwareProjection(void);
    void SetupHardwareLight(int);
    void BeginTiming(void);
    float EndTiming(void);
    void ApplyCull(void);

    pvkDevice* device;
    pvkDisplay* display;
    pvkPipeline* pipeline;
    int materialCull;
    pvkExtGamma* extGamma;
    pddiShader* defaultShader;
};

class pvkPrimBufferStream;

// as pglPrimBuffer: the arrays in CPU memory (filled on loader threads), copied to a device buffer by Display when
// they changed: positions, then the normals, UVs and colours the format has, then the indices
class pvkPrimBuffer : public pddiPrimBuffer
{
public:
    pvkPrimBuffer(pvkContext* context, pddiPrimType type, unsigned vertexFormat, int nVertex, int nIndex);
    ~pvkPrimBuffer();

    pddiPrimBufferStream* Lock();
    void Unlock(pddiPrimBufferStream* stream);
    unsigned char* LockIndexBuffer();
    void UnlockIndexBuffer(int count);
    void SetIndices(unsigned short* indices, int count);

    bool CheckMemImageVersion(int version) { return false; }
    void* LockMemImage(unsigned) { return NULL; }
    void UnlockMemImage() { }
    unsigned GetMemImageLength() { return 0; }
    void SetMemImageParam(unsigned param, unsigned value) { }

    void SetUsedSize(int size) { useSize = size > 0 ? (unsigned)size : 0; }

    void Display(void);

protected:
    bool Upload(void);

    friend class pvkPrimBufferStream;
    pvkPrimBufferStream* stream;
    pvkContext* context;
    pddiPrimType primType;
    unsigned vertexType;

    float* coord;
    float* normal;
    float* uv;
    unsigned char* colour;
    unsigned allocated;
    unsigned total;
    unsigned short* indices;
    unsigned indexCount;
    unsigned indexUsed;
    unsigned useSize;
    float* coordBase;
    float* normalBase;
    float* uvBase;
    unsigned char* colourBase;

    pvkBuffer buffer;
    VkDeviceSize normalOffset, uvOffset, colourOffset, indexOffset;
    volatile bool changed;
    float mem;
};

#endif
