//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// Linux port: SDL2 implementation of the radController system, replacing directinputcontroller.cpp.
//
// It exposes the same controllers, input points and values as the DirectInput version, because the game
// identifies everything by DirectInput conventions:
//   - locations "Keyboard0", "Mouse0", "Joystick<n>"
//   - keyboard points indexed through VirtualKeyToIndex[ DIK_code ]
//   - mouse points of type RelXAxis/RelYAxis/RelZAxis and Button named "Button <n>"
//   - joystick points of type XAxis/YAxis/ZAxis/RxAxis/RyAxis/POV/Button, laid out the way DirectInput
//     presents an Xbox-style pad (sticks on X/Y and Rx/Ry, triggers combined on Z, d-pad as POV 0,
//     buttons A B X Y LB RB Back Start LS RS)
//   - values computed exactly like radControllerInputPointDirectInput::CalculateNewValue
// Force feedback: game controllers have the output points "LeftMotor" (low frequency) and "RightMotor" (high
// frequency) that the game's RumbleEffect looks for (DirectInput never offered them, so PC gamepads never
// rumbled), sent to SDL_GameControllerRumble. Steering-wheel effects (Spring, Damper, ...) are not provided.
// SHAR_RUMBLE_LOG=1 prints every motor change.
//=============================================================================

#include "pch.hpp"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <vector>
#include <SDL2/SDL.h>
#include <radcontroller.hpp>
#include <raddebug.hpp>
#include <radtime.hpp>
#include <dinput.h>

//=============================================================================
// Key tables
//=============================================================================

static int s_VirtualKeyToIndex[ 256 ];
const int* VirtualKeyToIndex = &s_VirtualKeyToIndex[ -1 ];      // DIK_* starts at 1 not 0 (as on Win32)
const int* VirtualJoyKeyToIndex = &s_VirtualKeyToIndex[ -48 ];

static radMemoryAllocator g_ControllerSystemAllocator = RADMEMORY_ALLOC_DEFAULT;
static bool s_InputBlocked = false;     // radControllerSDLSetInputBlocked: keyboard and mouse read as idle

// debug: SHAR_KEY_SCRIPT="code@startms-endms,..." holds DIK key codes (1000+n = mouse button n) during time windows.
// Parsed once: rescanning the string for every key on every poll starved the load thread under valgrind.
static bool KeyScriptHolds( int code, unsigned int now )
{
    struct Window { int code; unsigned int from, to; };
    static Window s_windows[ 1024 ];
    static int s_count = -1;
    if ( s_count < 0 )
    {
        s_count = 0;
        for ( const char* c = getenv( "SHAR_KEY_SCRIPT" ); c && *c && s_count < 1024; )
        {
            Window& w = s_windows[ s_count ];
            if ( sscanf( c, "%d@%u-%u", &w.code, &w.from, &w.to ) == 3 ) s_count++;
            c = strchr( c, ',' ); if ( c ) c++;
        }
    }
    for ( int i = 0; i < s_count; i++ )
    {
        if ( s_windows[ i ].code == code && now >= s_windows[ i ].from && now < s_windows[ i ].to ) return true;
    }
    return false;
}

struct DikToScancode { int dik; SDL_Scancode sc; };

