//=============================================================================
// Linux port: Vulkan renderer textures (see vktex.hpp; the pddi side follows gltex.cpp)
//=============================================================================

#include <pddi/vk/vktex.hpp>
#include <pddi/vk/vkcon.hpp>

#include <pddi/base/debug.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// the DXT kinds, named as gltex.cpp's (GL_EXT_texture_compression_s3tc values)
#define PGL_COMPRESSED_RGBA_S3TC_DXT1 0x83F1
#define PGL_COMPRESSED_RGBA_S3TC_DXT3 0x83F2
#define PGL_COMPRESSED_RGBA_S3TC_DXT5 0x83F3

static int Log2(int x)
{
    int r = 0;
    while((1 << r) < x)
        r++;
    return (1 << r) == x ? r : -1;
}

pvkTexture::pvkTexture(pvkContext* c)
{
    context = c;
    xSize = ySize = 0;
    type = PDDI_TEXTYPE_RGB;
    nMipMap = 0;
    priority = 15;
    memset(&lock, 0, sizeof(lock));
    bits = NULL;
    compressedFormat = 0;
    blockBytes = 0;
    memset(&image, 0, sizeof(image));
    dirty = true;
}

pvkTexture::~pvkTexture()
{
    if(bits)
    {
        for(int i = 0; i < nMipMap + 1; i++)
            delete [] bits[i];
        delete [] bits;
    }
    if(g_pvk)
        g_pvk->Retire(image);

    if(compressedFormat)
    {
        int total = 0;
        for(int i = 0; i < nMipMap + 1; i++)
            total += MipBytes(i);
        context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_DXTN, -(float)total / 1024);
        context->ADD_STAT(PDDI_STAT_TEXTURE_COUNT_DXTN, -1);
        return;
    }
    context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_32BIT, -(float)((xSize * ySize * 4) / 1024));
    context->ADD_STAT(PDDI_STAT_TEXTURE_COUNT_32BIT, -1);
}

int pvkTexture::MipBytes(int mip)
{
    int w = xSize >> mip, h = ySize >> mip;
    if(w < 1) w = 1;
    if(h < 1) h = 1;
    return ((w + 3) / 4) * ((h + 3) / 4) * blockBytes;
}

bool pvkTexture::Create(int x, int y, int bpp, int alphaDepth, int nMip, pddiTextureType ty, pddiTextureUsageHint usageHint)
{
    xSize = x;
    ySize = y;
    nMipMap = nMip;
    type = PDDI_TEXTYPE_RGB;

    // the D3D8 pddi's power-of-two rule for art; dynamic textures (mods' video textures) may be any size
    bool dxt = ty == PDDI_TEXTYPE_DXT1 || ty == PDDI_TEXTYPE_DXT2 || ty == PDDI_TEXTYPE_DXT3 ||
               ty == PDDI_TEXTYPE_DXT4 || ty == PDDI_TEXTYPE_DXT5;
    bool anySize = usageHint == PDDI_USAGE_DYNAMIC && !dxt;
    if((Log2(xSize) < 0 || Log2(ySize) < 0) && !anySize)
    {
        fprintf(stderr, "pvkTexture: rejected %dx%d texture (not a power of two)\n", xSize, ySize);
        lastError = PDDI_TEX_NOT_POW_2;
        return false;
    }
    int maxSize = g_pvk ? (int)g_pvk->properties.limits.maxImageDimension2D : 2048;
    if(xSize > maxSize || ySize > maxSize)
    {
        lastError = PDDI_TEX_TOO_BIG;
        return false;
    }

    switch(ty)
    {
        case PDDI_TEXTYPE_DXT1: compressedFormat = PGL_COMPRESSED_RGBA_S3TC_DXT1; lock.format = PDDI_PIXEL_DXT1; break;
        case PDDI_TEXTYPE_DXT2: compressedFormat = PGL_COMPRESSED_RGBA_S3TC_DXT3; lock.format = PDDI_PIXEL_DXT2; break;
        case PDDI_TEXTYPE_DXT3: compressedFormat = PGL_COMPRESSED_RGBA_S3TC_DXT3; lock.format = PDDI_PIXEL_DXT3; break;
        case PDDI_TEXTYPE_DXT4: compressedFormat = PGL_COMPRESSED_RGBA_S3TC_DXT5; lock.format = PDDI_PIXEL_DXT4; break;
        case PDDI_TEXTYPE_DXT5: compressedFormat = PGL_COMPRESSED_RGBA_S3TC_DXT5; lock.format = PDDI_PIXEL_DXT5; break;
        default: compressedFormat = 0; break;
    }
    if(compressedFormat)
    {
        type = ty;
        blockBytes = compressedFormat == PGL_COMPRESSED_RGBA_S3TC_DXT1 ? 8 : 16;
        bits = new char*[nMipMap + 1];
        int total = 0;
        for(int i = 0; i < nMipMap + 1; i++)
        {
            bits[i] = new char[MipBytes(i)];
            memset(bits[i], 0, MipBytes(i));
            total += MipBytes(i);
        }
        lock.depth = blockBytes == 8 ? 4 : 8;
        lock.native = true;
        context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_DXTN, (float)total / 1024);
        context->ADD_STAT(PDDI_STAT_TEXTURE_COUNT_DXTN, 1);
        return true;
    }

    bits = new char*[nMipMap + 1];
    for(int i = 0; i < nMipMap + 1; i++)
    {
        int w = xSize >> i, h = ySize >> i;
        bits[i] = new char[(w < 1 ? 1 : w) * (h < 1 ? 1 : h) * 4];
    }

    // 0xAARRGGBB words: B, G, R, A bytes, VK_FORMAT_B8G8R8A8_UNORM
    lock.depth = 32;
    lock.format = PDDI_PIXEL_ARGB8888;
    lock.native = true;
    for(int c = 0; c < 4; c++)
        lock.rgbaLShift[c] = lock.rgbaRShift[c] = 0;
    lock.rgbaMask[0] = 0x00ff0000;
    lock.rgbaMask[1] = 0x0000ff00;
    lock.rgbaMask[2] = 0x000000ff;
    lock.rgbaMask[3] = 0xff000000;

    context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_32BIT, (float)((xSize * ySize * 4) / 1024));
    context->ADD_STAT(PDDI_STAT_TEXTURE_COUNT_32BIT, 1);
    return true;
}

