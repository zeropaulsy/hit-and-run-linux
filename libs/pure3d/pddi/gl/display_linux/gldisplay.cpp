//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#if defined(RAD_WIN32) && !defined(RAD_LINUX)
    #include <pddi/gl/display_win32/gldisplay.hpp>
    #include <pddi/gl/display_win32/gl.hpp>
#endif

#ifdef RAD_LINUX
    #include <pddi/gl/display_linux/gldisplay.hpp>
    #include <pddi/gl/display_linux/gl.hpp>
#endif


#include <pddi/gl/glcon.hpp>
#include <pddi/gl/glpost.hpp>

// the Vulkan renderer's side of the game's calls (vk/vkcore.cpp, vk/vkdisplay.cpp)
bool pvkActive();
void pvkPostProcessScene();
void pvkSetAntialiasing(int samples);
void pvkSetTextureFiltering(bool mipmaps, float anisotropy);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <assert.h>
#include <math.h>

#include <unistd.h>
#include <sys/time.h>

// vsync state, see SwapWithVsync
static bool s_vsync = false;            // wanted: on unless SHAR_VSYNC=0 or unsupported
static bool s_vsyncPaused = false;
static Uint32 s_vsyncProbeUntil = 0;    // just turned back on: until then one slow swap pauses it again
static int s_slowSwaps = 0;
static Uint32 s_nextVsyncProbe = 0;
static Uint32 s_vsyncProbeInterval = 0;  // doubles while retries fail

// Linux port: a core profile lists extensions one at a time (glGetString(GL_EXTENSIONS) is gone)
bool pglDisplay::CheckExtension( char *extName )
{
    if(extName == NULL)
        return false;
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for(GLint i = 0; i < n; i++)
    {
        const char* e = (const char*)glGetStringi(GL_EXTENSIONS, i);
        if(e && strcmp(e, extName) == 0)
            return true;
    }
    return false;
}

// SHAR_GL_DEBUG=1 (debug context, see linuxplatform_sdl.cpp): the driver reports errors and warnings as they happen
static void APIENTRY DebugMessage( GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length,
                                   const GLchar* message, const void* user )
{
    if( severity == GL_DEBUG_SEVERITY_NOTIFICATION )
        return;
    static int s_count = 0;
    if( s_count++ < 200 )
    {
        fprintf( stderr, "GL debug (%s): %s\n", type == GL_DEBUG_TYPE_ERROR ? "error" :
                 severity == GL_DEBUG_SEVERITY_HIGH ? "high" : severity == GL_DEBUG_SEVERITY_MEDIUM ? "medium" : "low", message );
    }
}

void pglSetTextureFiltering( int mipmaps, int anisotropy );
static pglDisplay* s_display = NULL;      // the display (one), for the game's calls into the renderer

pglDisplay::pglDisplay()
{
    s_display = this;
    msaaRequested = msaaSamples = msaaWidth = msaaHeight = 0;
    msaaFramebuffer = msaaColour = msaaDepth = 0;
    msaaBound = false;
    SetAntialiasing( 4 );       // until the settings are read (Win32Platform's default too)
    mode = PDDI_DISPLAY_WINDOW;
    winWidth = 640;
    winHeight = 480;
    winBitDepth = 32;

    context = NULL;

    window = NULL;
    glContext = NULL;

    extBGRA = false;

    gammaR = gammaG = gammaB = 1.0f;

    reset = true;
	m_ForceVSync = false;
}

pglDisplay::~pglDisplay()
{
}

void pglDisplay::SetWindowHandle(void* sdlWindow)
{
    window = (SDL_Window*)sdlWindow;
}

void* pglDisplay::GetWindowHandle()
{
    return (void*)window;
}

// Direct3D 8 let every thread use the device; Pure3D loads (and sets some state) on worker threads.
// Worker threads get one of these shared GL contexts at thread start (shar_SetThreadStartHook), so their
// GL calls are valid: textures are shared with the render context, other state stays local to them.
#include <windows.h>
#include <pthread.h>
static const int kWorkerContexts = 6;
static SDL_Window*   s_WorkerWindow = NULL;
static SDL_GLContext s_WorkerContexts[ kWorkerContexts ];
static int           s_WorkerContextCount = 0;     // left in the pool
static int           s_WorkerContextsCreated = 0;
static pthread_mutex_t s_WorkerLock = PTHREAD_MUTEX_INITIALIZER;

