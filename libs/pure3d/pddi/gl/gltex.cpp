//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include <pddi/gl/gltex.hpp>
#include <pddi/gl/glcon.hpp>

#if defined(RAD_WIN32) && !defined(RAD_LINUX)
    #include <pddi/gl/display_win32/gldisplay.hpp>
    #include <pddi/gl/display_win32/gl.hpp>
#endif

#ifdef RAD_LINUX
    #include <pddi/gl/display_linux/gldisplay.hpp>
    #include <pddi/gl/display_linux/gl.hpp>
#endif

#include <math.h>
#include <pddi/base/debug.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef RAD_LINUX
// Pure3D creates and edits textures on its loader thread (fine with Direct3D 8). Uploads happen lazily in SetGLState
// (render thread); the max texture size is cached at display init; stale GL textures are deleted through
// pglDeleteTexture, which queues them for the render thread where no context is current (glpipeline.cpp).
#include <pddi/gl/glpipeline.hpp>
#include <SDL2/SDL.h>

int g_pglMaxTextureSize = 0;                  // set by pglDisplay::InitDisplay on the render thread
bool g_pglTextureS3TC = false;               // GL_EXT_texture_compression_s3tc, set there too
float g_pglMaxAnisotropy = 0.0f;             // GL_*_texture_filter_anisotropic's limit (0: none), set there too

// texture-filtering settings (simpsons.ini mipmaps=, anisotropy=; pglSetTextureFiltering in gldisplay.cpp)
bool g_pglMipmaps = true;
float g_pglAnisotropy = 16.0f;

#define PGL_TEXTURE_MAX_ANISOTROPY 0x84FE    // GL_TEXTURE_MAX_ANISOTROPY (GL 4.6, ARB/EXT_texture_filter_anisotropic)

// from GL_EXT_texture_compression_s3tc (not in glcorearb.h)
#define PGL_COMPRESSED_RGBA_S3TC_DXT1 0x83F1
#define PGL_COMPRESSED_RGBA_S3TC_DXT3 0x83F2
#define PGL_COMPRESSED_RGBA_S3TC_DXT5 0x83F3

//-------------------------------------------------------------------
// Software DXT1/3/5 decoder, for drivers without S3TC (DXT2/4 are the premultiplied variants and decode like 3/5).
// Output pixels are 0xAARRGGBB, rows in block order (the art stores DXT images bottom row first, as GL samples).
static unsigned Expand565(unsigned short c)
{
    unsigned r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    return ((r << 3 | r >> 2) << 16) | ((g << 2 | g >> 4) << 8) | (b << 3 | b >> 2);
}

static unsigned Lerp(unsigned a, unsigned b, int wa, int wb, int div)
{
    unsigned out = 0;
    for(int shift = 0; shift <= 16; shift += 8)
    {
        unsigned ca = (a >> shift) & 255, cb = (b >> shift) & 255;
        out |= ((ca * wa + cb * wb) / div) << shift;
    }
    return out;
}

static void DecodeDXT(const unsigned char* blocks, int width, int height, unsigned format, unsigned* pixels)
{
    bool dxt1 = format == PGL_COMPRESSED_RGBA_S3TC_DXT1;
    bool dxt5 = format == PGL_COMPRESSED_RGBA_S3TC_DXT5;
    int blockBytes = dxt1 ? 8 : 16;
    int bw = (width + 3) / 4, bh = (height + 3) / 4;

    for(int by = 0; by < bh; by++)
    {
        for(int bx = 0; bx < bw; bx++)
        {
            const unsigned char* blk = blocks + (by * bw + bx) * blockBytes;
            unsigned alpha[16];
            if(dxt1)
            {
                for(int i = 0; i < 16; i++) alpha[i] = 255;
            }
            else if(dxt5)
            {
                unsigned a0 = blk[0], a1 = blk[1], apal[8];
                apal[0] = a0; apal[1] = a1;
                if(a0 > a1)
                    for(int k = 1; k < 7; k++) apal[k + 1] = (a0 * (7 - k) + a1 * k) / 7;
                else
                {
                    for(int k = 1; k < 5; k++) apal[k + 1] = (a0 * (5 - k) + a1 * k) / 5;
                    apal[6] = 0;
                    apal[7] = 255;
                }
                unsigned long long abits = 0;
                for(int k = 0; k < 6; k++) abits |= (unsigned long long)blk[2 + k] << (8 * k);
                for(int i = 0; i < 16; i++) alpha[i] = apal[(abits >> (3 * i)) & 7];
            }
            else    // DXT3: explicit 4-bit alpha
            {
                for(int i = 0; i < 16; i++)
                {
                    unsigned a = (blk[i / 2] >> ((i & 1) * 4)) & 15;
                    alpha[i] = a << 4 | a;
                }
            }

            const unsigned char* c = blk + (dxt1 ? 0 : 8);
            unsigned short c0 = c[0] | (c[1] << 8), c1 = c[2] | (c[3] << 8);
            unsigned pal[4];
            pal[0] = Expand565(c0);
            pal[1] = Expand565(c1);
            bool fourColour = !dxt1 || c0 > c1;
            if(fourColour)
            {
                pal[2] = Lerp(pal[0], pal[1], 2, 1, 3);
                pal[3] = Lerp(pal[0], pal[1], 1, 2, 3);
            }
            else
            {
                pal[2] = Lerp(pal[0], pal[1], 1, 1, 2);
                pal[3] = 0;
            }
            unsigned bits = c[4] | (c[5] << 8) | (c[6] << 16) | ((unsigned)c[7] << 24);
            for(int i = 0; i < 16; i++)
            {
                unsigned idx = (bits >> (2 * i)) & 3;
                int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if(x < width && y < height)
                    pixels[y * width + x] = (!fourColour && idx == 3) ? 0 : ((alpha[i] << 24) | pal[idx]);
            }
        }
    }
}

