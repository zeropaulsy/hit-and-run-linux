//=============================================================================
// Linux port: Vulkan renderer display (see vkdisplay.hpp; window handling as gldisplay.cpp)
//=============================================================================

#include <pddi/vk/vkdisplay.hpp>
#include <pddi/vk/vkcon.hpp>
#include <pddi/gl/glpost.hpp>

#include <SDL2/SDL_vulkan.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static pvkDisplay* s_display = NULL;

pvkDisplay::pvkDisplay()
{
    s_display = this;
    mode = PDDI_DISPLAY_WINDOW;
    winWidth = 640;
    winHeight = 480;
    winBitDepth = 32;
    context = NULL;
    gammaR = gammaG = gammaB = 1.0f;
    window = NULL;
    msaaRequested = 4;      // until the settings are read (Win32Platform's default too)
    memset(&beginTime, 0, sizeof(beginTime));
}

pvkDisplay::~pvkDisplay()
{
    pipeline.Shutdown();
    if(s_display == this)
        s_display = NULL;
}

void pvkDisplay::SetWindowHandle(void* sdlWindow)
{
    window = (SDL_Window*)sdlWindow;
}

void* pvkDisplay::GetWindowHandle()
{
    return (void*)window;
}

void pglSetTextureFiltering(int mipmaps, int anisotropy);