pddiPixelFormat pvkTexture::GetPixelFormat()
{
    return compressedFormat ? lock.format : PDDI_PIXEL_ARGB8888;
}

int pvkTexture::GetWidth()       { return xSize; }
int pvkTexture::GetHeight()      { return ySize; }
int pvkTexture::GetDepth()       { return compressedFormat ? lock.depth : 32; }
int pvkTexture::GetNumMipMaps()  { return nMipMap; }
int pvkTexture::GetAlphaDepth()  { return 8; }

pddiLockInfo* pvkTexture::Lock(int mipMap, pddiRect* rect)
{
    PDDIASSERT(mipMap <= nMipMap);
    lock.width = xSize >> mipMap;
    lock.height = ySize >> mipMap;
    if(lock.width < 1) lock.width = 1;
    if(lock.height < 1) lock.height = 1;
    if(compressedFormat)
    {
        // as D3D8's FillLockInfo for DXT: the blocks as stored
        lock.pitch = ((lock.width + 3) / 4) * blockBytes;
        lock.bits = bits[mipMap];
        return &lock;
    }
    // bottom-up, as the GL pddi
    lock.pitch = -lock.width * 4;
    lock.bits = bits[mipMap] + (lock.width * (lock.height - 1) * 4);
    return &lock;
}

void pvkTexture::Unlock(int mipLevel)
{
    dirty = true;       // a new image from bits[] at the next Prepare (render thread)
}

void pvkTexture::SetPriority(int p)  { priority = p; }
int pvkTexture::GetPriority(void)    { return priority; }
void pvkTexture::Prefetch(void)      {}
void pvkTexture::Discard(void)       {}
int pvkTexture::GetNumPaletteEntries(void)                  { return 0; }
void pvkTexture::SetPalette(int nEntries, pddiColour* p)    {}
int pvkTexture::GetPalette(pddiColour* palette)             { return 0; }

