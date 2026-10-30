//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

// Linux port of p3d/platform/win32/platform.cpp: identical behaviour, but the GL PDDI is linked
// statically (no LoadLibrary) and timing comes from SDL's performance counter.

#include <p3d/platform/linux/platform.hpp>
#include <p3d/platform/linux/plat_filemap.hpp>
#include <p3d/utility.hpp>
#include <p3d/memory.hpp>
#include <p3d/context.hpp>
#include <p3d/error.hpp>
#include <constants/version.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>

int pddiCreateVulkan(int versionMajor, int versionMinor, pddiDevice** device);     // pddi/vk/vkdev.cpp

tContextInitData::tContextInitData()
{
    hwnd           = 0;
    xsize          = 640;
    ysize          = 480;
    bpp            = 32;
    displayMode    = PDDI_DISPLAY_WINDOW;
    nColourBuffer  = 2;
    bufferMask     = PDDI_BUFFER_COLOUR | PDDI_BUFFER_DEPTH;
    adapterID      = 0;
    nSamples       = 0;
    PDDIlib[0]     = 0;
};

tPlatform* tPlatform::currentPlatform = NULL;

tPlatform::tPlatform(void* inst)
{
    instance = inst;
    currentContext = NULL;
    nContexts = 0;
}

tPlatform::~tPlatform()
{
}

tPlatform* tPlatform::Create(void* instance)
{
    p3d::UsePermanentMem( true );
    if(!currentPlatform)
    {
        currentPlatform = new tPlatform(instance);
        p3d::platform = currentPlatform;
    }
    p3d::UsePermanentMem( false );
    return currentPlatform;
}

void tPlatform::Destroy(tPlatform* plat)
{
    P3DASSERT(plat == currentPlatform);
    delete currentPlatform;
    currentPlatform = NULL;
}

tContext* tPlatform::CreateContext(tContextInitData* d)
{
    P3DASSERT(nContexts < P3D_MAX_CONTEXTS);

    pddiDevice* device;
    pddiDisplay* display;
    pddiRenderContext* context;
    pddiDisplayInfo* displayInfo;

    p3d::printf("Pure3D v%s, released %s\n", ATG_VERSION, ATG_RELEASE_DATE);

    p3d::UsePermanentMem( true );

    // static PDDIs: Vulkan when the platform layer made a Vulkan window and started Vulkan on it, else OpenGL
    bool vulkan = (SDL_GetWindowFlags((SDL_Window*)d->hwnd) & SDL_WINDOW_VULKAN) != 0;
    int success = vulkan ? pddiCreateVulkan(PDDI_VERSION_MAJOR, PDDI_VERSION_MINOR, &device)
                         : pddiCreate(PDDI_VERSION_MAJOR, PDDI_VERSION_MINOR, &device);
    if(success != PDDI_OK)
    {
        if(success == PDDI_VERSION_ERROR)
        {
            P3DVERIFY(0, "Cannot initialize PDDI library due to an incorrect version");
        }
        else
        {
            P3DVERIFY(0, "Cannot initialize PDDI library, unknown error.");
        }
    }

    tDebug::CapturePDDIMessages(device);

    display = device->NewDisplay(d->adapterID);
    display->SetWindowHandle(d->hwnd);
    if( !display->InitDisplay( d ) )
    {
        // Linux port: without a GL context every later GL call (loader threads included) crashed inside libGL.
        // Seen when the GPU's video memory was nearly full. Stop with a message instead.
        const char* msg = vulkan ? "The Simpsons: Hit & Run could not start its Vulkan renderer.\n\n"
                                   "Set renderer=opengl in simpsons.ini to use OpenGL." :
                          "The Simpsons: Hit & Run could not create its OpenGL context.\n\n"
                          "The graphics card may be out of video memory: close other programs using the GPU, "
                          "log out and back in, or lower the resolution in simpsons.ini.";
        fprintf( stderr, "%s\n", msg );
        SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "The Simpsons: Hit & Run", msg, NULL );
        exit( 1 );
    }

    context = device->NewRenderContext(display);
    P3DVERIFY(context != NULL, "NewRenderContext() failed");

    pddiLibInfo libInfo;
    device->GetLibraryInfo(&libInfo);
    displayInfo = display->GetDisplayInfo();

    p3d::printf("tContext created: PDDI v%d.%d (build %d) '%s', '%s'\n",
        libInfo.versionMajor,
        libInfo.versionMinor,
        libInfo.versionBuild,
        libInfo.description,
        displayInfo->description);

    for(int find = 0; find < P3D_MAX_CONTEXTS; find++)
    {
        if(!contexts[find].context)
        {
            contexts[find].context = new tContext(device,display,context);
            contexts[find].pddiLib = NULL;
            contexts[find].windowHandle = d->hwnd;

            if(!currentContext)
                SetActiveContext(contexts[find].context);

            contexts[find].context->Setup();

            nContexts++;

            p3d::UsePermanentMem( false );
            return contexts[find].context;
        }
    }

    p3d::UsePermanentMem( false );
    return NULL;
}

void tPlatform::DestroyContext(tContext* context)
{
    int foundHandle = -1;
    for(int i = 0; i < nContexts; i++)
        if(contexts[i].context == context)
            foundHandle = i;

    P3DASSERT(foundHandle != -1);

    context->Shutdown();

    contexts[foundHandle].windowHandle = NULL;
    delete context;
    contexts[foundHandle].context = NULL;
    contexts[foundHandle].pddiLib = NULL;
    nContexts--;

    if(currentContext == context)
    {
        currentContext = NULL;
    }
}

void tPlatform::SetActiveContext(tContext* context)
{
    P3DASSERT( context );
    currentContext = context;
    p3d::context = context;
    p3d::inventory = context->GetInventory();
    p3d::stack = context->GetMatrixStack();
    p3d::loadManager = context->GetLoadManager();
    p3d::pddi = context->GetContext();
    p3d::device = context->GetDevice();
    p3d::display = context->GetDisplay();
}

tFile* tPlatform::OpenFile(const char* filename)
{
    tLinuxFileMap* file = new tLinuxFileMap(filename);
    if(!file->IsOpen())
    {
        file->Release();
        return NULL;
    }
    return file;
}

P3D_U64 tPlatform::GetTimeFreq(void)
{
    return SDL_GetPerformanceFrequency();
}

P3D_U64 tPlatform::GetTime(void)
{
    return SDL_GetPerformanceCounter();
}

bool tPlatform::ProcessWindowsMessage(void* win, unsigned msg, unsigned wparam, int lparam)
{
    return false;   // no Win32 message pump under SDL; events are handled by the game's SDL loop
}

tPlatform* tPlatform::GetPlatform(void)
{
    return currentPlatform;
}
