//===========================================================================
// Linux port: SDL2 replacements for the Win32 window code in win32platform.cpp.
//
// Win32Platform is the game's PC platform class; everything in it is shared with Linux except the window
// creation, the window procedure (WndProc) and the mouse/cursor helpers, which live here instead.
// PumpEvents() is called by the game loop where the Win32 build ran its PeekMessage loop, and does what
// WndProc did: focus handling, front-end mouse movement and clicks, and quitting when the window closes.
//===========================================================================

#ifdef RAD_LINUX

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <radcontroller.hpp>
#include <raddebug.hpp>
#include <p3d/platform.hpp>
#include <p3d/context.hpp>
#include <input/inputmanager.h>
#include <input/FEMouse.h>
#include <input/MouseCursor.h>
#include <main/win32platform.h>
#include <render/RenderFlow/renderflow.h>

static const char ApplicationName[] = "The Simpsons: Hit & Run";
static const Win32Platform::Resolution StartingResolution = Win32Platform::Res_800x600;   // as win32platform.cpp
void pglToggleUserShaders();     // pddi/gl/glcustom.cpp: the M key
static bool s_swallowLeftUp = false;      // a click that only re-showed the front-end cursor (see PumpEvents)

// Intel Ice Lake (Gen11) with Mesa: the GPU hangs within a minute or two of gameplay ("Resetting rcs0 for preemption
// time out" in the kernel log, Iris Plus ICL GT1.5, Mesa 26.0.8, kernel 7.0, 2026-10-20), whatever the window system,
// shaders, MSAA or filtering; after three resets the context is banned and the screen stays black while the game runs
// on. Never with the depth buffer's HiZ off (or INTEL_DEBUG=sync); costs ~5% GPU time. Not traced to one operation
// (partial depth clears and stencil were ruled out). Must be set before the driver loads. The user's own INTEL_DEBUG
// or SHAR_INTEL_HIZ=1 keeps HiZ.
static void DisableHizOnIceLake()
{
    if( getenv( "INTEL_DEBUG" ) != NULL || getenv( "SHAR_INTEL_HIZ" ) != NULL )
        return;
    for( int card = 0; card < 8; card++ )
    {
        char path[ 64 ];
        unsigned vendor = 0, device = 0;
        snprintf( path, sizeof( path ), "/sys/class/drm/card%d/device/vendor", card );
        FILE* f = fopen( path, "r" );
        if( f == NULL )
            continue;
        if( fscanf( f, "%x", &vendor ) != 1 )
            vendor = 0;
        fclose( f );
        snprintf( path, sizeof( path ), "/sys/class/drm/card%d/device/device", card );
        f = fopen( path, "r" );
        if( f == NULL )
            continue;
        if( fscanf( f, "%x", &device ) != 1 )
            device = 0;
        fclose( f );
        if( vendor == 0x8086 && ( device & 0xff00 ) == 0x8a00 )    // Ice Lake graphics: 0x8a50-0x8a71
        {
            setenv( "INTEL_DEBUG", "nohiz", 0 );
            fprintf( stderr, "PDDI: Intel Ice Lake GPU: depth HiZ off (it hangs the GPU; SHAR_INTEL_HIZ=1 keeps it)\n" );
            return;
        }
    }
}

