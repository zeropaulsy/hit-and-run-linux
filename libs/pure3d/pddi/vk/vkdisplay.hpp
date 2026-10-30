//=============================================================================
// Linux port: Vulkan renderer display (as pglDisplay, gl/display_linux/gldisplay.hpp)
//=============================================================================

#ifndef _VKDISPLAY_HPP_
#define _VKDISPLAY_HPP_

#include <pddi/pddi.hpp>
#include <pddi/vk/vkpipeline.hpp>
#include <sys/time.h>
#include <SDL2/SDL.h>

class pvkContext;

class pvkDisplay : public pddiDisplay
{
public:
    pvkDisplay();
    ~pvkDisplay();

    bool InitDisplay(const pddiDisplayInit*);
    bool InitDisplay(int x, int y, int bpp);
    pddiDisplayInfo* GetDisplayInfo(void);
    int GetHeight(void);
    int GetWidth(void);
    int GetDepth(void);
    pddiDisplayMode GetDisplayMode(void);
    int GetNumColourBuffer(void);
    unsigned GetBufferMask(void);
    unsigned GetFreeTextureMem(void);
    void SwapBuffers();
    unsigned Screenshot(pddiColour* buffer, int nBytes);

    void SetWindowHandle(void* sdlWindow);
    void* GetWindowHandle();

    void BeginTiming();
    float EndTiming();

    void SetContext(pvkContext* c) { context = c; }
    pvkPipeline* GetPipeline(void) { return &pipeline; }

    void SetAntialiasing(int samples);
    void BeginFrameTarget(void);        // pvkContext::BeginFrame
    void PostProcess(int stage);        // pglPostStage: resolves at the scene stage (user post shaders: not yet)

    void SetGamma(float r, float g, float b);
    void GetGamma(float* r, float* g, float* b);

private:
    pddiDisplayMode mode;
    int winWidth, winHeight, winBitDepth;
    pvkContext* context;
    float gammaR, gammaG, gammaB;
    SDL_Window* window;
    pvkPipeline pipeline;
    int msaaRequested;
    struct timeval beginTime;
};

void pvkSetAntialiasing(int samples);
void pvkPostProcessScene();

#endif