// Uploads go into the frame's upload command buffer, which runs before its draws. The old image is retired, not
// overwritten: frames in flight may still sample it (video textures change every frame).
VkImageView pvkTexture::Prepare(void)
{
    if(!dirty && image.image)
        return image.view;
    if(!g_pvk || !bits)
        return VK_NULL_HANDLE;
    dirty = false;
    g_pvk->Retire(image);

    bool bc = compressedFormat && g_pvk->bcTextures;
    VkFormat format = VK_FORMAT_B8G8R8A8_UNORM;
    if(bc)
        format = compressedFormat == PGL_COMPRESSED_RGBA_S3TC_DXT1 ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK :
                 compressedFormat == PGL_COMPRESSED_RGBA_S3TC_DXT3 ? VK_FORMAT_BC2_UNORM_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;

    // the first level only, as the GL pddi; uncompressed images get a full mip chain (texture-filtering setting)
    int mips = 1;
    if(!bc)
    {
        while((xSize >> mips) > 0 || (ySize >> mips) > 0)
            mips++;
    }
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                              (mips > 1 ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0);
    if(!g_pvk->CreateImage(xSize, ySize, mips, format, VK_SAMPLE_COUNT_1_BIT, usage, VK_IMAGE_ASPECT_COLOR_BIT, &image))
    {
        static bool s_reported = false;
        if(!s_reported)
            fprintf(stderr, "PDDI: out of video memory: textures are missing (close other programs using the GPU or "
                            "lower the resolution)\n");
        s_reported = true;
        dirty = true;       // retried on the next use
        return VK_NULL_HANDLE;
    }

    VkDeviceSize size = bc ? (VkDeviceSize)MipBytes(0) : (VkDeviceSize)xSize * ySize * 4;
    VkBuffer staging;
    VkDeviceSize offset;
    void* dst = g_pvk->Transient(size, 16, &staging, &offset);
    if(!dst)
    {
        g_pvk->Retire(image);
        dirty = true;
        return VK_NULL_HANDLE;
    }
    if(compressedFormat && !bc)
        pglDecodeDXT((const unsigned char*)bits[0], xSize, ySize, compressedFormat, (unsigned*)dst);
    else
        memcpy(dst, bits[0], size);

    VkCommandBuffer cmd = g_pvk->UploadCmd();
    g_pvk->Transition(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkBufferImageCopy copy;
    memset(&copy, 0, sizeof(copy));
    copy.bufferOffset = offset;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent.width = xSize;
    copy.imageExtent.height = ySize;
    copy.imageExtent.depth = 1;
    vkCmdCopyBufferToImage(cmd, staging, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    // the mip chain, each level blitted from the one above
    for(int level = 1; level < mips; level++)
    {
        VkImageMemoryBarrier2 b = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2 };
        b.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        b.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image.image;
        b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b.subresourceRange.baseMipLevel = level - 1;
        b.subresourceRange.levelCount = 1;
        b.subresourceRange.layerCount = 1;
        VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &b;
        vkCmdPipelineBarrier2(cmd, &dep);

        int sw = xSize >> (level - 1), sh = ySize >> (level - 1);
        int dw = xSize >> level, dh = ySize >> level;
        VkImageBlit blit;
        memset(&blit, 0, sizeof(blit));
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.mipLevel = level - 1;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[1].x = sw < 1 ? 1 : sw;
        blit.srcOffsets[1].y = sh < 1 ? 1 : sh;
        blit.srcOffsets[1].z = 1;
        blit.dstSubresource = blit.srcSubresource;
        blit.dstSubresource.mipLevel = level;
        blit.dstOffsets[1].x = dw < 1 ? 1 : dw;
        blit.dstOffsets[1].y = dh < 1 ? 1 : dh;
        blit.dstOffsets[1].z = 1;
        vkCmdBlitImage(cmd, image.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image.image,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
    }

    // levels above the last are TRANSFER_SRC now, the last TRANSFER_DST: both to shader reads
    VkImageMemoryBarrier2 b[2];
    for(int i = 0; i < 2; i++)
    {
        memset(&b[i], 0, sizeof(b[i]));
        b[i].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        b[i].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
        b[i].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        b[i].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        b[i].dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        b[i].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        b[i].srcQueueFamilyIndex = b[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b[i].image = image.image;
        b[i].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        b[i].subresourceRange.layerCount = 1;
    }
    b[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    b[0].subresourceRange.baseMipLevel = 0;
    b[0].subresourceRange.levelCount = mips - 1;
    b[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    b[1].subresourceRange.baseMipLevel = mips - 1;
    b[1].subresourceRange.levelCount = 1;
    VkDependencyInfo dep = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO };
    dep.imageMemoryBarrierCount = mips > 1 ? 2 : 1;
    dep.pImageMemoryBarriers = mips > 1 ? b : &b[1];
    vkCmdPipelineBarrier2(cmd, &dep);
    image.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    return image.view;
}
