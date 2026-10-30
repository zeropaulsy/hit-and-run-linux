//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   Win32Platform   
//
// Description: Abstracts the differences for setting up and shutting down
//              the different platforms.
//
// History:     + Stolen and cleaned up from Penthouse -- Darwin Chau
//
//=============================================================================

#ifndef WIN32PLATFORM_H
#define WIN32PLATFORM_H

//========================================
// System Includes
//========================================

#include <windows.h>

//========================================
// Nested Includes
//========================================
#include "platform.h" // base class
#include <data/config/gameconfig.h> // interface

//========================================
// Forward References
//========================================
struct IRadMemoryHeap;
class tPlatform;
class tContext;


struct MOUSETRACKER 
{
    DWORD cbSize;
    DWORD dwFlags;
    HWND  hwndTrack;
};

//=============================================================================
//
// Synopsis:    Provides abstraction for setting up and closing a win32 exe.
//
//=============================================================================
class Win32Platform : public Platform, public GameConfigHandler
{
public:

    enum Resolution
    {
        Res_640x480 = 0,
        Res_800x600,
        Res_1024x768,
        Res_1152x864,
        Res_1280x1024,
        Res_1600x1200,
        // Linux port: widescreen additions. Values = menu positions, so only append (labels added in
        // CGuiScreenDisplay; Display.pag lists the original six).
        Res_1280x720,
        Res_1920x1080,
        Res_2560x1440,
        Res_2560x1600,
        NUM_RESOLUTIONS,
        // Linux port: further values index the runtime resolution table (GetNumResolutions); this widens the
        // enum's range to hold them
        RES_TABLE_MAX = 63
    };

public:

    // Static Methods for accessing this singleton.
    static Win32Platform* CreateInstance( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow );
    static Win32Platform* GetInstance();
    static void DestroyInstance();

    // Had to workaround our nice clean design cause FTech must be init'ed
    // before anything else is done.
    static bool InitializeWindow( HINSTANCE hInstance );
#ifdef RAD_LINUX
    static struct SDL_Window* CreateGameWindow();
#endif
    static void InitializeFoundation();
    static void InitializeMemory();
    static void ShutdownMemory();

    // Implement Platform interface.
    virtual void InitializePlatform();
    virtual void ShutdownPlatform();

    virtual void LaunchDashboard();
    virtual void ResetMachine();

    virtual void DisplaySplashScreen( SplashScreen screenID, 
        const char* overlayText = NULL, 
        float fontScale = 1.0f, 
        float textPosX = 0.0f,
        float textPosY = 0.0f,
        tColour textColour = tColour( 255,255,255 ),
        int fadeFrames = 3 );

    virtual void DisplaySplashScreen( const char* textureName,
        const char* overlayText = NULL, 
        float fontScale = 1.0f, 
        float textPosX = 0.0f,
        float textPosY = 0.0f,
        tColour textColour = tColour( 255,255,255 ),
        int fadeFrames = 3 );

    virtual bool OnDriveError( radFileError error, const char* pDriveName, void* pUserData );  
    virtual void OnControllerError(const char *msg);

    HWND GetHwnd() const { return mhWnd; }

    // Set the resolution of the display, or increase the size of the window.
    // Returns true if the change was successful, false if resolution is not supported.
    bool SetResolution( Resolution res, int bpp, bool fullscreen );

    Resolution GetResolution() const;
    int GetBPP() const;
    bool IsFullscreen() const;

    // Implementation of the GameConfigHandler interface
    virtual const char* GetConfigName() const;
    virtual int GetNumProperties() const;
    virtual void LoadDefaults();
    virtual void LoadConfig( ConfigString& config );
    virtual void SaveConfig( ConfigString& config );

private:

    // Constructors, Destructors, and Operators
    Win32Platform( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow );
    virtual ~Win32Platform();

    // Unused Constructors, Destructors, and Operators
    Win32Platform( const Win32Platform& aPlatform );
    Win32Platform& operator=( const Win32Platform& aPlatform );

    // Methods from Platform
    virtual void InitializeFoundationDrive();
    virtual void ShutdownFoundation();

    virtual void InitializePure3D();
    virtual void ShutdownPure3D();

    // Initializes the d3d context
    void InitializeContext();

    // Resolution related methods
    static void TranslateResolution( Resolution res, int&x, int&y );
    bool IsResolutionSupported( Resolution res, int bpp ) const;

    // Mouse tracking method
    static BOOL TrackMouseEvent( MOUSETRACKER* pMouseEventTracker );
    static void CALLBACK TrackMouseTimerProc( HWND hWnd, UINT uMsg, UINT idEvent,DWORD dwTime );

    // Windows methods.
    void ResizeWindow();
    static void ShowTheCursor( bool show );
    static LRESULT CALLBACK WndProc( HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam );
#ifdef RAD_LINUX
public:
    // SDL2 replacements (linuxplatform_sdl.cpp): PumpEvents does what WndProc did and returns
    // true when the window was closed (the game loop then runs its WM_QUIT handling).
    static bool PumpEvents();
    static void ShowGameWindow();
    static void GetDesktopSize( int& width, int& height );

    // Resolutions: values index a runtime table, the enum's entries first (so they keep their meaning), then
    // the desktop size, the display's modes and common PC sizes. FindResolution returns -1 if not found
    // (add = false) or the table is full.
    static int GetNumResolutions();
    static Resolution FindResolution( int width, int height, bool add );

    // Fullscreen kind: borderless at the desktop size, or exclusive (display switched to the resolution).
    // Only meaningful while fullscreen; set it before SetResolution().
    void SetBorderless( bool borderless ) { mBorderless = borderless; }
    bool IsBorderless() const { return mBorderless; }
    static void GetResolutionSize( Resolution res, int& x, int& y ) { TranslateResolution( res, x, y ); }

    // Graphics settings (CGuiScreenGraphics; simpsons.ini antialiasing=, shaders=, renderer=). Anti-aliasing and
    // the user's shaders (shaders/) change at once; the renderer is read at start-up.
    enum Renderer
    {
        RENDERER_OPENGL,
        RENDERER_VULKAN,        // not implemented yet: OpenGL is used

        NUM_RENDERERS
    };
    void SetAntialiasing( int samples );
    int GetAntialiasing() const { return mAntialiasing; }
    void SetUserShaders( bool on );
    bool GetUserShaders() const;
    void SetRenderer( Renderer renderer ) { mRenderer = renderer; }
    Renderer GetRenderer() const { return mRenderer; }
private:
#endif

private:

    // Pointer to the one and only instance of this singleton.
    static Win32Platform* spInstance;

    // Private Attributes
    // Had to make these static because of the initialization order problem.
    static HINSTANCE mhInstance;
    static HWND mhWnd;
    static HANDLE mhMutex;
    static bool mShowCursor;

    // Pure 3D attributes
    tPlatform* mpPlatform; 
    tContext* mpContext;

    // window properties.
    Resolution mResolution;
    int mbpp;
    bool mFullscreen;
#ifdef RAD_LINUX
    bool mBorderless;
    int mAntialiasing;      // MSAA samples (simpsons.ini antialiasing=, 0 = off)
    bool mMipmaps;          // texture filtering (simpsons.ini mipmaps=yes/no, anisotropy=)
    int mAnisotropy;
    Renderer mRenderer;     // simpsons.ini renderer=opengl/vulkan
#endif
    int mScreenWidth;
    int mScreenHeight;
};

#endif // WIN32PLATFORM_H