static void AttachWorkerContext( void )
{
    if( SDL_GL_GetCurrentContext( ) != NULL ) return;
    pthread_mutex_lock( &s_WorkerLock );
    SDL_GLContext ctx = s_WorkerContextCount > 0 ? s_WorkerContexts[ --s_WorkerContextCount ] : NULL;
    pthread_mutex_unlock( &s_WorkerLock );
    if( ctx != NULL )
    {
        // Workers never draw to the window: no surface where the platform allows it (EGL, i.e. Wayland, where
        // the window's surface may be current on one thread only, so binding it failed while the render thread
        // held it); GLX needs a drawable and lets several threads share the window's.
        if( SDL_GL_MakeCurrent( NULL, ctx ) != 0 && SDL_GL_MakeCurrent( s_WorkerWindow, ctx ) != 0 )
        {
            fprintf( stderr, "PDDI: cannot attach a worker GL context: %s\n", SDL_GetError( ) );
            pthread_mutex_lock( &s_WorkerLock );
            s_WorkerContexts[ s_WorkerContextCount++ ] = ctx;      // back to the pool
            pthread_mutex_unlock( &s_WorkerLock );
        }
    }
    else
    {
        fprintf( stderr, "PDDI: no GL context left for a worker thread; its GL calls will fail\n" );
    }
}

static void CreateWorkerContexts( SDL_Window* window, SDL_GLContext mainContext )
{
    if( s_WorkerWindow != NULL ) return;
    s_WorkerWindow = window;
    SDL_GL_SetAttribute( SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 1 );
    for( int i = 0; i < kWorkerContexts; i++ )
    {
        SDL_GLContext ctx = SDL_GL_CreateContext( window );     // shares with the current (main) context
        if( ctx == NULL )
        {
            fprintf( stderr, "PDDI: created %d of %d worker GL contexts: %s\n", i, kWorkerContexts, SDL_GetError( ) );
            break;
        }
        s_WorkerContexts[ s_WorkerContextCount++ ] = ctx;
        s_WorkerContextsCreated++;
        SDL_GL_MakeCurrent( window, mainContext );
    }
    SDL_GL_SetAttribute( SDL_GL_SHARE_WITH_CURRENT_CONTEXT, 0 );
    SDL_GL_MakeCurrent( window, mainContext );
    shar_SetThreadStartHook( AttachWorkerContext );
}