static const DikToScancode s_KeyTable[] =
{
    { DIK_ESCAPE, SDL_SCANCODE_ESCAPE }, { DIK_1, SDL_SCANCODE_1 }, { DIK_2, SDL_SCANCODE_2 },
    { DIK_3, SDL_SCANCODE_3 }, { DIK_4, SDL_SCANCODE_4 }, { DIK_5, SDL_SCANCODE_5 }, { DIK_6, SDL_SCANCODE_6 },
    { DIK_7, SDL_SCANCODE_7 }, { DIK_8, SDL_SCANCODE_8 }, { DIK_9, SDL_SCANCODE_9 }, { DIK_0, SDL_SCANCODE_0 },
    { DIK_MINUS, SDL_SCANCODE_MINUS }, { DIK_EQUALS, SDL_SCANCODE_EQUALS }, { DIK_BACK, SDL_SCANCODE_BACKSPACE },
    { DIK_TAB, SDL_SCANCODE_TAB }, { DIK_Q, SDL_SCANCODE_Q }, { DIK_W, SDL_SCANCODE_W }, { DIK_E, SDL_SCANCODE_E },
    { DIK_R, SDL_SCANCODE_R }, { DIK_T, SDL_SCANCODE_T }, { DIK_Y, SDL_SCANCODE_Y }, { DIK_U, SDL_SCANCODE_U },
    { DIK_I, SDL_SCANCODE_I }, { DIK_O, SDL_SCANCODE_O }, { DIK_P, SDL_SCANCODE_P },
    { DIK_LBRACKET, SDL_SCANCODE_LEFTBRACKET }, { DIK_RBRACKET, SDL_SCANCODE_RIGHTBRACKET },
    { DIK_RETURN, SDL_SCANCODE_RETURN }, { DIK_LCONTROL, SDL_SCANCODE_LCTRL },
    { DIK_A, SDL_SCANCODE_A }, { DIK_S, SDL_SCANCODE_S }, { DIK_D, SDL_SCANCODE_D }, { DIK_F, SDL_SCANCODE_F },
    { DIK_G, SDL_SCANCODE_G }, { DIK_H, SDL_SCANCODE_H }, { DIK_J, SDL_SCANCODE_J }, { DIK_K, SDL_SCANCODE_K },
    { DIK_L, SDL_SCANCODE_L }, { DIK_SEMICOLON, SDL_SCANCODE_SEMICOLON }, { DIK_APOSTROPHE, SDL_SCANCODE_APOSTROPHE },
    { DIK_GRAVE, SDL_SCANCODE_GRAVE }, { DIK_LSHIFT, SDL_SCANCODE_LSHIFT }, { DIK_BACKSLASH, SDL_SCANCODE_BACKSLASH },
    { DIK_Z, SDL_SCANCODE_Z }, { DIK_X, SDL_SCANCODE_X }, { DIK_C, SDL_SCANCODE_C }, { DIK_V, SDL_SCANCODE_V },
    { DIK_B, SDL_SCANCODE_B }, { DIK_N, SDL_SCANCODE_N }, { DIK_M, SDL_SCANCODE_M },
    { DIK_COMMA, SDL_SCANCODE_COMMA }, { DIK_PERIOD, SDL_SCANCODE_PERIOD }, { DIK_SLASH, SDL_SCANCODE_SLASH },
    { DIK_RSHIFT, SDL_SCANCODE_RSHIFT }, { DIK_MULTIPLY, SDL_SCANCODE_KP_MULTIPLY }, { DIK_LMENU, SDL_SCANCODE_LALT },
    { DIK_SPACE, SDL_SCANCODE_SPACE }, { DIK_CAPITAL, SDL_SCANCODE_CAPSLOCK },
    { DIK_F1, SDL_SCANCODE_F1 }, { DIK_F2, SDL_SCANCODE_F2 }, { DIK_F3, SDL_SCANCODE_F3 }, { DIK_F4, SDL_SCANCODE_F4 },
    { DIK_F5, SDL_SCANCODE_F5 }, { DIK_F6, SDL_SCANCODE_F6 }, { DIK_F7, SDL_SCANCODE_F7 }, { DIK_F8, SDL_SCANCODE_F8 },
    { DIK_F9, SDL_SCANCODE_F9 }, { DIK_F10, SDL_SCANCODE_F10 }, { DIK_NUMLOCK, SDL_SCANCODE_NUMLOCKCLEAR },
    { DIK_SCROLL, SDL_SCANCODE_SCROLLLOCK }, { DIK_NUMPAD7, SDL_SCANCODE_KP_7 }, { DIK_NUMPAD8, SDL_SCANCODE_KP_8 },
    { DIK_NUMPAD9, SDL_SCANCODE_KP_9 }, { DIK_SUBTRACT, SDL_SCANCODE_KP_MINUS }, { DIK_NUMPAD4, SDL_SCANCODE_KP_4 },
    { DIK_NUMPAD5, SDL_SCANCODE_KP_5 }, { DIK_NUMPAD6, SDL_SCANCODE_KP_6 }, { DIK_ADD, SDL_SCANCODE_KP_PLUS },
    { DIK_NUMPAD1, SDL_SCANCODE_KP_1 }, { DIK_NUMPAD2, SDL_SCANCODE_KP_2 }, { DIK_NUMPAD3, SDL_SCANCODE_KP_3 },
    { DIK_NUMPAD0, SDL_SCANCODE_KP_0 }, { DIK_DECIMAL, SDL_SCANCODE_KP_PERIOD }, { DIK_OEM_102, SDL_SCANCODE_NONUSBACKSLASH },
    { DIK_F11, SDL_SCANCODE_F11 }, { DIK_F12, SDL_SCANCODE_F12 },
    { DIK_NUMPADENTER, SDL_SCANCODE_KP_ENTER }, { DIK_RCONTROL, SDL_SCANCODE_RCTRL },
    { DIK_DIVIDE, SDL_SCANCODE_KP_DIVIDE }, { DIK_SYSRQ, SDL_SCANCODE_PRINTSCREEN }, { DIK_RMENU, SDL_SCANCODE_RALT },
    { DIK_PAUSE, SDL_SCANCODE_PAUSE }, { DIK_HOME, SDL_SCANCODE_HOME }, { DIK_UP, SDL_SCANCODE_UP },
    { DIK_PRIOR, SDL_SCANCODE_PAGEUP }, { DIK_LEFT, SDL_SCANCODE_LEFT }, { DIK_RIGHT, SDL_SCANCODE_RIGHT },
    { DIK_END, SDL_SCANCODE_END }, { DIK_DOWN, SDL_SCANCODE_DOWN }, { DIK_NEXT, SDL_SCANCODE_PAGEDOWN },
    { DIK_INSERT, SDL_SCANCODE_INSERT }, { DIK_DELETE, SDL_SCANCODE_DELETE }, { DIK_LWIN, SDL_SCANCODE_LGUI },
    { DIK_RWIN, SDL_SCANCODE_RGUI }, { DIK_APPS, SDL_SCANCODE_APPLICATION },
};

//=============================================================================
// Input point
//=============================================================================