// for the Vulkan pddi (vk/vktex.cpp)
void pglDecodeDXT(const unsigned char* blocks, int width, int height, unsigned format, unsigned* pixels)
{
    DecodeDXT(blocks, width, height, format, pixels);
}

int pglTexture::MipBytes(int mip)
{
    int w = xSize >> mip, h = ySize >> mip;
    if(w < 1) w = 1;
    if(h < 1) h = 1;
    return ((w + 3) / 4) * ((h + 3) / 4) * blockBytes;
}

// the first level, as the uncompressed path (mip maps were never uploaded: the filters do not use them)
void pglTexture::UploadCompressed(void)
{
    if(g_pglTextureS3TC)
    {
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, compressedFormat, xSize, ySize, 0, MipBytes(0), bits[0]);
        return;
    }
    unsigned* pixels = (unsigned*)malloc(xSize * ySize * 4);
    if(!pixels)
        return;
    DecodeDXT((const unsigned char*)bits[0], xSize, ySize, compressedFormat, pixels);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, xSize, ySize, 0, GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    free(pixels);
}
#endif

void pglTexture::SetGLState(void)
{
    if(context->contextID != contextID)
    {
        contextID = context->contextID;
        gltexture = 0;
    }

    if(!gltexture)
    {
        glGenTextures(1,&gltexture);
        glBindTexture(GL_TEXTURE_2D, gltexture);

//      if(nMipMap == 0)
        {
#ifdef RAD_LINUX
            if(compressedFormat)
                UploadCompressed();
            else
#endif
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, xSize,
                ySize, 0, lock.native ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE,
                (GLvoid *)bits[0]);
#ifdef RAD_LINUX
            // Linux port: mip levels for every uncompressed texture (the D3D8 pddi's art had one level and almost
            // every material filtered without mips: distant textures shimmered). Whether they are used is the
            // texture-filtering setting's call, in SetSampling, so it can change at any time.
            hasMipmaps = !compressedFormat;
            if(hasMipmaps)
                glGenerateMipmap(GL_TEXTURE_2D);
            // Linux port: out of video memory the upload fails and the texture drew black for good. Say so once
            // and retry on the next use, so textures appear once memory is freed.
            if(glGetError() == GL_OUT_OF_MEMORY)
            {
                static bool s_reported = false;
                if(!s_reported)
                {
                    fprintf(stderr, "PDDI: out of video memory: textures are missing (close other programs using "
                                    "the GPU or lower the resolution)\n");
                    s_reported = true;
                }
                glDeleteTextures(1, &gltexture);
                gltexture = 0;
            }
#endif
        }
        /*
        else
        {

            int tmpMipMap = nMipMap;
            tmpMipMap--;
            if(tmpMipMap < 0)
                tmpMipMap = 0;

            int i = 0;
            int width = xSize;
            int height = ySize;
            bool bottomed = false;
            bool done = false;

            while(!done)
            {
                char* data = bits[i];
                if(i > tmpMipMap)
                    data = bits[tmpMipMap];

                glTexImage2D(GL_TEXTURE_2D, i, GL_RGBA8, xSize >> i,
                    ySize >> i, 0, lock.native ? GL_BGRA : GL_RGBA, GL_UNSIGNED_BYTE,
                    (GLvoid *)data);

                done = (width == 1) || (height == 1);

                width >>= 1;
                if(width < 1) 
                {
                    width = 1;
                    bottomed = true;
                }

                height >>= 1;
                if(height < 1) 
                {
                    height = 1;
                    bottomed = true;
                }

                i++;
            }
        }
        */
    }
    else
    {
        glBindTexture(GL_TEXTURE_2D, gltexture);
    }
}