// the platform layer created the window with SDL_WINDOW_VULKAN and started Vulkan on it (pvkStart)
bool pvkDisplay::InitDisplay(const pddiDisplayInit* init)
{
    assert(window && "pvkDisplay: SetWindowHandle(SDL_Window*) must be called before InitDisplay");
    if(!g_pvk)
        return false;
    mode = init->displayMode;
    winBitDepth = init->bpp;

    // as gldisplay.cpp: borderless covers the desktop, exclusive switches to the closest mode, else a window
    if(mode == PDDI_DISPLAY_FULLSCREEN_DESKTOP)
    {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    else if(mode == PDDI_DISPLAY_FULLSCREEN)
    {
        SDL_SetWindowFullscreen(window, 0);
        SDL_DisplayMode want, closest;
        memset(&want, 0, sizeof(want));
        want.w = init->xsize;
        want.h = init->ysize;
        int display = SDL_GetWindowDisplayIndex(window);
        if(SDL_GetClosestDisplayMode(display < 0 ? 0 : display, &want, &closest) != NULL)
            SDL_SetWindowDisplayMode(window, &closest);
        SDL_SetWindowSize(window, init->xsize, init->ysize);
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
    }
    else
    {
        SDL_SetWindowFullscreen(window, 0);
        SDL_SetWindowSize(window, init->xsize, init->ysize);
    }

    if(!pipeline.Init())
        return false;
    pglSetTextureFiltering(-1, -1);

    int w = init->xsize, h = init->ysize;
    if(mode == PDDI_DISPLAY_FULLSCREEN || mode == PDDI_DISPLAY_FULLSCREEN_DESKTOP)
        SDL_Vulkan_GetDrawableSize(window, &w, &h);
    return InitDisplay(w, h, init->bpp);
}

bool pvkDisplay::InitDisplay(int x, int y, int bpp)
{
    winWidth = x;
    winHeight = y;
    if(g_pvk)
        g_pvk->SetSize(x, y);
    return true;
}

extern pddiDisplayInfo pvkDisplayInfo[1];

pddiDisplayInfo* pvkDisplay::GetDisplayInfo(void)   { return &pvkDisplayInfo[0]; }
unsigned pvkDisplay::GetFreeTextureMem()            { return unsigned(-1); }
unsigned pvkDisplay::GetBufferMask()                { return ~0U; }
int pvkDisplay::GetHeight()                         { return winHeight; }
int pvkDisplay::GetWidth()                          { return winWidth; }
int pvkDisplay::GetDepth()                          { return winBitDepth; }
pddiDisplayMode pvkDisplay::GetDisplayMode(void)    { return mode; }
int pvkDisplay::GetNumColourBuffer(void)            { return 2; }

void pvkDisplay::GetGamma(float* r, float* g, float* b)
{
    *r = gammaR;
    *g = gammaG;
    *b = gammaB;
}

void pvkDisplay::SetGamma(float r, float g, float b)
{
    gammaR = r;
    gammaG = g;
    gammaB = b;
}

void pvkSetAntialiasing(int samples)
{
    if(s_display)
        s_display->SetAntialiasing(samples);
}

void pvkDisplay::SetAntialiasing(int samples)
{
    const char* e = getenv("SHAR_MSAA");     // overrides the setting
    msaaRequested = e ? atoi(e) : samples;
}

void pvkDisplay::BeginFrameTarget(void)
{
    if(g_pvk)
        g_pvk->SetSamples(msaaRequested);
}

// the texture-filtering settings changed (gldisplay.cpp pglSetTextureFiltering)
void pvkSetTextureFiltering(bool mipmaps, float anisotropy)
{
    if(s_display)
        s_display->GetPipeline()->SetTextureFiltering(mipmaps, anisotropy);
}

void pvkPostProcessScene()
{
    if(s_display)
        s_display->PostProcess(PGL_POST_SCENE);
}

// the scene stage: as the GL renderer, the GUI layer is drawn single-sampled after the multisampled scene is resolved.
// The user's post-processing shaders do not run on Vulkan yet.
void pvkDisplay::PostProcess(int stage)
{
    if(g_pvk && stage == PGL_POST_SCENE)
        g_pvk->ResolveTarget();
}

// SHAR_SHOT_EVERY=<seconds>: shot_<n>.png as the GL renderer's (gldisplay.cpp), read back a frame or two later
void pglSavePNG(const char* path, int w, int h, const unsigned char* rgbaBottomUp);

static void SaveShot(const unsigned char* rgba, int w, int h)
{
    static int n = 0;
    unsigned char* flipped = (unsigned char*)malloc((size_t)w * h * 4);
    if(!flipped)
        return;
    for(int y = 0; y < h; y++)
        memcpy(flipped + (size_t)(h - 1 - y) * w * 4, rgba + (size_t)y * w * 4, (size_t)w * 4);
    char path[64];
    sprintf(path, "shot_%03d.png", n++);
    pglSavePNG(path, w, h, flipped);
    free(flipped);
}

static void MaybeScreenshot()
{
    static int every = -1;
    static unsigned last = 0;
    if(every < 0)
    {
        const char* e = getenv("SHAR_SHOT_EVERY");
        every = e ? atoi(e) : 0;
    }
    if(every <= 0)
        return;
    unsigned now = SDL_GetTicks();
    if(now - last < (unsigned)every * 1000)
        return;
    last = now;
    g_pvk->RequestReadback(SaveShot);
}

void pvkDisplay::SwapBuffers()
{
    s_display = this;
    if(!g_pvk)
        return;
    MaybeScreenshot();
    g_pvk->Present(gammaR, window);

    // the window is resizable: follow its size
    int w, h;
    SDL_Vulkan_GetDrawableSize(window, &w, &h);
    if(w > 0 && h > 0)
    {
        if(w != winWidth || h != winHeight)
            fprintf(stderr, "PDDI: drawing at %dx%d\n", w, h);
        winWidth = w;
        winHeight = h;
        g_pvk->SetSize(w, h);
    }
}

unsigned pvkDisplay::Screenshot(pddiColour* buffer, int nBytes)
{
    assert(0 && "PDDI: pddiDisplay::ScreenShot() - Not implemented under linux.");
    return 0;
}

void pvkDisplay::BeginTiming()
{
    gettimeofday(&beginTime, NULL);
}

float pvkDisplay::EndTiming()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (float)(tv.tv_sec - beginTime.tv_sec) * 1e3 + (float)(tv.tv_usec - beginTime.tv_usec) * 1e-3;
}