class radControllerInputPointSDL
    :
    public IRadControllerInputPoint,
    public radRefCount
{
public:
    IMPLEMENT_REFCOUNTED( "radControllerInputPointSDL" )

    enum Kind { AbsAxis, RelAxis, Button, POV };

    radControllerInputPointSDL( const char* name, const char* type, Kind kind, bool invert = false )
        :
        radRefCount( 0 ),
        m_Kind( kind ),
        m_Invert( invert ),
        m_Value( kind == POV ? 1.0f : ( kind == AbsAxis ? 0.5f : 0.0f ) ),
        m_Tolerance( 0.0f ),
        m_MinRange( 0.0f ),
        m_MaxRange( 1.0f ),
        m_TimeOfStateChange( radTimeGetMilliseconds( ) )
    {
        strncpy( m_Name, name, sizeof( m_Name ) - 1 );
        m_Name[ sizeof( m_Name ) - 1 ] = '\0';
        strncpy( m_Type, type, sizeof( m_Type ) - 1 );
        m_Type[ sizeof( m_Type ) - 1 ] = '\0';
    }

    // raw follows DirectInput units: abs axis 0..65535, rel axis delta, button 0/0x80, POV hundredths or -1
    void Update( int raw )
    {
        float newValue;
        switch( m_Kind )
        {
        case RelAxis: newValue = m_Value + (float) raw;                         break;
        case AbsAxis: newValue = (float) raw / 65535.0f;
                      if ( m_Invert ) newValue = 1.0f - newValue;               break;
        case Button:  newValue = ( raw & 0x80 ) ? 1.0f : 0.0f;                  break;
        case POV:     newValue = ( raw == -1 ) ? 1.0f : (float) raw / 36000.0f; break;
        default:      newValue = 0.0f;
        }
        if ( newValue == m_Value || fabsf( newValue - m_Value ) < m_Tolerance )
        {
            return;
        }
        m_Value = newValue;
        m_TimeOfStateChange = radTimeGetMilliseconds( );
        static int s_log = -1;
        if ( s_log < 0 ) { const char* e = getenv( "SHAR_INPUT_LOG" ); s_log = ( e && *e != '0' ) ? 1 : 0; }
        if ( s_log ) fprintf( stderr, "[input] %s (%s) = %g  callbacks=%u\n", m_Name, m_Type, m_Value, (unsigned) m_Callbacks.size( ) );

        AddRef( );                                                  // we are calling out
        for ( unsigned int i = 0; i < m_Callbacks.size( ); i++ )
        {
            m_Callbacks[ i ].pCallback->OnControllerInputPointChange( m_Callbacks[ i ].userData, m_Value );
        }
        Release( );
    }

    virtual const char* GetName( void ) { return m_Name; }
    virtual const char* GetType( void ) { return m_Type; }

    virtual void SetTolerance( float percentage )
    {
        m_Tolerance = percentage < 0.0f ? 0.0f : ( percentage > 1.0f ? 1.0f : percentage );
    }
    virtual float GetTolerance( void ) { return m_Tolerance; }

    virtual void RegisterControllerInputPointCallback( IRadControllerInputPointCallback* pCallback, unsigned int userData )
    {
        rAssert( pCallback != NULL );
        CallbackEntry e = { pCallback, userData };
        m_Callbacks.push_back( e );
    }

    virtual void UnRegisterControllerInputPointCallback( IRadControllerInputPointCallback* pCallback )
    {
        for ( unsigned int i = 0; i < m_Callbacks.size( ); )
        {
            if ( m_Callbacks[ i ].pCallback == pCallback ) m_Callbacks.erase( m_Callbacks.begin( ) + i );
            else i++;
        }
    }

    virtual float GetCurrentValue( unsigned int* pTime )
    {
        if ( pTime != NULL )
        {
            *pTime = radTimeGetMilliseconds( ) - m_TimeOfStateChange;
        }
        return ( ( m_MaxRange - m_MinRange ) * m_Value ) + m_MinRange;
    }

    virtual void SetRange( float min, float max ) { m_MinRange = min; m_MaxRange = max; }
    virtual void GetRange( float* pMin, float* pMax )
    {
        if ( pMin ) *pMin = m_MinRange;
        if ( pMax ) *pMax = m_MaxRange;
    }

private:
    struct CallbackEntry { IRadControllerInputPointCallback* pCallback; unsigned int userData; };

    char  m_Name[ 64 ];
    char  m_Type[ 16 ];
    Kind  m_Kind;
    bool  m_Invert;
    float m_Value;
    float m_Tolerance;
    float m_MinRange;
    float m_MaxRange;
    unsigned int m_TimeOfStateChange;
    std::vector< CallbackEntry > m_Callbacks;
};

//=============================================================================
// Output point: a rumble motor. SetGain only records the value (as DirectInput's did); the controller sends
// it to the device when it polls.
//=============================================================================

class radControllerOutputPointSDL
    :
    public IRadControllerOutputPoint,
    public radRefCount
{
public:
    IMPLEMENT_REFCOUNTED( "radControllerOutputPointSDL" )

    radControllerOutputPointSDL( const char* name )
        :
        radRefCount( 0 ),
        m_Gain( 0.0f )
    {
        strncpy( m_Name, name, sizeof( m_Name ) - 1 );
        m_Name[ sizeof( m_Name ) - 1 ] = '\0';
    }

    virtual const char* GetName( void ) { return m_Name; }
    virtual const char* GetType( void ) { return "Analog"; }
    virtual float GetGain( void ) { return m_Gain; }
    virtual void SetGain( float value )
    {
        m_Gain = value < 0.0f ? 0.0f : ( value > 1.0f ? 1.0f : value );
    }

    // DirectInput effects (steering wheels only)
    virtual long GetOffset( void ) const { return 0; }
    virtual void UpdateEffect( const DIEFFECT* effect ) { }
    virtual void Start( void ) { }
    virtual void Stop( void ) { }
    virtual void ReleaseEffect( void ) { }

private:
    char  m_Name[ 16 ];
    float m_Gain;
};