// SDL2 replaces the original glX code: the platform layer creates the SDL window (with SDL_WINDOW_OPENGL
// and the GL attributes already set); here the GL context is created on it and made current.
bool pglDisplay::InitDisplay(const pddiDisplayInit* init)
{
    assert(window && "pglDisplay: SetWindowHandle(SDL_Window*) must be called before InitDisplay");
    mode = init->displayMode;
    winBitDepth = init->bpp;

    // Borderless fullscreen covers the desktop at its own size (no display mode change). Exclusive fullscreen
    // switches the display to the mode closest to the requested size (rendering below the native resolution,
    // e.g. for speed). In a window the requested size is the window size.
    if(mode == PDDI_DISPLAY_FULLSCREEN_DESKTOP)
    {
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
    }
    else if(mode == PDDI_DISPLAY_FULLSCREEN)
    {
        // leave fullscreen first: switching straight from borderless (or between exclusive modes) kept the old
        // size under XWayland
        SDL_SetWindowFullscreen(window, 0);
        SDL_DisplayMode want, closest;
        memset(&want, 0, sizeof(want));
        want.w = init->xsize;
        want.h = init->ysize;
        int display = SDL_GetWindowDisplayIndex(window);
        if(SDL_GetClosestDisplayMode(display < 0 ? 0 : display, &want, &closest) != NULL)
        {
            SDL_SetWindowDisplayMode(window, &closest);
        }
        SDL_SetWindowSize(window, init->xsize, init->ysize);
        SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN);
    }
    else
    {
        SDL_SetWindowFullscreen(window, 0);
        SDL_SetWindowSize(window, init->xsize, init->ysize);
    }

    if(!glContext)
    {
        glContext = SDL_GL_CreateContext(window);
        if(!glContext)
        {
            fprintf(stderr, "PDDI: SDL_GL_CreateContext failed (the game needs OpenGL 3.3): %s\n", SDL_GetError());
            return false;
        }
    }
    if(SDL_GL_MakeCurrent(window, glContext) != 0)
    {
        fprintf(stderr, "PDDI: SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        return false;
    }

    static bool s_reported = false;
    if(!s_reported)
    {
        fprintf(stderr, "PDDI: OpenGL %s, %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
        s_reported = true;
        GLint flags = 0;
        glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
        if((flags & GL_CONTEXT_FLAG_DEBUG_BIT) && CheckExtension((char*)"GL_KHR_debug"))
        {
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(DebugMessage, NULL);
            fprintf(stderr, "PDDI: GL debug output on\n");
        }
    }
    if(!pipeline.Init())
    {
        return false;
    }
    // Linux port: always sync to the display (as the D3D8 build effectively did). The game's physics and AI
    // were tuned for 30-60 fps; unsynced it ran at thousands of fps with 0 ms frames (NaN traffic, unstable cars).
    // The main loop also caps the frame rate (game.cpp) in case the driver ignores this.
    const char* vs = getenv( "SHAR_VSYNC" );            // SHAR_VSYNC=0 turns it off (debugging)
    s_vsync = !( vs && vs[0] == '0' );
    s_vsyncPaused = false;
    if( SDL_GL_SetSwapInterval( s_vsync ? 1 : 0 ) != 0 )
    {
        fprintf(stderr, "PDDI: vsync unavailable (%s); relying on the frame cap\n", SDL_GetError());
        s_vsync = false;
    }

    extern int g_pglMaxTextureSize;          // cached for texture creation on loader threads (gltex.cpp)
    g_pglMaxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &g_pglMaxTextureSize);
    if(g_pglMaxTextureSize <= 0)
    {
        // seen with the GPU's memory nearly full: the context exists but answers nothing
        fprintf(stderr, "PDDI: the GL context gave no max texture size (out of video memory?); assuming 2048\n");
        g_pglMaxTextureSize = 2048;
    }

    // DXT textures stay compressed on the GPU where the driver takes them (every desktop driver; Mesa since 19.3)
    extern bool g_pglTextureS3TC;           // gltex.cpp
    g_pglTextureS3TC = CheckExtension((char*)"GL_EXT_texture_compression_s3tc") && getenv("SHAR_GL_NO_S3TC") == NULL;
    if(!g_pglTextureS3TC)
        fprintf(stderr, "PDDI: no S3TC texture compression: DXT textures are decompressed\n");

    // anisotropic filtering: core in GL 4.6, an extension (everywhere) before
    extern float g_pglMaxAnisotropy;        // gltex.cpp
    g_pglMaxAnisotropy = 0.0f;
    if( CheckExtension( (char*)"GL_EXT_texture_filter_anisotropic" ) || CheckExtension( (char*)"GL_ARB_texture_filter_anisotropic" ) )
    {
        glGetFloatv( 0x84FF, &g_pglMaxAnisotropy );     // GL_MAX_TEXTURE_MAX_ANISOTROPY
    }
    pglSetTextureFiltering( -1, -1 );       // the current settings, for the pipeline (and the environment's overrides)

    CreateWorkerContexts(window, glContext);
    if(s_WorkerContextsCreated == 0)
    {
        return false;       // loader threads would make GL calls with no context (crashes inside libGL)
    }

    int w = init->xsize, h = init->ysize;
    if(mode == PDDI_DISPLAY_FULLSCREEN || mode == PDDI_DISPLAY_FULLSCREEN_DESKTOP)
    {
        SDL_GL_GetDrawableSize(window, &w, &h);     // SwapBuffers keeps following it afterwards
    }
    return InitDisplay(w, h, init->bpp);
}

bool pglDisplay::InitDisplay(int x, int y, int bpp)
{
    reset = true;

    winWidth = x;
    winHeight = y;

    extBGRA = true;     // GL_BGRA uploads are core OpenGL

    //sprintf(userDisplayInfo[0].description,"OpenGL - Vendor: %s, Renderer: %s, Version: %s",glVendor,glRenderer,glVersion);

    return true;
}

extern pddiDisplayInfo displayInfo[2];

pddiDisplayInfo* pglDisplay::GetDisplayInfo(void)
{
    return &displayInfo[0];
}

unsigned pglDisplay::GetFreeTextureMem()
{
    return unsigned(-1);
}

unsigned pglDisplay::GetBufferMask()
{
    return ~0U;
}

int pglDisplay::GetHeight()
{
    return winHeight;
}

int pglDisplay::GetWidth()
{
    return winWidth;
}

int pglDisplay::GetDepth()
{
    return winBitDepth;
}

pddiDisplayMode pglDisplay::GetDisplayMode(void)
{
    return mode;
}