//==============================================================================
// Win32Platform::InitializeWindow
//==============================================================================
// Creates the game window. The GL context itself is created by the PDDI display (gldisplay.cpp) on this
// window, so the GL attributes must be set here, before the window exists.
bool Win32Platform::InitializeWindow( HINSTANCE hInstance )
{
    mhInstance = hInstance;

    // relative mouse mode (mouse-look in gameplay): use raw relative input, not pointer warping, which
    // reports a spurious one-pixel motion every frame under XWayland
    SDL_SetHint( SDL_HINT_MOUSE_RELATIVE_MODE_WARP, "0" );

    // In a Wayland session run as a native Wayland client (SDL2 picks X11 first). Under XWayland a window
    // covering the screen on mirrored outputs made XWayland allocate a new screen-sized buffer per frame that
    // the compositor did not hand back: gigabytes within minutes, until the out-of-memory killer struck.
    // SDL_VIDEODRIVER=x11 (or SHAR_X11=1) still chooses X11.
    if( getenv( "SDL_VIDEODRIVER" ) == NULL && getenv( "WAYLAND_DISPLAY" ) != NULL && getenv( "SHAR_X11" ) == NULL )
    {
        SDL_SetHint( SDL_HINT_VIDEODRIVER, "wayland,x11" );

        // NVIDIA: the 32-bit egl-wayland2 platform (09_nvidia_wayland2.json, tried first) fails every window surface
        // with EGL_BAD_ALLOC (driver 595, 2026-10-19); the older egl-wayland works. Point NVIDIA's EGL at that one
        // unless the user chose. Mesa does not read this variable.
        static const char* const nvidiaWayland = "/usr/share/egl/egl_external_platform.d/10_nvidia_wayland.json";
        if( getenv( "__EGL_EXTERNAL_PLATFORM_CONFIG_FILENAMES" ) == NULL &&
            getenv( "__EGL_EXTERNAL_PLATFORM_CONFIG_DIRS" ) == NULL && access( nvidiaWayland, R_OK ) == 0 )
        {
            setenv( "__EGL_EXTERNAL_PLATFORM_CONFIG_FILENAMES", nvidiaWayland, 0 );
        }
    }
    DisableHizOnIceLake( );

    if( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER | SDL_INIT_HAPTIC ) != 0 )
    {
        fprintf( stderr, "SDL_Init failed: %s\n", SDL_GetError() );
        return false;
    }

    fprintf( stderr, "SDL video driver: %s\n", SDL_GetCurrentVideoDriver( ) );

    SDL_Window* window = CreateGameWindow();
    if( window == NULL && strcmp( SDL_GetCurrentVideoDriver( ), "wayland" ) == 0 )
    {
        // A Wayland window can still fail where X11 works (a driver's EGL platform): fall back to XWayland
        fprintf( stderr, "SDL_CreateWindow failed on Wayland (%s): trying X11\n", SDL_GetError() );
        SDL_QuitSubSystem( SDL_INIT_VIDEO );
        SDL_SetHint( SDL_HINT_VIDEODRIVER, "x11" );
        if( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 )
        {
            fprintf( stderr, "SDL video driver: %s\n", SDL_GetCurrentVideoDriver( ) );
            window = CreateGameWindow();
        }
    }
    if( window == NULL )
    {
        fprintf( stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError() );
        return false;
    }

    // SDL2 starts with text input on, which hands the keyboard to the desktop's input method: on KDE Plasma holding
    // a key with accented variants (A, S, D...) then waits for the accent popup before the game sees the press.
    // The game reads raw keys only, never text.
    SDL_StopTextInput( );

    mhWnd = (HWND) window;
    ShowTheCursor( false );
    return true;
}

// The GL attributes belong to the video subsystem: set them again after it is restarted
SDL_Window* Win32Platform::CreateGameWindow()
{
    // Pure3D's GL renderer runs on an OpenGL 3.3 core context (pddi/gl/glpipeline.hpp). SHAR_GL_DEBUG=1 asks for a
    // debug context: the driver's errors and warnings are printed as they happen.
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 3 );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 3 );
    SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE );
    const char* glDebug = getenv( "SHAR_GL_DEBUG" );
    if( glDebug && glDebug[ 0 ] == '1' )
    {
        SDL_GL_SetAttribute( SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG );
    }
    SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
    SDL_GL_SetAttribute( SDL_GL_DEPTH_SIZE, 24 );
    SDL_GL_SetAttribute( SDL_GL_STENCIL_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
    SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );

    int x, y;
    TranslateResolution( StartingResolution, x, y );

    SDL_Window* window = SDL_CreateWindow( ApplicationName,
                                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                           x, y,
                                           SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE |
                                           SDL_WINDOW_ALLOW_HIGHDPI );     // Wayland with display scaling: render
                                                                           // at the screen's pixels (the display
                                                                           // follows SDL_GL_GetDrawableSize)
    return window;
}

