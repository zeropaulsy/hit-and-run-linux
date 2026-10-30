//=============================================================================
// Linux port: Vulkan renderer textures (as pglTexture, gltex.cpp)
//=============================================================================

#ifndef _VKTEX_HPP_
#define _VKTEX_HPP_

#include <pddi/pddi.hpp>
#include <pddi/pdditype.hpp>
#include <pddi/vk/vkcore.hpp>

class pvkContext;

// The pixels stay in CPU memory as in the GL pddi (Lock hands them out bottom-up, DXT blocks as stored); the image is
// made from them on the render thread when the texture is first drawn after a Create or Unlock (Prepare). Rows go up
// in memory order, as GL took them: the game's UVs sample the same texels in both renderers.
class pvkTexture : public pddiTexture
{
public:
    pvkTexture(pvkContext*);
    ~pvkTexture();

    bool Create(int xSize, int ySize, int bpp, int alphaDepth, int nMip, pddiTextureType type = PDDI_TEXTYPE_RGB,
                pddiTextureUsageHint usageHint = PDDI_USAGE_STATIC);

    pddiPixelFormat GetPixelFormat();
    int GetWidth();
    int GetHeight();
    int GetDepth();
    int GetNumMipMaps();
    int GetAlphaDepth();

    int GetNumPaletteEntries(void);
    void SetPalette(int nEntries, pddiColour* palette);
    int GetPalette(pddiColour* palette);

    pddiLockInfo* Lock(int mipLevel, pddiRect* rect = 0);
    void Unlock(int mipLevel);

    void Prefetch(void);
    void Discard(void);
    void SetPriority(int priority);
    int GetPriority();

    // render thread: the image to sample, uploaded first when the pixels changed (NULL when it cannot be made)
    VkImageView Prepare(void);
    bool HasMipmaps(void) const { return image.mips > 1; }

protected:
    int MipBytes(int mip);

    pvkContext* context;
    int xSize, ySize;
    pddiTextureType type;
    int nMipMap;
    int priority;
    pddiLockInfo lock;
    char** bits;
    unsigned compressedFormat;  // the DXT kind (PGL_COMPRESSED_* as gltex.cpp), 0 for 32-bit ARGB
    int blockBytes;

    pvkImage image;
    volatile bool dirty;        // set by Unlock (any thread), cleared by Prepare
};

// texture-filtering settings, shared with the GL pddi (gltex.cpp: simpsons.ini mipmaps=, anisotropy=)
extern bool g_pglMipmaps;
extern float g_pglAnisotropy;

// gltex.cpp's software DXT decoder (drivers without BC formats)
void pglDecodeDXT(const unsigned char* blocks, int width, int height, unsigned format, unsigned* pixels);

#endif