int pglDisplay::GetNumColourBuffer(void)
{
    return 2;
}

void pglDisplay::GetGamma(float* r, float* g, float* b)
{
    *r = gammaR;
    *g = gammaG;
    *b = gammaB;
}

void pglDisplay::SetGamma(float r, float g, float b)
{
    gammaR = r;
    gammaG = g;
    gammaB = b;
}

// Debug aid (Linux port): SHAR_SHOT_EVERY=<seconds> saves the back buffer as shot_<n>.png in the working
// directory every <seconds>. Uncompressed PNG (stored deflate blocks), so no extra libraries are needed.
static unsigned Crc32(const unsigned char* d, size_t n, unsigned c = 0xffffffffu)
{
    for(size_t i = 0; i < n; i++)
    {
        c ^= d[i];
        for(int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
    }
    return c;
}

static void PutBE(FILE* f, unsigned v) { unsigned char b[4] = { (unsigned char)(v >> 24), (unsigned char)(v >> 16), (unsigned char)(v >> 8), (unsigned char)v }; fwrite(b, 1, 4, f); }

static void WriteChunk(FILE* f, const char* type, const unsigned char* data, unsigned len)
{
    PutBE(f, len);
    unsigned char* buf = (unsigned char*)malloc(len + 4);
    memcpy(buf, type, 4);
    if(len) memcpy(buf + 4, data, len);
    fwrite(buf, 1, len + 4, f);
    PutBE(f, Crc32(buf, len + 4) ^ 0xffffffffu);
    free(buf);
}

static void SavePNG(const char* path, int w, int h, const unsigned char* rgbaBottomUp);

// for the Vulkan pddi's screenshots (vk/vkdisplay.cpp)
void pglSavePNG(const char* path, int w, int h, const unsigned char* rgbaBottomUp)
{
    SavePNG(path, w, h, rgbaBottomUp);
}

static void SavePNG(const char* path, int w, int h, const unsigned char* rgbaBottomUp)
{
    FILE* f = fopen(path, "wb");
    if(!f) return;
    static const unsigned char sig[8] = { 137, 'P', 'N', 'G', 13, 10, 26, 10 };
    fwrite(sig, 1, 8, f);
    unsigned char ihdr[13] = { 0 };
    ihdr[0] = w >> 24; ihdr[1] = w >> 16; ihdr[2] = w >> 8; ihdr[3] = w;
    ihdr[4] = h >> 24; ihdr[5] = h >> 16; ihdr[6] = h >> 8; ihdr[7] = h;
    ihdr[8] = 8; ihdr[9] = 2;                                  // 8-bit RGB
    WriteChunk(f, "IHDR", ihdr, 13);

    size_t rowLen = 1 + (size_t)w * 3, rawLen = rowLen * h;
    unsigned char* raw = (unsigned char*)malloc(rawLen);
    for(int y = 0; y < h; y++)
    {
        const unsigned char* src = rgbaBottomUp + (size_t)(h - 1 - y) * w * 4;
        unsigned char* dst = raw + y * rowLen;
        *dst++ = 0;
        for(int x = 0; x < w; x++) { *dst++ = src[0]; *dst++ = src[1]; *dst++ = src[2]; src += 4; }
    }
    size_t blocks = (rawLen + 65534) / 65535;
    size_t zlen = 2 + rawLen + blocks * 5 + 4;
    unsigned char* z = (unsigned char*)malloc(zlen);
    unsigned char* zp = z;
    *zp++ = 0x78; *zp++ = 0x01;
    unsigned a = 1, b = 0;
    for(size_t off = 0; off < rawLen; off += 65535)
    {
        unsigned n = (unsigned)((rawLen - off) < 65535 ? (rawLen - off) : 65535);
        *zp++ = (off + n >= rawLen) ? 1 : 0;
        *zp++ = n & 255; *zp++ = n >> 8; *zp++ = ~n & 255; *zp++ = (~n >> 8) & 255;
        memcpy(zp, raw + off, n); zp += n;
        for(unsigned i = 0; i < n; i++) { a = (a + raw[off + i]) % 65521; b = (b + a) % 65521; }
    }
    unsigned adler = (b << 16) | a;
    *zp++ = adler >> 24; *zp++ = adler >> 16; *zp++ = adler >> 8; *zp++ = adler;
    WriteChunk(f, "IDAT", z, (unsigned)(zp - z));
    WriteChunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z);
    free(raw);
}