//==============================================================================
// Win32Platform::ShowGameWindow
//==============================================================================
void Win32Platform::ShowGameWindow()
{
    SDL_ShowWindow( (SDL_Window*) mhWnd );
    SDL_RaiseWindow( (SDL_Window*) mhWnd );
}

//==============================================================================
// Win32Platform::GetDesktopSize
//==============================================================================
void Win32Platform::GetDesktopSize( int& width, int& height )
{
    SDL_DisplayMode mode;
    if( SDL_GetDesktopDisplayMode( 0, &mode ) == 0 )
    {
        width = mode.w;
        height = mode.h;
    }
    else
    {
        width = 1024;
        height = 768;
    }
}

//==============================================================================
// Resolution table
//==============================================================================
// Win32 offered a fixed list of mostly 4:3 sizes, checked against the adapter's modes. Here the list is built
// at run time: the Resolution enum's entries first (their values stay valid), then common PC sizes and the
// desktop size. The display menu shows the ones that fit the desktop, and any WxH in simpsons.ini is added.
// The display's own mode list is not used: under XWayland it is a long list of emulated sizes, and neither
// windows nor (borderless) fullscreen need a real mode. Fullscreen is borderless at the desktop size, so the resolution only sizes the window.
namespace
{
    struct ResolutionSize { int w, h; };
    ResolutionSize sResolutions[ Win32Platform::RES_TABLE_MAX + 1 ];
    int sNumResolutions = 0;
    bool sDesktopAdded = false;

    int AddResolution( int w, int h )
    {
        for( int i = 0; i < sNumResolutions; i++ )
        {
            if( sResolutions[ i ].w == w && sResolutions[ i ].h == h )
            {
                return i;
            }
        }
        if( sNumResolutions > Win32Platform::RES_TABLE_MAX )
        {
            return -1;
        }
        sResolutions[ sNumResolutions ].w = w;
        sResolutions[ sNumResolutions ].h = h;
        return sNumResolutions++;
    }

    void InitResolutions()
    {
        if( sNumResolutions == 0 )
        {
            // = Win32Platform::Resolution order
            static const int enumSizes[][ 2 ] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1152, 864 },
                                                  { 1280, 1024 }, { 1600, 1200 }, { 1280, 720 }, { 1920, 1080 },
                                                  { 2560, 1440 }, { 2560, 1600 } };
            rAssert( sizeof( enumSizes ) / sizeof( enumSizes[ 0 ] ) == Win32Platform::NUM_RESOLUTIONS );
            for( unsigned int i = 0; i < sizeof( enumSizes ) / sizeof( enumSizes[ 0 ] ); i++ )
            {
                AddResolution( enumSizes[ i ][ 0 ], enumSizes[ i ][ 1 ] );
            }
            static const int commonSizes[][ 2 ] = { { 1280, 800 }, { 1366, 768 }, { 1440, 900 }, { 1600, 900 },
                                                    { 1680, 1050 }, { 1920, 1200 }, { 2560, 1080 }, { 3440, 1440 },
                                                    { 3840, 2160 } };
            for( unsigned int i = 0; i < sizeof( commonSizes ) / sizeof( commonSizes[ 0 ] ); i++ )
            {
                AddResolution( commonSizes[ i ][ 0 ], commonSizes[ i ][ 1 ] );
            }
        }
        if( !sDesktopAdded && SDL_WasInit( SDL_INIT_VIDEO ) )
        {
            sDesktopAdded = true;
            SDL_DisplayMode mode;
            if( SDL_GetDesktopDisplayMode( 0, &mode ) == 0 )
            {
                AddResolution( mode.w, mode.h );
            }
        }
    }
}

int Win32Platform::GetNumResolutions()
{
    InitResolutions();
    return sNumResolutions;
}