int fastlog2(int x)
{
    int r = 0;
    int tmp = x;
    while(tmp > 1)
    {
        r++;
        tmp = tmp >> 1;

        if((tmp << r) != x)
            // not power of 2
            return -1;
    }
    return r;
}

bool pglTexture::Create(int x, int y, int bpp, int alphaDepth, int nMip, pddiTextureType ty, pddiTextureUsageHint usageHint)
{
    xSize = x;
    ySize = y;
    nMipMap = nMip;
    type = PDDI_TEXTYPE_RGB;

    log2X = fastlog2(xSize);
    log2Y = fastlog2(ySize);

#ifdef RAD_LINUX
    // Linux port: GL 3.3 takes any size. Art keeps the D3D8 pddi's power-of-two rule; dynamic textures (mods' video
    // textures: retail movies are 640x480) may be any size.
    bool anySize = usageHint == PDDI_USAGE_DYNAMIC && ty != PDDI_TEXTYPE_DXT1 && ty != PDDI_TEXTYPE_DXT2 &&
                   ty != PDDI_TEXTYPE_DXT3 && ty != PDDI_TEXTYPE_DXT4 && ty != PDDI_TEXTYPE_DXT5;
    if(((log2X == -1) || (log2Y == -1)) && !anySize)
#else
    if((log2X == -1) || (log2Y == -1))
#endif
    {
#ifdef RAD_LINUX
        fprintf(stderr, "pglTexture: rejected %dx%d texture (not a power of two)\n", xSize, ySize);
#endif
        lastError = PDDI_TEX_NOT_POW_2;
        return false;
    }

    int texSize;
#ifdef RAD_LINUX
    texSize = g_pglMaxTextureSize ? g_pglMaxTextureSize : 2048;
#else
    glGetIntegerv(GL_MAX_TEXTURE_SIZE,&texSize);
#endif
    
    if((xSize > texSize) || (ySize > texSize))
    {
        lastError = PDDI_TEX_TOO_BIG;
        return false;
    }

#ifdef RAD_LINUX
    // Linux port: DXT textures are kept compressed and uploaded as such (the D3D8 pddi's behaviour; Lock hands
    // out the blocks, top row first, which p3d/dxtn.cpp fills straight from the file)
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
        bits = new char*[nMipMap+1];
        int total = 0;
        for(int i = 0; i < nMipMap+1; i++)
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
#endif

    bits = new char*[nMipMap+1];
    for(int i = 0; i < nMipMap+1; i++)
        bits[i] = new char[(xSize>>i)*(ySize>>i)*4];

    lock.depth = 32;
    lock.format = PDDI_PIXEL_ARGB8888;

    if(context->GetDisplay()->ExtBGRA())
    {
        lock.native = true;
        lock.rgbaLShift[0] = lock.rgbaRShift[0] =
        lock.rgbaLShift[1] = lock.rgbaRShift[1] =
        lock.rgbaLShift[2] = lock.rgbaRShift[2] =
        lock.rgbaLShift[3] = lock.rgbaRShift[3] = 0;

        lock.rgbaMask[0] = 0x00ff0000;
        lock.rgbaMask[1] = 0x0000ff00;
        lock.rgbaMask[2] = 0x000000ff;
        lock.rgbaMask[3] = 0xff000000;
    }
    else
    {
        lock.native = false;
        lock.rgbaRShift[0] = 16;
        lock.rgbaLShift[2] = 16;

        lock.rgbaLShift[0] = 
        lock.rgbaLShift[1] = lock.rgbaRShift[1] =
        lock.rgbaRShift[2] =
        lock.rgbaLShift[3] = lock.rgbaRShift[3] = 0;

        lock.rgbaMask[0] = 0x000000ff;
        lock.rgbaMask[1] = 0x0000ff00;
        lock.rgbaMask[2] = 0x00ff0000;
        lock.rgbaMask[3] = 0xff000000;
    }

    context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_32BIT, (float)((xSize * ySize * 4) / 1024));
    context->ADD_STAT(PDDI_STAT_TEXTURE_COUNT_32BIT, 1);

    return true;
}

pglTexture::pglTexture(pglContext* c)
{
    context = c;
    contextID = c->contextID;
    bits = NULL;
    gltexture = 0;
    priority = 15;
    nMipMap = 0;
#ifdef RAD_LINUX
    compressedFormat = 0;
    blockBytes = 0;
    hasMipmaps = false;
#endif
}