//=============================================================================
// Controller
//=============================================================================

class radControllerSDL
    :
    public IRadController,
    public radRefCount
{
public:
    IMPLEMENT_REFCOUNTED( "radControllerSDL" )

    enum Classification { Keyboard, Mouse, Joystick };

    radControllerSDL( Classification c, const char* type, const char* location )
        :
        radRefCount( 0 ),
        m_Classification( c ),
        m_Connected( true ),
        m_pGameController( NULL ),
        m_InstanceId( -1 ),
        m_WheelAccum( 0 ),
        m_MotionX( 0 ),
        m_MotionY( 0 ),
        m_SentLow( 0 ),
        m_SentHigh( 0 ),
        m_SentTime( 0 )
    {
        strncpy( m_Type, type, sizeof( m_Type ) - 1 );
        m_Type[ sizeof( m_Type ) - 1 ] = '\0';
        strncpy( m_Location, location, sizeof( m_Location ) - 1 );
        m_Location[ sizeof( m_Location ) - 1 ] = '\0';
    }

    virtual ~radControllerSDL( void )
    {
        for ( unsigned int i = 0; i < m_Points.size( ); i++ )
        {
            m_Points[ i ]->Release( );
        }
        for ( unsigned int i = 0; i < m_Motors.size( ); i++ )
        {
            m_Motors[ i ]->Release( );
        }
        if ( m_pGameController != NULL )
        {
            SDL_GameControllerRumble( m_pGameController, 0, 0, 0 );
            SDL_GameControllerClose( m_pGameController );
        }
    }

    radControllerInputPointSDL* AddPoint( const char* name, const char* type,
                                          radControllerInputPointSDL::Kind kind, bool invert = false )
    {
        radControllerInputPointSDL* p = new( g_ControllerSystemAllocator ) radControllerInputPointSDL( name, type, kind, invert );
        p->AddRef( );
        m_Points.push_back( p );
        return p;
    }

    void AddMotor( const char* name )
    {
        radControllerOutputPointSDL* p = new( g_ControllerSystemAllocator ) radControllerOutputPointSDL( name );
        p->AddRef( );
        m_Motors.push_back( p );
    }

    // ---- IRadController
    virtual bool IsConnected( void ) { return m_Connected; }
    virtual const char* GetType( void ) { return m_Type; }
    virtual const char* GetClassification( void )
    {
        return m_Classification == Keyboard ? "Keyboard" : ( m_Classification == Mouse ? "Mouse" : "Joystick" );
    }

    virtual unsigned int GetNumberOfInputPointsOfType( const char* pType )
    {
        unsigned int n = 0;
        for ( unsigned int i = 0; i < m_Points.size( ); i++ )
            if ( strcmp( m_Points[ i ]->GetType( ), pType ) == 0 ) n++;
        return n;
    }
    virtual unsigned int GetNumberOfOutputPointsOfType( const char* pType )
    {
        return strcmp( pType, "Analog" ) == 0 ? m_Motors.size( ) : 0;
    }

    virtual IRadControllerInputPoint* GetInputPointByTypeAndIndex( const char* pType, unsigned int indx )
    {
        for ( unsigned int i = 0; i < m_Points.size( ); i++ )
        {
            if ( strcmp( m_Points[ i ]->GetType( ), pType ) == 0 )
            {
                if ( indx == 0 ) return m_Points[ i ];
                indx--;
            }
        }
        return NULL;
    }
    virtual IRadControllerOutputPoint* GetOutputPointByTypeAndIndex( const char* pType, unsigned int indx )
    {
        return strcmp( pType, "Analog" ) == 0 ? GetOutputPointByIndex( indx ) : NULL;
    }

    virtual IRadControllerInputPoint* GetInputPointByName( const char* pName )
    {
        for ( unsigned int i = 0; i < m_Points.size( ); i++ )
            if ( strcmp( m_Points[ i ]->GetName( ), pName ) == 0 ) return m_Points[ i ];
        return NULL;
    }
    virtual IRadControllerOutputPoint* GetOutputPointByName( const char* pName )
    {
        for ( unsigned int i = 0; i < m_Motors.size( ); i++ )
            if ( strcmp( m_Motors[ i ]->GetName( ), pName ) == 0 ) return m_Motors[ i ];
        return NULL;
    }

    virtual const char* GetLocation( void ) { return m_Location; }
    virtual unsigned int GetNumberOfInputPoints( void ) { return m_Points.size( ); }
    virtual IRadControllerInputPoint* GetInputPointByIndex( unsigned int indx )
    {
        return indx < m_Points.size( ) ? m_Points[ indx ] : NULL;
    }
    virtual unsigned int GetNumberOfOutputPoints( void ) { return m_Motors.size( ); }
    virtual IRadControllerOutputPoint* GetOutputPointByIndex( unsigned int indx )
    {
        return indx < m_Motors.size( ) ? m_Motors[ indx ] : NULL;
    }

    // ---- polling
    void Poll( void )
    {
        if ( !m_Connected ) return;
        if ( m_Classification == Keyboard )
        {
            static const bool s_noInput = getenv( "SHAR_NO_INPUT" ) != NULL;   // debug: ignore the keyboard
            unsigned int now = SDL_GetTicks( );
            const Uint8* keys = SDL_GetKeyboardState( NULL );
            for ( unsigned int i = 0; i < m_Points.size( ); i++ )
            {
                bool down = ( !s_noInput && !s_InputBlocked && keys[ s_KeyTable[ i ].sc ] ) ||
                            KeyScriptHolds( s_KeyTable[ i ].dik, now );
                m_Points[ i ]->Update( down ? 0x80 : 0 );
            }
        }
        else if ( m_Classification == Mouse )
        {
            // motion comes from SDL_MOUSEMOTION events (HandleEvent), not SDL_GetRelativeMouseState: with
            // pointer-warp emulation of relative mode the polled state drifts by a pixel every frame
            Uint32 buttons = SDL_GetMouseState( NULL, NULL );
            if ( m_MotionX ) m_Points[ 0 ]->Update( m_MotionX );
            if ( m_MotionY ) m_Points[ 1 ]->Update( m_MotionY );
            if ( m_WheelAccum ) m_Points[ 2 ]->Update( m_WheelAccum );
            m_MotionX = m_MotionY = m_WheelAccum = 0;
            static const int sdlButton[ 5 ] = { SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE, SDL_BUTTON_X1, SDL_BUTTON_X2 };
            static const bool s_noInput = getenv( "SHAR_NO_INPUT" ) != NULL;
            unsigned int now = SDL_GetTicks( );
            for ( int b = 0; b < 5; b++ )
            {
                bool down = ( !s_noInput && !s_InputBlocked && ( buttons & SDL_BUTTON( sdlButton[ b ] ) ) ) ||
                            KeyScriptHolds( 1000 + b, now );
                m_Points[ 3 + b ]->Update( down ? 0x80 : 0 );
            }
        }
        else if ( m_pGameController != NULL )
        {
            SDL_GameController* gc = m_pGameController;
            // SDL axes are -32768..32767; DirectInput absolute axes are 0..65535
            m_Points[ 0 ]->Update( AxisToDI( SDL_GameControllerGetAxis( gc, SDL_CONTROLLER_AXIS_LEFTX ) ) );
            m_Points[ 1 ]->Update( AxisToDI( SDL_GameControllerGetAxis( gc, SDL_CONTROLLER_AXIS_LEFTY ) ) );
            // XInput pads under DirectInput: both triggers share Z (LT towards 65535, RT towards 0)
            int lt = SDL_GameControllerGetAxis( gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT );
            int rt = SDL_GameControllerGetAxis( gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT );
            m_Points[ 2 ]->Update( 32767 + ( lt - rt ) );
            m_Points[ 3 ]->Update( AxisToDI( SDL_GameControllerGetAxis( gc, SDL_CONTROLLER_AXIS_RIGHTX ) ) );
            m_Points[ 4 ]->Update( AxisToDI( SDL_GameControllerGetAxis( gc, SDL_CONTROLLER_AXIS_RIGHTY ) ) );

            bool up    = SDL_GameControllerGetButton( gc, SDL_CONTROLLER_BUTTON_DPAD_UP ) != 0;
            bool down  = SDL_GameControllerGetButton( gc, SDL_CONTROLLER_BUTTON_DPAD_DOWN ) != 0;
            bool left  = SDL_GameControllerGetButton( gc, SDL_CONTROLLER_BUTTON_DPAD_LEFT ) != 0;
            bool right = SDL_GameControllerGetButton( gc, SDL_CONTROLLER_BUTTON_DPAD_RIGHT ) != 0;
            int pov = -1;
            if ( up )         pov = right ? 4500 : ( left ? 31500 : 0 );
            else if ( down )  pov = right ? 13500 : ( left ? 22500 : 18000 );
            else if ( right ) pov = 9000;
            else if ( left )  pov = 27000;
            m_Points[ 5 ]->Update( pov );

            static const SDL_GameControllerButton order[ 10 ] =
            {
                SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
                SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
                SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START,
                SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK
            };
            for ( int b = 0; b < 10; b++ )
                m_Points[ 6 + b ]->Update( SDL_GameControllerGetButton( gc, order[ b ] ) ? 0x80 : 0 );

            SendRumble( );
        }
    }

    // Sends the motors' gains when they change. A rumble lasts at most RUMBLE_MS, so it is renewed while it
    // runs and a stalled game (breakpoint, hang) cannot leave the pad shaking.
    void SendRumble( void )
    {
        const unsigned int RUMBLE_MS = 500;
        Uint16 low = (Uint16) ( m_Motors[ 0 ]->GetGain( ) * 65535.0f );
        Uint16 high = (Uint16) ( m_Motors[ 1 ]->GetGain( ) * 65535.0f );
        unsigned int now = SDL_GetTicks( );
        bool renew = ( low != 0 || high != 0 ) && now - m_SentTime >= RUMBLE_MS / 2;
        if ( low == m_SentLow && high == m_SentHigh && !renew )
        {
            return;
        }
        static int s_log = -1;
        if ( s_log < 0 ) { const char* e = getenv( "SHAR_RUMBLE_LOG" ); s_log = ( e && *e != '0' ) ? 1 : 0; }
        if ( s_log && ( low != m_SentLow || high != m_SentHigh ) )
            fprintf( stderr, "[rumble] %s: low %.2f high %.2f\n", m_Location, low / 65535.0f, high / 65535.0f );
        SDL_GameControllerRumble( m_pGameController, low, high, RUMBLE_MS );
        m_SentLow = low;
        m_SentHigh = high;
        m_SentTime = now;
    }

    static int AxisToDI( int sdlValue ) { return sdlValue + 32768; }

    Classification          m_Classification;
    bool                    m_Connected;
    SDL_GameController*     m_pGameController;
    SDL_JoystickID          m_InstanceId;
    int                     m_WheelAccum;
    int                     m_MotionX;
    int                     m_MotionY;
    Uint16                  m_SentLow;
    Uint16                  m_SentHigh;
    unsigned int            m_SentTime;

private:
    char m_Type[ 64 ];
    char m_Location[ 32 ];
    std::vector< radControllerInputPointSDL* > m_Points;
    std::vector< radControllerOutputPointSDL* > m_Motors;
};