// Linux port: gamma. The D3D8 display set the video card's gamma ramp (out = in^(1/gamma); the display menu's slider
// gives 0.5-2.5). SDL's gamma ramp does nothing under (X)Wayland and in a window would recolour the whole desktop, so a
// last pass before each swap copies the back buffer to a texture and draws it back through a small shader. Skipped at
// gamma 1. The GL state it changes is restored afterwards, so the pddi's cached state still matches.
static pglProgram s_gammaProgram;
static GLint  s_gammaInvLoc = -1;
static GLuint s_gammaTexture = 0;
static GLuint s_gammaVAO = 0;
static int    s_gammaTexW = 0, s_gammaTexH = 0;

static bool InitGammaPass()
{
    // one triangle covering the screen, made from the vertex index (no vertex data)
    static const char* vertexSource =
        "#version 330 core\n"
        "out vec2 uv;\n"
        "void main()\n"
        "{\n"
        "    vec2 p = vec2( gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0 );\n"
        "    uv = p * 0.5 + 0.5;\n"
        "    gl_Position = vec4( p, 0.0, 1.0 );\n"
        "}\n";
    static const char* fragmentSource =
        "#version 330 core\n"
        "in vec2 uv;\n"
        "uniform sampler2D image;\n"
        "uniform float invGamma;\n"
        "out vec4 fragColour;\n"
        "void main()\n"
        "{\n"
        "    vec3 c = texture( image, uv ).rgb;\n"
        "    fragColour = vec4( pow( c, vec3( invGamma ) ), 1.0 );\n"
        "}\n";
    if( !s_gammaProgram.Build( "gamma", vertexSource, fragmentSource ) )
        return false;
    glUseProgram( s_gammaProgram.Handle( ) );
    glUniform1i( s_gammaProgram.Uniform( "image" ), 0 );
    s_gammaInvLoc = s_gammaProgram.Uniform( "invGamma" );
    glUseProgram( 0 );
    glGenTextures( 1, &s_gammaTexture );
    glGenVertexArrays( 1, &s_gammaVAO );
    return true;
}

static void ApplyGamma( float gamma, int w, int h )
{
    if( gamma <= 0.0f || fabsf( gamma - 1.0f ) < 0.001f || w <= 0 || h <= 0 )
    {
        return;
    }
    static int s_ready = 0;     // 1 ready, -1 unavailable
    if( s_ready == 0 )
    {
        s_ready = InitGammaPass( ) ? 1 : -1;
        if( s_ready < 0 )
        {
            fprintf( stderr, "PDDI: the gamma shader does not build: the gamma setting has no effect\n" );
        }
    }
    if( s_ready < 0 )
    {
        return;
    }

    // what the pass changes (the pddi binds its own program and vertex array object before each draw)
    pglScreenPassState saved;
    saved.Save( );
    glViewport( 0, 0, w, h );
    glBindFramebuffer( GL_READ_FRAMEBUFFER, 0 );

    glBindTexture( GL_TEXTURE_2D, s_gammaTexture );
    if( w != s_gammaTexW || h != s_gammaTexH )
    {
        glTexImage2D( GL_TEXTURE_2D, 0, GL_RGB8, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
        glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
        s_gammaTexW = w;
        s_gammaTexH = h;
    }
    glReadBuffer( GL_BACK );
    glCopyTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, 0, 0, w, h );

    glUseProgram( s_gammaProgram.Handle( ) );
    glUniform1f( s_gammaInvLoc, 1.0f / gamma );
    glBindVertexArray( s_gammaVAO );
    glDrawArrays( GL_TRIANGLES, 0, 3 );
    glBindVertexArray( 0 );
    glUseProgram( 0 );

    saved.Restore( );
}