Win32Platform::Resolution Win32Platform::FindResolution( int width, int height, bool add )
{
    InitResolutions();
    for( int i = 0; i < sNumResolutions; i++ )
    {
        if( sResolutions[ i ].w == width && sResolutions[ i ].h == height )
        {
            return static_cast< Resolution >( i );
        }
    }
    return static_cast< Resolution >( add ? AddResolution( width, height ) : -1 );
}

void Win32Platform::TranslateResolution( Resolution res, int& x, int& y )
{
    InitResolutions();
    if( res >= 0 && res < sNumResolutions )
    {
        x = sResolutions[ res ].w;
        y = sResolutions[ res ].h;
    }
    else
    {
        x = 640;
        y = 480;
    }
}

bool Win32Platform::IsResolutionSupported( Resolution res, int bpp ) const
{
    // any size works in a window, and fullscreen always uses the desktop's
    InitResolutions();
    return res >= 0 && res < sNumResolutions;
}

//==============================================================================
// Win32Platform::ResizeWindow
//==============================================================================
void Win32Platform::ResizeWindow()
{
    if( mFullscreen )
    {
        return;
    }

    int cx, cy;
    TranslateResolution( mResolution, cx, cy );

    SDL_Window* window = (SDL_Window*) mhWnd;
    SDL_SetWindowSize( window, cx, cy );
    SDL_SetWindowPosition( window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED );
    SDL_ShowWindow( window );
}

//==============================================================================
// Win32Platform::ShowTheCursor
//==============================================================================
void Win32Platform::ShowTheCursor( bool show )
{
    if( mShowCursor != show )
    {
        mShowCursor = show;
        // "Hidden" = a single pixel at alpha 1/255, not SDL_ShowCursor( SDL_DISABLE ) or a fully transparent
        // cursor: XWayland treats both of those as hidden, and a hidden cursor keeps the pointer locked in the
        // window after mouse-look (see radControllerSDLSetMouseLook)
        static SDL_Cursor* s_blank = NULL;
        if( s_blank == NULL )
        {
            SDL_Surface* pixel = SDL_CreateRGBSurfaceWithFormat( 0, 1, 1, 32, SDL_PIXELFORMAT_ARGB8888 );
            if( pixel != NULL )
            {
                *(Uint32*) pixel->pixels = 0x01000000;
                s_blank = SDL_CreateColorCursor( pixel, 0, 0 );
                SDL_FreeSurface( pixel );
            }
        }
        SDL_SetCursor( ( mShowCursor || s_blank == NULL ) ? SDL_GetDefaultCursor() : s_blank );
    }
}