//=============================================================================
// Controller system
//=============================================================================

class radControllerSystemSDL;
static radControllerSystemSDL* s_pTheSDLControllerSystem = NULL;

class radControllerSystemSDL
    :
    public IRadControllerSystem,
    public radRefCount
{
public:
    IMPLEMENT_REFCOUNTED( "radControllerSystemSDL" )

    radControllerSystemSDL( IRadControllerConnectionChangeCallback* pCallback, radMemoryAllocator alloc )
        :
        radRefCount( 0 ),
        m_pKeyboard( NULL ),
        m_pMouse( NULL ),
        m_MouseLook( false ),
        m_HaveLastPos( false ),
        m_LastX( 0 ),
        m_LastY( 0 )
    {
        rAssert( s_pTheSDLControllerSystem == NULL );
        s_pTheSDLControllerSystem = this;
        g_ControllerSystemAllocator = alloc;
        AddRef( );

        if ( !SDL_WasInit( SDL_INIT_GAMECONTROLLER ) )
        {
            SDL_InitSubSystem( SDL_INIT_GAMECONTROLLER );
        }

        if ( pCallback != NULL )
        {
            m_Callbacks.push_back( pCallback );
        }

        // Keyboard0
        m_pKeyboard = new( alloc ) radControllerSDL( radControllerSDL::Keyboard, "Keyboard", "Keyboard0" );
        m_pKeyboard->AddRef( );
        for ( unsigned int i = 0; i < sizeof( s_KeyTable ) / sizeof( s_KeyTable[ 0 ] ); i++ )
        {
            s_VirtualKeyToIndex[ s_KeyTable[ i ].dik - 1 ] = (int) i;
            m_pKeyboard->AddPoint( SDL_GetScancodeName( s_KeyTable[ i ].sc ), "Button", radControllerInputPointSDL::Button );
        }
        m_Controllers.push_back( m_pKeyboard );

        // Mouse0
        m_pMouse = new( alloc ) radControllerSDL( radControllerSDL::Mouse, "Mouse", "Mouse0" );
        m_pMouse->AddRef( );
        m_pMouse->AddPoint( "X-axis", "RelXAxis", radControllerInputPointSDL::RelAxis );
        m_pMouse->AddPoint( "Y-axis", "RelYAxis", radControllerInputPointSDL::RelAxis );
        m_pMouse->AddPoint( "Wheel", "RelZAxis", radControllerInputPointSDL::RelAxis );
        for ( int b = 0; b < 5; b++ )
        {
            char name[ 16 ];
            sprintf( name, "Button %d", b );
            m_pMouse->AddPoint( name, "Button", radControllerInputPointSDL::Button );
        }
        m_Controllers.push_back( m_pMouse );

        // Joystick<n>: game controllers present at startup (hot-plug through radControllerSDLHandleEvent)
        for ( int i = 0; i < SDL_NumJoysticks( ); i++ )
        {
            OpenGameController( i, false );
        }
    }

    virtual ~radControllerSystemSDL( void )
    {
        for ( unsigned int i = 0; i < m_Controllers.size( ); i++ )
        {
            m_Controllers[ i ]->Release( );
        }
        s_pTheSDLControllerSystem = NULL;
    }

    void OpenGameController( int deviceIndex, bool notify )
    {
        if ( !SDL_IsGameController( deviceIndex ) )
        {
            return;
        }
        // SDL also sends SDL_CONTROLLERDEVICEADDED for the pads already present at startup (opened above):
        // opening those again made a second Joystick<n> for the same pad, and the front end, which listens to
        // every controller, moved two menu items per press
        SDL_JoystickID deviceId = SDL_JoystickGetDeviceInstanceID( deviceIndex );
        for ( unsigned int i = 0; i < m_Controllers.size( ); i++ )
        {
            if ( m_Controllers[ i ]->m_Classification == radControllerSDL::Joystick && m_Controllers[ i ]->m_Connected &&
                 m_Controllers[ i ]->m_InstanceId == deviceId )
            {
                return;
            }
        }
        SDL_GameController* gc = SDL_GameControllerOpen( deviceIndex );
        if ( gc == NULL )
        {
            return;
        }
        SDL_JoystickID id = SDL_JoystickInstanceID( SDL_GameControllerGetJoystick( gc ) );

        // reuse a disconnected slot first, so "Joystick0" stays Joystick0 across a replug
        radControllerSDL* c = NULL;
        for ( unsigned int i = 0; i < m_Controllers.size( ); i++ )
        {
            if ( m_Controllers[ i ]->m_Classification == radControllerSDL::Joystick && !m_Controllers[ i ]->m_Connected )
            {
                c = m_Controllers[ i ];
                break;
            }
        }
        if ( c == NULL )
        {
            char location[ 32 ];
            sprintf( location, "Joystick%d", CountJoysticks( ) );
            const char* name = SDL_GameControllerName( gc );
            c = new( g_ControllerSystemAllocator ) radControllerSDL( radControllerSDL::Joystick, name ? name : "Gamepad", location );
            c->AddRef( );
            c->AddPoint( "X Axis", "XAxis", radControllerInputPointSDL::AbsAxis );
            c->AddPoint( "Y Axis", "YAxis", radControllerInputPointSDL::AbsAxis, true );   // DI inverts Y
            c->AddPoint( "Z Axis", "ZAxis", radControllerInputPointSDL::AbsAxis );
            c->AddPoint( "X Rotation", "RxAxis", radControllerInputPointSDL::AbsAxis );
            c->AddPoint( "Y Rotation", "RyAxis", radControllerInputPointSDL::AbsAxis );
            c->AddPoint( "Hat Switch", "POV", radControllerInputPointSDL::POV );
            for ( int b = 0; b < 10; b++ )
            {
                char name2[ 16 ];
                sprintf( name2, "Button %d", b );
                c->AddPoint( name2, "Button", radControllerInputPointSDL::Button );
            }
            c->AddMotor( "LeftMotor" );
            c->AddMotor( "RightMotor" );
            m_Controllers.push_back( c );
        }
        c->m_pGameController = gc;
        c->m_InstanceId = id;
        c->m_SentLow = c->m_SentHigh = 0;
        c->m_Connected = true;
        if ( notify )
        {
            NotifyConnectionChange( c );
        }
    }

    int CountJoysticks( void )
    {
        int n = 0;
        for ( unsigned int i = 0; i < m_Controllers.size( ); i++ )
            if ( m_Controllers[ i ]->m_Classification == radControllerSDL::Joystick ) n++;
        return n;
    }

    void NotifyConnectionChange( radControllerSDL* c )
    {
        for ( unsigned int i = 0; i < m_Callbacks.size( ); i++ )
        {
            m_Callbacks[ i ]->OnControllerConnectionStatusChange( c );
        }
    }

    void HandleEvent( const SDL_Event* e )
    {
        switch ( e->type )
        {
        case SDL_MOUSEMOTION:
            if ( getenv( "SHAR_NO_MOUSE" ) || getenv( "SHAR_NO_INPUT" ) ) break;   // debug: ignore the mouse
            if ( m_MouseLook && NativeWayland( ) )
            {
                m_pMouse->m_MotionX += e->motion.xrel;      // relative mode (see SetMouseLook)
                m_pMouse->m_MotionY += e->motion.yrel;
                break;
            }
            HandleMouseMotion( e->motion.windowID, e->motion.x, e->motion.y );
            break;
        case SDL_MOUSEWHEEL:
            m_pMouse->m_WheelAccum += e->wheel.y * 120;     // DirectInput reports 120 per wheel notch
            break;
        case SDL_CONTROLLERDEVICEADDED:
            OpenGameController( e->cdevice.which, true );
            break;
        case SDL_CONTROLLERDEVICEREMOVED:
            for ( unsigned int i = 0; i < m_Controllers.size( ); i++ )
            {
                radControllerSDL* c = m_Controllers[ i ];
                if ( c->m_Classification == radControllerSDL::Joystick && c->m_Connected && c->m_InstanceId == e->cdevice.which )
                {
                    SDL_GameControllerClose( c->m_pGameController );
                    c->m_pGameController = NULL;
                    c->m_SentLow = c->m_SentHigh = 0;
                    c->m_Connected = false;
                    NotifyConnectionChange( c );
                }
            }
            break;
        }
    }

    // Mouse deltas come from absolute positions. SDL relative mode is NOT used: under XWayland (with display
    // scaling) it re-centres the pointer every frame and each re-centre lands a pixel or two off, which the
    // game reads as constant mouse motion (mouse-look then turns the character on its own). In mouse-look
    // the pointer is hidden and confined, and only re-centred when it nears the window edge.
    void SetMouseLook( bool on )
    {
        m_MouseLook = on;
        if ( NativeWayland( ) )
        {
            // Wayland clients cannot warp the pointer, but there SDL's relative mode is the compositor's pointer
            // lock and relative-motion events, without XWayland's re-centring error
            SDL_SetRelativeMouseMode( on ? SDL_TRUE : SDL_FALSE );
            m_HaveLastPos = false;
            return;
        }
        SDL_Window* w = SDL_GetMouseFocus( ) ? SDL_GetMouseFocus( ) : SDL_GetKeyboardFocus( );
        if ( w != NULL )
        {
            SDL_SetWindowGrab( w, on ? SDL_TRUE : SDL_FALSE );
            if ( on ) Recentre( w );
        }
        // Only mouse-look truly hides the system cursor: XWayland then locks the pointer (its pointer-warp
        // emulation, which the re-centring needs) and keeps it locked for as long as the cursor stays hidden,
        // grab or not; it lets go only when the pointer leaves the window, and then puts it back where the last
        // re-centre left it. Elsewhere the cursor stays "shown" as the near-invisible cursor
        // Win32Platform::ShowTheCursor sets (the front end draws its own), which ends the lock straight away.
        SDL_ShowCursor( on ? SDL_DISABLE : SDL_ENABLE );
    }

    static bool NativeWayland( void )
    {
        static const bool s_wayland = SDL_GetCurrentVideoDriver( ) != NULL && strcmp( SDL_GetCurrentVideoDriver( ), "wayland" ) == 0;
        return s_wayland;
    }

    void Recentre( SDL_Window* w )
    {
        int ww, wh;
        SDL_GetWindowSize( w, &ww, &wh );
        SDL_WarpMouseInWindow( w, ww / 2, wh / 2 );
        m_HaveLastPos = false;                       // the warp's own motion event only re-bases the position
    }

    void HandleMouseMotion( Uint32 windowID, int x, int y )
    {
        if ( m_HaveLastPos )
        {
            m_pMouse->m_MotionX += x - m_LastX;
            m_pMouse->m_MotionY += y - m_LastY;
        }
        m_LastX = x;
        m_LastY = y;
        m_HaveLastPos = true;

        if ( m_MouseLook )
        {
            SDL_Window* w = SDL_GetWindowFromID( windowID );
            int ww, wh;
            if ( w != NULL )
            {
                SDL_GetWindowSize( w, &ww, &wh );
                int mx = ww / 4, my = wh / 4;
                if ( x < mx || x > ww - mx || y < my || y > wh - my ) Recentre( w );
            }
        }
    }

    void Service( void )
    {
        for ( unsigned int i = 0; i < m_Controllers.size( ); i++ )
        {
            m_Controllers[ i ]->Poll( );
        }
    }

    // ---- IRadControllerSystem
    virtual unsigned int GetNumberOfControllers( void ) { return m_Controllers.size( ); }
    virtual IRadController* GetControllerByIndex( unsigned int indx )
    {
        return indx < m_Controllers.size( ) ? m_Controllers[ indx ] : NULL;
    }
    virtual IRadController* GetControllerAtLocation( const char* pLocation )
    {
        for ( unsigned int i = 0; i < m_Controllers.size( ); i++ )
            if ( strcmp( m_Controllers[ i ]->GetLocation( ), pLocation ) == 0 ) return m_Controllers[ i ];
        return NULL;
    }
    // Event buffering and virtual time were DirectInput buffered-data features; state is polled here.
    virtual void SetBufferTime( unsigned int milliseconds ) { }
    virtual void MapVirtualTime( unsigned int timerManagerMs, unsigned int virtualTicks ) { }
    virtual void SetVirtualTime( unsigned int virtualTicks ) { }
    virtual void SetCaptureRate( unsigned int ms ) { }
    virtual void RegisterConnectionChangeCallback( IRadControllerConnectionChangeCallback* pCallback )
    {
        m_Callbacks.push_back( pCallback );
    }
    virtual void UnRegisterConnectionChangeCallback( IRadControllerConnectionChangeCallback* pCallback )
    {
        for ( unsigned int i = 0; i < m_Callbacks.size( ); )
        {
            if ( m_Callbacks[ i ] == pCallback ) m_Callbacks.erase( m_Callbacks.begin( ) + i );
            else i++;
        }
    }

private:
    radControllerSDL* m_pKeyboard;
    radControllerSDL* m_pMouse;
    bool m_MouseLook;
    bool m_HaveLastPos;
    int  m_LastX, m_LastY;
    std::vector< radControllerSDL* > m_Controllers;
    std::vector< IRadControllerConnectionChangeCallback* > m_Callbacks;
};