static void MaybeScreenshot(int w, int h)
{
    static int every = -1;
    static unsigned last = 0;
    static int n = 0;
    if(every < 0)
    {
        const char* e = getenv("SHAR_SHOT_EVERY");
        every = e ? atoi(e) : 0;
    }
    if(every <= 0) return;
    unsigned now = SDL_GetTicks();
    if(now - last < (unsigned)every * 1000) return;
    last = now;
    unsigned char* px = (unsigned char*)malloc((size_t)w * h * 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    char path[64];
    sprintf(path, "shot_%03d.png", n++);
    SavePNG(path, w, h, px);
    free(px);
}

// Vsync on a window nobody can see: the driver throttles presenting a minimised window (or one on a sleeping
// display) to about one swap a second, and the game's loads advance once per frame, so a load started or left
// running in the background stalled. When swaps keep taking that long, vsync is paused and the main loop's frame
// cap (game.cpp) paces the game; it comes back as soon as a minimised window is restored, and otherwise (sleeping
// display, covered window: SDL reports neither; under XWayland not even minimising, reliably) by retrying after
// 5 s, then less often (up to every 30 s) while it stays hidden. Test: SHAR_MINIMIZE_SCRIPT, SHAR_FRAME_LOG=1.

static void SwapWithVsync( SDL_Window* window )
{
    const Uint32 SLOW_SWAP_MS = 100, THROTTLED_SWAP_MS = 500, PROBE_MS = 3000;
    const Uint32 FIRST_RETRY_MS = 5000, MAX_RETRY_MS = 30000;
    bool minimised = ( SDL_GetWindowFlags( window ) & ( SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN ) ) != 0;
    if( s_vsyncPaused )
    {
        if( minimised )
        {
            s_nextVsyncProbe = 0;                           // retry as soon as it is restored
        }
        else if( SDL_GetTicks( ) >= s_nextVsyncProbe )
        {
            SDL_GL_SetSwapInterval( 1 );
            s_vsyncPaused = false;
            s_vsyncProbeUntil = SDL_GetTicks( ) + PROBE_MS;
        }
    }

    Uint32 start = SDL_GetTicks( );
    SDL_GL_SwapWindow( window );
    Uint32 took = SDL_GetTicks( ) - start;

    if( s_vsync && !s_vsyncPaused )
    {
        s_slowSwaps = ( took > SLOW_SWAP_MS ) ? s_slowSwaps + 1 : 0;
        bool probing = SDL_GetTicks( ) < s_vsyncProbeUntil;
        if( s_slowSwaps >= 2 || took >= THROTTLED_SWAP_MS || ( s_slowSwaps >= 1 && ( probing || minimised ) ) )
        {
            static bool s_reported = false;
            if( !s_reported )
            {
                fprintf( stderr, "PDDI: presenting is throttled (window hidden?): vsync paused, the frame cap paces the game\n" );
                s_reported = true;
            }
            SDL_GL_SetSwapInterval( 0 );
            s_vsyncPaused = true;
            s_vsyncProbeInterval = ( probing && s_vsyncProbeInterval > 0 ) ? s_vsyncProbeInterval * 2 : FIRST_RETRY_MS;
            if( s_vsyncProbeInterval > MAX_RETRY_MS ) s_vsyncProbeInterval = MAX_RETRY_MS;
            s_nextVsyncProbe = SDL_GetTicks( ) + s_vsyncProbeInterval;
            s_slowSwaps = 0;
        }
    }
}

// the scene stage of the user's post-processing (glpost.hpp), called by the game before it draws the GUI layer
void pglPostProcessScene()
{
    if( pvkActive( ) )
        pvkPostProcessScene( );        // the Vulkan renderer (vk/vkdisplay.cpp)
    else if( s_display )
        s_display->PostProcess( PGL_POST_SCENE );
}

void pglDisplay::PostProcess( int stage )
{
    ResolveFrame( );
    pglPostRun( (pglPostStage)stage, winWidth, winHeight, &pipeline );
}

//-----------------------------------------------------------------------------
// Multisample anti-aliasing
//-----------------------------------------------------------------------------

// texture filtering (Win32Platform::LoadConfig: simpsons.ini mipmaps=, anisotropy=; -1 keeps a value): mip levels
// are always made, so this applies at once. SHAR_MIPMAPS=0 / SHAR_ANISOTROPY=n override the settings.
void pglSetTextureFiltering( int mipmaps, int anisotropy )
{
    extern bool g_pglMipmaps;
    extern float g_pglAnisotropy;
    if( mipmaps >= 0 )
        g_pglMipmaps = mipmaps != 0;
    if( anisotropy >= 0 )
        g_pglAnisotropy = (float)anisotropy;
    const char* e = getenv( "SHAR_MIPMAPS" );
    if( e )
        g_pglMipmaps = atoi( e ) != 0;
    e = getenv( "SHAR_ANISOTROPY" );
    if( e )
        g_pglAnisotropy = (float)atoi( e );
    if( s_display )
        s_display->GetPipeline( )->SetTextureFiltering( g_pglMipmaps, g_pglAnisotropy );
    pvkSetTextureFiltering( g_pglMipmaps, g_pglAnisotropy );
}

// for the game's settings (Win32Platform::LoadConfig: simpsons.ini antialiasing=)
void pglSetAntialiasing( int samples )
{
    if( s_display )
        s_display->SetAntialiasing( samples );
    pvkSetAntialiasing( samples );
}

void pglDisplay::SetAntialiasing( int samples )
{
    const char* e = getenv( "SHAR_MSAA" );     // overrides the setting
    msaaRequested = e ? atoi( e ) : samples;
}

void pglDisplay::BeginFrameTarget( void )
{
    msaaBound = false;
    pipeline.SetMultisampled( false );
    int samples = msaaRequested;
    static GLint s_maxSamples = -1;
    if( samples > 1 && s_maxSamples < 0 )
    {
        s_maxSamples = 0;
        glGetIntegerv( GL_MAX_SAMPLES, &s_maxSamples );
    }
    if( samples > s_maxSamples )
        samples = s_maxSamples;
    if( samples < 2 || winWidth <= 0 || winHeight <= 0 )
    {
        if( msaaSamples )
        {
            fprintf( stderr, "PDDI: anti-aliasing off\n" );
            msaaSamples = 0;
        }
        return;
    }

    if( samples != msaaSamples || winWidth != msaaWidth || winHeight != msaaHeight )
    {
        if( !msaaFramebuffer )
        {
            glGenFramebuffers( 1, &msaaFramebuffer );
            glGenRenderbuffers( 1, &msaaColour );
            glGenRenderbuffers( 1, &msaaDepth );
        }
        while( glGetError( ) != GL_NO_ERROR ) {}
        glBindRenderbuffer( GL_RENDERBUFFER, msaaColour );
        glRenderbufferStorageMultisample( GL_RENDERBUFFER, samples, GL_RGBA8, winWidth, winHeight );
        glBindRenderbuffer( GL_RENDERBUFFER, msaaDepth );
        glRenderbufferStorageMultisample( GL_RENDERBUFFER, samples, GL_DEPTH24_STENCIL8, winWidth, winHeight );   // as the window's
        glBindRenderbuffer( GL_RENDERBUFFER, 0 );
        glBindFramebuffer( GL_FRAMEBUFFER, msaaFramebuffer );
        glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, msaaColour );
        glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepth );
        GLenum status = glCheckFramebufferStatus( GL_FRAMEBUFFER );
        GLenum error = glGetError( );
        glBindFramebuffer( GL_FRAMEBUFFER, 0 );
        if( status != GL_FRAMEBUFFER_COMPLETE || error != GL_NO_ERROR )
        {
            // out of video memory at a large size, typically: carry on without
            fprintf( stderr, "PDDI: %dx anti-aliasing unavailable at %dx%d (status 0x%x, error 0x%x): off\n",
                     samples, winWidth, winHeight, status, error );
            msaaRequested = 0;
            msaaSamples = 0;
            return;
        }
        if( samples != msaaSamples )
            fprintf( stderr, "PDDI: %dx anti-aliasing\n", samples );
        msaaSamples = samples;
        msaaWidth = winWidth;
        msaaHeight = winHeight;
    }

    glBindFramebuffer( GL_FRAMEBUFFER, msaaFramebuffer );
    msaaBound = true;
    pipeline.SetMultisampled( true );
}