//==============================================================================
// Win32Platform::PumpEvents
//==============================================================================
// The Linux equivalent of the Win32 message loop + WndProc. Keyboard and gamepad input is read by the
// input layer (as it was through DirectInput), so only window-level events are handled here.
bool Win32Platform::PumpEvents()
{
    bool quit = false;
    SDL_Event e;

    // debug: SHAR_RESIZE_SCRIPT="WxH@ms,..." resizes the window at those times (tests window resizing)
    static const char* s_resizeScript = getenv( "SHAR_RESIZE_SCRIPT" );
    static unsigned int s_resizeDone = 0;
    unsigned int resizeIndex = 0;
    for( const char* c = s_resizeScript; c && *c; resizeIndex++ )
    {
        int w = 0, h = 0;
        unsigned int at = 0;
        if( sscanf( c, "%dx%d@%u", &w, &h, &at ) == 3 && resizeIndex == s_resizeDone && SDL_GetTicks() >= at )
        {
            SDL_SetWindowSize( (SDL_Window*) mhWnd, w, h );
            s_resizeDone++;
        }
        c = strchr( c, ',' );
        if( c ) c++;
    }
    // debug: SHAR_KEYPRESS_SCRIPT="M@ms,..." pushes a key press (SDL key name) into the event queue at those times
    // (tests window-level keys such as the shader toggle; the game's own input reads SHAR_KEY_SCRIPT)
    static const char* s_keypressScript = getenv( "SHAR_KEYPRESS_SCRIPT" );
    static unsigned int s_keypressDone = 0;
    unsigned int keypressIndex = 0;
    for( const char* c = s_keypressScript; c && *c; keypressIndex++ )
    {
        char name[32] = "";
        unsigned int at = 0;
        if( sscanf( c, "%31[^@]@%u", name, &at ) == 2 && keypressIndex == s_keypressDone && SDL_GetTicks() >= at )
        {
            SDL_Event key;
            memset( &key, 0, sizeof( key ) );
            key.type = SDL_KEYDOWN;
            key.key.state = SDL_PRESSED;
            key.key.keysym.scancode = SDL_GetScancodeFromName( name );
            key.key.keysym.sym = SDL_GetKeyFromScancode( key.key.keysym.scancode );
            SDL_PushEvent( &key );
            s_keypressDone++;
        }
        c = strchr( c, ',' );
        if( c ) c++;
    }
    // debug: SHAR_MOUSE_SCRIPT="x,y@ms,..." moves the mouse cursor (window pixels) at those times (menus select the item
    // under the cursor, so scripted keyboard navigation moves it out of the way first)
    static const char* s_mouseScript = getenv( "SHAR_MOUSE_SCRIPT" );
    static unsigned int s_mouseDone = 0;
    unsigned int mouseIndex = 0;
    for( const char* c = s_mouseScript; c && *c; mouseIndex++ )
    {
        int x = 0, y = 0;
        unsigned int at = 0;
        if( sscanf( c, "%d,%d@%u", &x, &y, &at ) == 3 && mouseIndex == s_mouseDone && SDL_GetTicks() >= at )
        {
            SDL_Event motion;
            memset( &motion, 0, sizeof( motion ) );
            motion.type = SDL_MOUSEMOTION;
            motion.motion.x = x;
            motion.motion.y = y;
            SDL_PushEvent( &motion );
            s_mouseDone++;
        }
        c = strchr( c, '@' );
        c = c ? strchr( c, ',' ) : NULL;
        if( c ) c++;
    }
    // debug: SHAR_QUIT_AT=ms closes the window at that time (tests shutting down from any context; Ctrl+C is the same)
    static const char* s_quitAt = getenv( "SHAR_QUIT_AT" );
    static bool s_quitPushed = false;
    if( s_quitAt && !s_quitPushed && SDL_GetTicks() >= (unsigned int) atoi( s_quitAt ) )
    {
        SDL_Event quitEvent;
        memset( &quitEvent, 0, sizeof( quitEvent ) );
        quitEvent.type = SDL_QUIT;
        SDL_PushEvent( &quitEvent );
        s_quitPushed = true;
        fprintf( stderr, "SHAR_QUIT_AT: closing the window\n" );
    }
    // debug: SHAR_MINIMIZE_SCRIPT="startms-endms,..." minimises the window for those times (tests hidden windows)
    static const char* s_minimizeScript = getenv( "SHAR_MINIMIZE_SCRIPT" );
    static bool s_minimized = false;
    bool minimize = false;
    for( const char* c = s_minimizeScript; c && *c; )
    {
        unsigned int from = 0, to = 0;
        if( sscanf( c, "%u-%u", &from, &to ) == 2 && SDL_GetTicks() >= from && SDL_GetTicks() < to )
        {
            minimize = true;
        }
        c = strchr( c, ',' );
        if( c ) c++;
    }
    if( s_minimizeScript && minimize != s_minimized )
    {
        if( minimize ) SDL_MinimizeWindow( (SDL_Window*) mhWnd ); else SDL_RestoreWindow( (SDL_Window*) mhWnd );
        s_minimized = minimize;
    }
    while( SDL_PollEvent( &e ) )
    {
        radControllerSDLHandleEvent( &e );                 // wheel + controller hot-plug for radcore input

        InputManager* pInputManager = GetInputManager();
        FEMouse* pFEMouse = pInputManager ? pInputManager->GetFEMouse() : NULL;

        switch( e.type )
        {
        case SDL_QUIT:
            quit = true;                                    // WM_DESTROY -> WM_QUIT
            break;

        case SDL_WINDOWEVENT:
            switch( e.window.event )
            {
            case SDL_WINDOWEVENT_FOCUS_GAINED:              // WM_ACTIVATEAPP (TRUE)
            case SDL_WINDOWEVENT_FOCUS_LOST:                // WM_ACTIVATEAPP (FALSE)
                {
                    bool active = ( e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED );
                    if( spInstance != NULL && spInstance->mpContext != NULL )
                    {
                        if( active )
                        {
                            RenderFlow* rf = GetRenderFlow();
                            rf->SetGamma( rf->GetGamma() );
                        }
                        if( pFEMouse )
                        {
                            pFEMouse->SetInGameOverride( !active );
                        }
                    }
                }
                break;

            case SDL_WINDOWEVENT_LEAVE:                     // WM_MOUSELEAVE
                if( pFEMouse )
                {
                    pFEMouse->getCursor()->SetVisible( false );
                }
                break;

            case SDL_WINDOWEVENT_ENTER:
                // Win32 never showed the cursor again after WM_MOUSELEAVE (ClipCursor kept the pointer in the
                // window); now that menus let it leave, bring the front-end cursor back when it returns
                if( pFEMouse && !pFEMouse->IsInGameMode() )
                {
                    pFEMouse->getCursor()->SetVisible( true );
                }
                break;
            }
            break;

        case SDL_KEYDOWN:
            // Linux port: M turns the user's shaders (shaders/: post-processing and material shaders) on and off,
            // for comparing; the game's input reads the key as usual
            if( e.key.keysym.scancode == SDL_SCANCODE_M && !e.key.repeat )
            {
                pglToggleUserShaders();
            }
            break;

        case SDL_MOUSEMOTION:                               // WM_MOUSEMOVE
            if( pFEMouse && pFEMouse->DidWeMove( e.motion.x, e.motion.y ) )
            {
                int w, h;
                SDL_GetWindowSize( (SDL_Window*) mhWnd, &w, &h );
                pFEMouse->Move( e.motion.x, e.motion.y, w, h );
                pFEMouse->SetHiddenForGamepad( false );
            }
            ShowTheCursor( false );
            break;

        case SDL_MOUSEBUTTONDOWN:                           // WM_LBUTTONDOWN
            if( pFEMouse && e.button.button == SDL_BUTTON_LEFT )
            {
                if( pFEMouse->IsHiddenForGamepad() )
                {
                    // the first click only brings the cursor back: it was not visibly over anything
                    pFEMouse->SetHiddenForGamepad( false );
                    s_swallowLeftUp = true;
                }
                else
                {
                    pFEMouse->ButtonDown( BUTTON_LEFT );
                }
            }
            break;

        case SDL_MOUSEBUTTONUP:                             // WM_LBUTTONUP
            if( pFEMouse && e.button.button == SDL_BUTTON_LEFT )
            {
                if( s_swallowLeftUp )
                {
                    s_swallowLeftUp = false;
                }
                else
                {
                    pFEMouse->ButtonUp( BUTTON_LEFT );
                }
            }
            break;

        // A gamepad in use hides the front-end cursor until the mouse moves or clicks again (sticks and
        // triggers count past half travel, so drift does not)
        case SDL_CONTROLLERBUTTONDOWN:
            if( pFEMouse )
            {
                pFEMouse->SetHiddenForGamepad( true );
            }
            break;

        case SDL_CONTROLLERAXISMOTION:
            if( pFEMouse && ( e.caxis.value > 16384 || e.caxis.value < -16384 ) )
            {
                pFEMouse->SetHiddenForGamepad( true );
            }
            break;

        default:
            break;
        }
    }
    return quit;
}

#endif // RAD_LINUX