//=============================================================================
// Public functions
//=============================================================================

void radControllerInitialize( IRadControllerConnectionChangeCallback* pConnectionChangeCallback, radMemoryAllocator alloc )
{
    rAssert( s_pTheSDLControllerSystem == NULL );
    for ( unsigned int i = 0; i < 256; i++ )
    {
        s_VirtualKeyToIndex[ i ] = -1;
    }
    new ( alloc ) radControllerSystemSDL( pConnectionChangeCallback, alloc );
}

void radControllerTerminate( void )
{
    radRelease( s_pTheSDLControllerSystem, NULL );
}

IRadControllerSystem* radControllerSystemGet( void )
{
    rAssert( s_pTheSDLControllerSystem != NULL );
    return s_pTheSDLControllerSystem;
}

void radControllerSystemService( void )
{
    if ( s_pTheSDLControllerSystem != NULL )
    {
        s_pTheSDLControllerSystem->Service( );
    }
}

void radControllerSDLHandleEvent( const void* pSDLEvent )
{
    if ( s_pTheSDLControllerSystem != NULL && pSDLEvent != NULL )
    {
        s_pTheSDLControllerSystem->HandleEvent( (const SDL_Event*) pSDLEvent );
    }
}

void radControllerSDLSetMouseLook( bool on )
{
    if ( s_pTheSDLControllerSystem != NULL )
    {
        s_pTheSDLControllerSystem->SetMouseLook( on );
    }
}

void radControllerSDLSetInputBlocked( bool on )
{
    s_InputBlocked = on;
}