void pglDisplay::ResolveFrame( void )
{
    if( !msaaBound )
        return;
    msaaBound = false;
    pipeline.SetMultisampled( false );      // what follows (the GUI layer) draws into the window

    // a blit is clipped by the scissor test; write masks are opened in case the driver applies them
    GLboolean scissor = glIsEnabled( GL_SCISSOR_TEST );
    GLboolean colourMask[ 4 ], depthMask;
    GLint stencilMask;
    glGetBooleanv( GL_COLOR_WRITEMASK, colourMask );
    glGetBooleanv( GL_DEPTH_WRITEMASK, &depthMask );
    glGetIntegerv( GL_STENCIL_WRITEMASK, &stencilMask );
    glDisable( GL_SCISSOR_TEST );
    glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
    glDepthMask( GL_TRUE );
    glStencilMask( 0xff );

    // depth and stencil too: the GUI layer is drawn into the window afterwards and may test against them
    glBindFramebuffer( GL_READ_FRAMEBUFFER, msaaFramebuffer );
    glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 );
    glBlitFramebuffer( 0, 0, msaaWidth, msaaHeight, 0, 0, msaaWidth, msaaHeight,
                       GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT, GL_NEAREST );
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );

    glStencilMask( stencilMask );
    glDepthMask( depthMask );
    glColorMask( colourMask[ 0 ], colourMask[ 1 ], colourMask[ 2 ], colourMask[ 3 ] );
    if( scissor )
        glEnable( GL_SCISSOR_TEST );
}