pglTexture::~pglTexture()
{
    if(bits)
    {
        for(int i = 0; i < nMipMap+1; i++)
            delete [] bits[i];
        delete [] bits;
    }

#ifdef RAD_LINUX
    // Linux port: the GL copy was never freed, so every texture unloaded (e.g. on a level change) kept its video
    // memory until exit. A gltexture from before a display reset belongs to no one (SetGLState dropped it).
    if(contextID == context->contextID)
        pglDeleteTexture(gltexture);
#endif

#ifdef RAD_LINUX
    if(compressedFormat)
    {
        int total = 0;
        for(int i = 0; i < nMipMap+1; i++)
            total += MipBytes(i);
        context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_DXTN, -(float)total / 1024);
        context->ADD_STAT(PDDI_STAT_TEXTURE_COUNT_DXTN, -1);
        return;
    }
#endif
    context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_32BIT, -(float)((xSize * ySize * 4) / 1024));
    context->ADD_STAT(PDDI_STAT_TEXTURE_COUNT_32BIT, -1);
}

pddiPixelFormat pglTexture::GetPixelFormat()
{
#ifdef RAD_LINUX
    if(compressedFormat)
        return lock.format;
#endif
    return PDDI_PIXEL_ARGB8888;
}

int   pglTexture::GetWidth()
{
    return xSize;
}

int   pglTexture::GetHeight()
{
    return ySize;
}

int   pglTexture::GetDepth()
{
#ifdef RAD_LINUX
    if(compressedFormat)
        return lock.depth;
#endif
    return 32;
}

int   pglTexture::GetNumMipMaps()
{
    return nMipMap;
}

int pglTexture::GetAlphaDepth()
{
    return 8;
}

pddiLockInfo* pglTexture::Lock(int mipMap, pddiRect* rect)
{
    PDDIASSERT(mipMap <= nMipMap);

#ifdef RAD_LINUX
    if(compressedFormat)
    {
        // as D3D8's FillLockInfo for DXT: the blocks as stored, a row of blocks per pitch, no bottom-up view
        lock.width = xSize >> mipMap;
        lock.height = ySize >> mipMap;
        if(lock.width < 1) lock.width = 1;
        if(lock.height < 1) lock.height = 1;
        lock.pitch = ((lock.width + 3) / 4) * blockBytes;
        lock.bits = bits[mipMap];
        return &lock;
    }
#endif

#ifdef RAD_LINUX
    lock.width = xSize >> mipMap;       // any size (dynamic textures); the same for powers of two
    lock.height = ySize >> mipMap;
    if(lock.width < 1) lock.width = 1;
    if(lock.height < 1) lock.height = 1;
#else
    lock.width = 1 << (log2X-mipMap);
    lock.height = 1 << (log2Y-mipMap);
#endif
    lock.pitch = -lock.width * 4;
    lock.bits = bits[mipMap] + (lock.width * (lock.height-1) * 4);

    return &lock;
}

void pglTexture::Unlock(int mipLevel)
{
#ifdef RAD_LINUX
    pglDeleteTexture(gltexture);        // re-uploaded from bits[] by SetGLState on the render thread
#else
    glDeleteTextures(1,&gltexture);
#endif
    gltexture = 0;
}

void pglTexture::SetPriority(int p)
{
    priority = p;
#ifndef RAD_LINUX
    float fpriority = float(p) / 31.0f;
    glPrioritizeTextures(1,&gltexture,&fpriority);  // Linux port: not in core OpenGL (drivers ignored it anyway)
#endif
}

int pglTexture::GetPriority(void)
{
    return priority;
}

// paging control
void pglTexture::Prefetch(void)
{
}

void pglTexture::Discard(void)
{
}

// palette managment
int pglTexture::GetNumPaletteEntries(void)
{
    return 0;
}

void pglTexture::SetPalette(int nEntries, pddiColour* palette)
{
}

int pglTexture::GetPalette(pddiColour* palette)
{
    return 0;
}



#ifdef RAD_LINUX
void pglTexture::SetSampling(int filterMode, unsigned wrap, unsigned magFilter, unsigned minFilter)
{
    // with mipmaps on, every filtered (non-point) material samples trilinear, with anisotropy as set
    bool mipmapped = g_pglMipmaps && hasMipmaps && filterMode != PDDI_FILTER_NONE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mipmapped ? GL_LINEAR_MIPMAP_LINEAR : minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
    if(g_pglMaxAnisotropy > 0.0f)
    {
        float anisotropy = mipmapped ? g_pglAnisotropy : 1.0f;
        if(anisotropy > g_pglMaxAnisotropy)
            anisotropy = g_pglMaxAnisotropy;
        glTexParameterf(GL_TEXTURE_2D, PGL_TEXTURE_MAX_ANISOTROPY, anisotropy < 1.0f ? 1.0f : anisotropy);
    }
}
#endif