void pglDisplay ::SwapBuffers()
{
    s_display = this;
    PostProcess( PGL_POST_FINAL );
    ApplyGamma( gammaR, winWidth, winHeight );
    pipeline.Unbind( );     // the gamma pass bound its own program and vertex array object
    MaybeScreenshot( winWidth, winHeight );
    SwapWithVsync( window );
    reset = false;      // as the Win32 display: otherwise every BeginFrame treats the device as reset and
                        // re-uploads (and leaks) every texture

    // the window is resizable: follow its size (viewports and aspect ratios are worked out from it each frame)
    int w, h;
    SDL_GL_GetDrawableSize( window, &w, &h );
    if( w > 0 && h > 0 )
    {
        if( w != winWidth || h != winHeight )
            fprintf( stderr, "PDDI: drawing at %dx%d\n", w, h );
        winWidth = w;
        winHeight = h;
    }
}

unsigned pglDisplay::Screenshot(pddiColour* buffer, int nBytes)
{
    // not implemented under linux
    assert(0 && "PDDI: pddiDisplay::ScreenShot() - Not implemented under linux.");
    return 0;
}

// SDL2 replacement for EnumDisplaySettings. The game offers fixed 4:3 resolutions (640x480..1600x1200)
// that modern monitors often do not list as modes; any size that fits the desktop works in a window,
// so those are reported (16 and 32 bpp, like Win32) plus the display's real fullscreen modes.
static void AddMode(pddiModeInfo* modes, int& n, int w, int h, int bpp)
{
    for(int i = 0; i < n; i++)
        if(modes[i].width == w && modes[i].height == h && modes[i].bpp == bpp)
            return;
    if(n >= 1024)
        return;
    modes[n].width = w;
    modes[n].height = h;
    modes[n].bpp = bpp;
    n++;
}

// for the Vulkan pddi (vk/vkdev.cpp)
unsigned pglFillDisplayModes(pddiModeInfo* displayModes)
{
    return pglDisplay::FillDisplayModes(displayModes);
}

unsigned pglDisplay::FillDisplayModes(pddiModeInfo* displayModes)
{
    int nModes = 0;
    if(!SDL_WasInit(SDL_INIT_VIDEO))
        SDL_InitSubSystem(SDL_INIT_VIDEO);

    SDL_DisplayMode desktop;
    int dw = 1600, dh = 1200;
    if(SDL_GetDesktopDisplayMode(0, &desktop) == 0)
    {
        dw = desktop.w;
        dh = desktop.h;
    }

    // every Win32Platform::Resolution (incl. the port's widescreen additions) that fits the desktop
    static const int gameRes[][2] = { {640,480}, {800,600}, {1024,768}, {1152,864}, {1280,1024}, {1600,1200},
                                      {1280,720}, {1920,1080}, {2560,1440}, {2560,1600} };
    for(int r = 0; r < (int)(sizeof(gameRes) / sizeof(gameRes[0])); r++)
    {
        if(gameRes[r][0] <= dw && gameRes[r][1] <= dh)
        {
            AddMode(displayModes, nModes, gameRes[r][0], gameRes[r][1], 16);
            AddMode(displayModes, nModes, gameRes[r][0], gameRes[r][1], 32);
        }
    }

    for(int m = 0; m < SDL_GetNumDisplayModes(0); m++)
    {
        SDL_DisplayMode mode;
        if(SDL_GetDisplayMode(0, m, &mode) == 0)
        {
            AddMode(displayModes, nModes, mode.w, mode.h, 16);
            AddMode(displayModes, nModes, mode.w, mode.h, 32);
        }
    }
    return nModes;
}
    
void pglDisplay::BeginTiming()
{
    gettimeofday(&beginTime,NULL);
}

float pglDisplay::EndTiming()
{
    struct timeval tv;
    gettimeofday(&tv,NULL);
    // return time in milliseconds
    return (float)(tv.tv_sec - beginTime.tv_sec) * 1e3 + (float)(tv.tv_usec - beginTime.tv_usec) * 1e-3;
}

void pglDisplay::BeginContext(void)
{
}

void pglDisplay::EndContext(void)
{
}
