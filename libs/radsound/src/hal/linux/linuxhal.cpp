//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// Linux port: radsound hardware abstraction layer with a software mixer on SDL2 audio.
//
// Replaces the DirectSound HAL in hal/win32:
//   - buffers keep their PCM in the radsound memory object (loaded through the shared buffer loader)
//   - an SDL audio callback mixes every playing voice (linear resampling for sample rate x pitch,
//     volume/trim/mute as amplitude = analog^2 like DirectSound, DirectSound pan law) and owns the play
//     position, so streams that refill a looping buffer by watching the position stay in step with what
//     is heard; looping buffers wrap, one-shots stop and rewind at the end
//   - 3D positioning uses radsound's own software listener / positional group (hal/common): its volume,
//     pan and pitch (doppler) adjustments are applied by the mixer
//   - if no audio device can be opened (or SHAR_NO_AUDIO is set) voices advance in real time without
//     output, so everything that waits on sounds still behaves
//   - EAX2 environmental reverb in software (radSoundLinuxReverb below): as with EAX under DirectSound, only
//     positional voices feed it; the game fades it with the system aux gain of the environment send
// Voice state shared with the callback (play list, buffer, position) changes under the audio device lock.
//=============================================================================

#include "pch.hpp"
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include <radlinkedclass.hpp>
#include <radsound_hal.hpp>
#include <radsound_win32.hpp>
#include <radsoundobject.hpp>
#include <radsoundupdatableobject.hpp>
#include <radtime.hpp>
#include "../common/banner.hpp"
#include "../common/memoryregion.hpp"
#include "../common/softwarelistener.hpp"
#include "../common/softwarepositionalgroup.hpp"
#include "../win32/bufferloader.hpp"

#define RSD_LINUX_MAX_AUX_SENDS 4

//=============================================================================
// Buffer
//=============================================================================

class radSoundHalBufferLinux
    :
    public IRadSoundHalBuffer,
    public IRadSoundHalBufferLoadCallback,
    public radLinkedClass< radSoundHalBufferLinux >,
    public radSoundObject
{
public:
    IMPLEMENT_REFCOUNTED( "radSoundHalBufferLinux" )

    radSoundHalBufferLinux( void ) : m_SizeInFrames( 0 ), m_Looping( false ), m_Streaming( false ) { }

    virtual void Initialize( IRadSoundHalAudioFormat* pFormat, IRadMemoryObject* pMemory, unsigned int sizeInFrames,
                             bool looping, bool streaming )
    {
        rAssert( pFormat != NULL );
        rAssert( pFormat->GetEncoding( ) == IRadSoundHalAudioFormat::PCM );
        m_refFormat = pFormat;
        m_refMemory = pMemory;
        m_SizeInFrames = sizeInFrames;
        m_Looping = looping;
        m_Streaming = streaming;
    }

    virtual IRadSoundHalAudioFormat* GetFormat( void ) { return m_refFormat; }
    virtual IRadMemoryObject* GetMemoryObject( void ) { return m_refMemory; }
    virtual bool IsLooping( void ) { return m_Looping; }
    virtual unsigned int GetSizeInFrames( void ) { return m_SizeInFrames; }

    virtual void LoadAsync( IRadSoundHalDataSource* pDataSource, unsigned int bufferStartInFrames,
                            unsigned int numberOfFrames, IRadSoundHalBufferLoadCallback* pCallback )
    {
        rAssert( m_refLoadCallback == NULL );
        m_refLoadCallback = pCallback;
        char* dest = (char*) m_refMemory->GetMemoryAddress( ) + m_refFormat->FramesToBytes( bufferStartInFrames );
        new( "radSoundBufferLoaderLinux", RADMEMORY_ALLOC_TEMP ) radSoundBufferLoaderWin(
            static_cast< IRadSoundHalBuffer* >( this ), dest, pDataSource, m_refFormat, numberOfFrames, this );
    }

    virtual void OnBufferLoadComplete( unsigned int dataSourceFrames )
    {
        if ( m_refLoadCallback != NULL )
        {
            ref< IRadSoundHalBufferLoadCallback > cb = m_refLoadCallback;
            m_refLoadCallback = NULL;
            cb->OnBufferLoadComplete( dataSourceFrames );
        }
    }

    virtual void ClearAsync( unsigned int startPositionInFrames, unsigned int numberOfFrames,
                             IRadSoundHalBufferClearCallback* pCallback )
    {
        unsigned char fill = ( m_refFormat->GetBitResolution( ) == 8 ) ? PCM_SILENCE_8_BIT : 0;
        memset( (char*) m_refMemory->GetMemoryAddress( ) + m_refFormat->FramesToBytes( startPositionInFrames ),
                fill, m_refFormat->FramesToBytes( numberOfFrames ) );
        if ( pCallback != NULL )
        {
            pCallback->OnBufferClearComplete( );
        }
    }

    virtual unsigned int GetMinTransferSize( IRadSoundHalAudioFormat::SizeType sizeType )
    {
        return m_refFormat->ConvertSizeType( sizeType,
            radMemorySpace_OptimalMultiple * m_refFormat->GetNumberOfChannels( ), IRadSoundHalAudioFormat::Bytes );
    }

    virtual void CancelAsyncOperations( void )
    {
        radSoundBufferLoaderWin::CancelOperations( static_cast< IRadSoundHalBuffer* >( this ) );
        m_refLoadCallback = NULL;
    }

    virtual void ReSetAudioFormat( IRadSoundHalAudioFormat* pFormat ) { }

    unsigned int GetSizeInBytes( void ) { return m_refFormat != NULL ? m_refFormat->FramesToBytes( m_SizeInFrames ) : 0; }

private:
    virtual ~radSoundHalBufferLinux( void ) { CancelAsyncOperations( ); }

    unsigned int m_SizeInFrames;
    bool m_Looping;
    bool m_Streaming;
    ref< IRadSoundHalAudioFormat >          m_refFormat;
    ref< IRadMemoryObject >                 m_refMemory;
    ref< IRadSoundHalBufferLoadCallback >   m_refLoadCallback;
};

template<> radSoundHalBufferLinux* radLinkedClass< radSoundHalBufferLinux >::s_pLinkedClassHead = NULL;
template<> radSoundHalBufferLinux* radLinkedClass< radSoundHalBufferLinux >::s_pLinkedClassTail = NULL;

IRadSoundHalBuffer* radSoundHalBufferCreate( radMemoryAllocator allocator )
{
    return new( "radSoundHalBufferLinux", allocator ) radSoundHalBufferLinux( );
}

//=============================================================================
// SHAR_SOUND_LOG=1: voice/buffer counts every 5 s and voices the mixer refuses (diagnostics)
static bool SoundLogOn( void )
{
    static int s_on = -1;
    if ( s_on < 0 ) { const char* e = getenv( "SHAR_SOUND_LOG" ); s_on = ( e && *e && *e != '0' ) ? 1 : 0; }
    return s_on == 1;
}

// Mixer state (audio device + list of playing voices, both guarded by the device lock)
//=============================================================================

class radSoundHalVoiceLinux;

static SDL_AudioDeviceID s_AudioDevice = 0;
static int s_OutputRate = 48000;
static const unsigned int MAX_ACTIVE_VOICES = 256;
static radSoundHalVoiceLinux* s_ActiveVoices[ MAX_ACTIVE_VOICES ];
static unsigned int s_NumActiveVoices = 0;

// A value the game thread sets and the mixer reads without the device lock (voice volume, pan, pitch...): relaxed
// atomic loads and stores, the same instructions as plain ones, but defined behaviour and visible to TSan. A reader
// may see the previous value for one block; the mixer ramps gains, so that's inaudible.
template< typename T > class radSoundShared
{
public:
    radSoundShared( void ) { }
    radSoundShared( T x ) : m_Value( x ) { }
    operator T( void ) const { T x; __atomic_load( &m_Value, &x, __ATOMIC_RELAXED ); return x; }
    radSoundShared& operator=( T x ) { __atomic_store( &m_Value, &x, __ATOMIC_RELAXED ); return *this; }
    radSoundShared& operator+=( T x ) { return *this = T( *this ) + x; }   // one writer at a time
    radSoundShared& operator-=( T x ) { return *this = T( *this ) - x; }
    radSoundShared( const radSoundShared& o ) : m_Value( T( o ) ) { }
private:
    T m_Value;
};

struct radSoundMixLock
{
    radSoundMixLock( void ) { if ( s_AudioDevice != 0 ) SDL_LockAudioDevice( s_AudioDevice ); }
    ~radSoundMixLock( void ) { if ( s_AudioDevice != 0 ) SDL_UnlockAudioDevice( s_AudioDevice ); }
};

//=============================================================================
// Movie stream: radmovie's FFmpeg Bink player pushes decoded stereo float audio (at the output rate) into
// this ring; the mixer plays it while the stream is running. Stands in for Bink's own DirectSound output.
//=============================================================================

static float* s_MovieRing = NULL;               // interleaved stereo
static unsigned int s_MovieRingFrames = 0;
static unsigned int s_MovieRead = 0;            // frame indices, wrap at s_MovieRingFrames
static unsigned int s_MovieFill = 0;            // frames buffered
static bool s_MovieRunning = false;
static float s_MovieGain[ 2 ] = { 1.0f, 1.0f };

void radSoundLinuxMovieStreamOpen( unsigned int seconds )
{
    radSoundMixLock lock;
    free( s_MovieRing );
    s_MovieRingFrames = seconds * s_OutputRate;
    s_MovieRing = (float*) malloc( s_MovieRingFrames * 2 * sizeof( float ) );
    s_MovieRead = s_MovieFill = 0;
    s_MovieRunning = false;
}

void radSoundLinuxMovieStreamClose( void )
{
    radSoundMixLock lock;
    free( s_MovieRing );
    s_MovieRing = NULL;
    s_MovieRingFrames = s_MovieRead = s_MovieFill = 0;
    s_MovieRunning = false;
}

unsigned int radSoundLinuxMovieStreamOutputRate( void ) { return s_OutputRate; }

// Returns the frames accepted (the rest is dropped when the ring is full).
unsigned int radSoundLinuxMovieStreamWrite( const float* stereo, unsigned int frames )
{
    radSoundMixLock lock;
    if ( s_MovieRing == NULL || s_AudioDevice == 0 )
    {
        return frames;
    }
    unsigned int space = s_MovieRingFrames - s_MovieFill;
    if ( frames > space ) frames = space;
    unsigned int w = ( s_MovieRead + s_MovieFill ) % s_MovieRingFrames;
    for ( unsigned int i = 0; i < frames; i++ )
    {
        s_MovieRing[ w * 2 ] = stereo[ i * 2 ];
        s_MovieRing[ w * 2 + 1 ] = stereo[ i * 2 + 1 ];
        if ( ++w == s_MovieRingFrames ) w = 0;
    }
    s_MovieFill += frames;
    return frames;
}

void radSoundLinuxMovieStreamSetRunning( bool running )
{
    radSoundMixLock lock;
    s_MovieRunning = running;
}

void radSoundLinuxMovieStreamSetGains( float left, float right )
{
    radSoundMixLock lock;
    s_MovieGain[ 0 ] = left;
    s_MovieGain[ 1 ] = right;
}

static void radSoundMixMovie( float* out, int frames )
{
    if ( !s_MovieRunning || s_MovieRing == NULL )
    {
        return;
    }
    for ( int i = 0; i < frames && s_MovieFill > 0; i++ )
    {
        out[ i * 2 ] += s_MovieRing[ s_MovieRead * 2 ] * s_MovieGain[ 0 ];
        out[ i * 2 + 1 ] += s_MovieRing[ s_MovieRead * 2 + 1 ] * s_MovieGain[ 1 ];
        if ( ++s_MovieRead == s_MovieRingFrames ) s_MovieRead = 0;
        s_MovieFill--;
    }
}

//=============================================================================
// Environmental reverb: stands in for the sound card's EAX 2 listener environment. The parameters are
// EAX2 listener properties (levels in mB); the structure is a usual software one: room HF filter, a
// predelay line with early-reflection taps (at ReflectionsDelay) and a late tail (ReflectionsDelay +
// ReverbDelay later) of 8 damped comb filters per channel (decay from DecayTime, HF decay from
// DecayHFRatio, HF reference 5 kHz as EAX2) into 4 allpasses (EnvironmentDiffusion). Mono in, stereo out.
// Audio thread except Set*, which take the device lock. SHAR_REVERB=0 turns it off, SHAR_REVERB_LOG=1
// prints each preset and fade.
//=============================================================================

struct radSoundLinuxReverbParams
{
    int   room, roomHF;
    float roomRolloff, decayTime, decayHFRatio;
    int   reflections;
    float reflectionsDelay;
    int   reverb;
    float reverbDelay, diffusion;
};

static radSoundShared< float > s_AuxGain[ RSD_LINUX_MAX_AUX_SENDS ] = { 1.0f, 1.0f, 1.0f, 1.0f };    // system aux send gains
static unsigned int s_ReverbAux = 0xFFFFFFFF;  // aux send the reverb effect is attached to
static float* s_WetIn = NULL;                  // per-callback mono reverb send, s_WetCapacity frames
static int s_WetCapacity = 0;

static bool radSoundReverbLog( void )
{
    static int s_log = -1;
    if ( s_log < 0 ) { const char* e = getenv( "SHAR_REVERB_LOG" ); s_log = ( e && *e != '0' ) ? 1 : 0; }
    return s_log != 0;
}

static float MilliBelToGain( float mB ) { return powf( 10.0f, mB / 2000.0f ); }

// One-pole lowpass y += (1 - d)(x - y) whose gain at hz is r (0 < r <= 1): the d in [0, 1) solving
// r^2 (1 - 2 d cos w + d^2) = (1 - d)^2.
static float LowpassCoefForGain( float r, float hz, float rate )
{
    if ( r >= 0.9999f ) return 0.0f;
    if ( r < 0.0001f ) r = 0.0001f;
    float c = cosf( 2.0f * 3.14159265f * hz / rate );
    float a = r * r - 1.0f, b = 2.0f - 2.0f * r * r * c;
    float disc = b * b - 4.0f * a * a;
    if ( disc < 0.0f ) disc = 0.0f;
    float d = ( -b + sqrtf( disc ) ) / ( 2.0f * a );
    return d < 0.0f ? 0.0f : ( d > 0.99f ? 0.99f : d );
}

class radSoundLinuxReverb
{
public:
    enum { NUM_COMBS = 8, NUM_ALLPASSES = 4, NUM_TAPS = 6 };

    radSoundLinuxReverb( void ) : m_Rate( 0 ), m_Mem( NULL ), m_Enabled( false ), m_Gain( 0.0f ), m_InLP( 0.0f ),
                                  m_Pre( NULL ), m_PreSize( 0 ), m_PreWrite( 0 ), m_Active( false ), m_Silent( 0 )
    {
        radSoundLinuxReverbParams defaults = { -1000, -500, 0.0f, 3.92f, 0.70f, -1230, 0.020f, -2, 0.029f, 1.0f };
        m_Params = defaults;
    }

    void Open( int rate )
    {
        static const int combTune[ NUM_COMBS ] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };   // at 44.1 kHz
        static const int allpassTune[ NUM_ALLPASSES ] = { 556, 441, 341, 225 };
        const int SPREAD = 23;
        m_Rate = rate;
        float scale = rate / 44100.0f;
        int total = 0;
        for ( int ch = 0; ch < 2; ch++ )
        {
            for ( int i = 0; i < NUM_COMBS; i++ ) total += m_Comb[ ch ][ i ].size = (int) ( ( combTune[ i ] + ch * SPREAD ) * scale );
            for ( int i = 0; i < NUM_ALLPASSES; i++ ) total += m_Allpass[ ch ][ i ].size = (int) ( ( allpassTune[ i ] + ch * SPREAD ) * scale );
        }
        m_PreSize = (int) ( 0.45f * rate );         // ReflectionsDelay (<= 0.3 s) + ReverbDelay (<= 0.1 s) + taps
        total += m_PreSize;
        m_Mem = (float*) calloc( total, sizeof( float ) );
        if ( m_Mem == NULL ) return;
        float* p = m_Mem;
        m_Pre = p; p += m_PreSize;
        for ( int ch = 0; ch < 2; ch++ )
        {
            for ( int i = 0; i < NUM_COMBS; i++ ) { Comb& c = m_Comb[ ch ][ i ]; c.buf = p; p += c.size; c.pos = 0; c.store = 0.0f; }
            for ( int i = 0; i < NUM_ALLPASSES; i++ ) { Allpass& a = m_Allpass[ ch ][ i ]; a.buf = p; p += a.size; a.pos = 0; }
        }
        SetParams( m_Params );
    }

    void SetEnabled( bool enabled ) { m_Enabled = enabled; }
    bool IsEnabled( void ) const { return m_Enabled && m_Mem != NULL; }
    float RoomRolloff( void ) const { return m_Params.roomRolloff; }

    void SetParams( const radSoundLinuxReverbParams& prm )
    {
        if ( radSoundReverbLog( ) && m_Rate != 0 && memcmp( &prm, &m_Params, sizeof( prm ) ) != 0 )
            fprintf( stderr, "[reverb] room %d roomHF %d rolloff %.2f decay %.2f s HF ratio %.2f reflections %d @ %.3f s"
                             " reverb %d @ +%.3f s diffusion %.2f\n", prm.room, prm.roomHF, prm.roomRolloff, prm.decayTime,
                     prm.decayHFRatio, prm.reflections, prm.reflectionsDelay, prm.reverb, prm.reverbDelay, prm.diffusion );
        m_Params = prm;
        if ( m_Mem == NULL ) return;
        float rate = (float) m_Rate;
        float room = MilliBelToGain( (float) prm.room );
        m_ReflGain = room * MilliBelToGain( (float) prm.reflections ) / sqrtf( (float) NUM_TAPS );
        m_LateGain = room * MilliBelToGain( (float) prm.reverb );
        m_InCoef = LowpassCoefForGain( MilliBelToGain( (float) prm.roomHF ), 5000.0f, rate );

        // early reflections spread over ~25 ms after ReflectionsDelay, alternating sides
        static const float tapMs[ NUM_TAPS ] = { 0.0f, 3.7f, 7.9f, 12.1f, 17.3f, 23.9f };
        static const float tapGain[ NUM_TAPS ] = { 1.0f, 0.9f, 0.8f, 0.7f, 0.6f, 0.5f };
        float reflDelay = prm.reflectionsDelay < 0.0f ? 0.0f : ( prm.reflectionsDelay > 0.3f ? 0.3f : prm.reflectionsDelay );
        float revDelay = prm.reverbDelay < 0.0f ? 0.0f : ( prm.reverbDelay > 0.1f ? 0.1f : prm.reverbDelay );
        for ( int t = 0; t < NUM_TAPS; t++ )
        {
            m_TapDelay[ t ] = (int) ( ( reflDelay + tapMs[ t ] * 0.001f ) * rate );
            m_TapGain[ t ] = tapGain[ t ];
        }
        m_LateDelay = (int) ( ( reflDelay + revDelay ) * rate );

        // comb feedback for a 60 dB decay in DecayTime; the loop lowpass makes 5 kHz decay in DecayTime * HFRatio
        float t60 = prm.decayTime < 0.1f ? 0.1f : prm.decayTime;
        float hfRatio = prm.decayHFRatio < 0.1f ? 0.1f : prm.decayHFRatio;
        float diffusion = prm.diffusion < 0.0f ? 0.0f : ( prm.diffusion > 1.0f ? 1.0f : prm.diffusion );
        for ( int ch = 0; ch < 2; ch++ )
        {
            for ( int i = 0; i < NUM_COMBS; i++ )
            {
                Comb& c = m_Comb[ ch ][ i ];
                float seconds = c.size / rate;
                c.feedback = powf( 10.0f, -3.0f * seconds / t60 );
                float hfFeedback = powf( 10.0f, -3.0f * seconds / ( t60 * hfRatio ) );
                c.damp = LowpassCoefForGain( hfFeedback / c.feedback, 5000.0f, rate );
                c.inGain = sqrtf( 1.0f - c.feedback * c.feedback ) / sqrtf( (float) NUM_COMBS );
            }
            for ( int i = 0; i < NUM_ALLPASSES; i++ )
                m_Allpass[ ch ][ i ].feedback = 0.3f + 0.35f * diffusion;
        }
    }

    // Adds the reverb of wet (mono, frames) into out (stereo). gain is the send's aux gain, ramped from the last.
    void Process( const float* wet, float* out, int frames, float gain )
    {
        bool feeding = gain > 0.0f || m_Gain > 0.0f;
        if ( !feeding && !m_Active ) { m_Gain = gain; return; }
        float g = m_Gain, dg = ( gain - m_Gain ) / frames;
        m_Gain = gain;
        float peak = 0.0f;
        for ( int i = 0; i < frames; i++ )
        {
            float x = wet[ i ] * g;
            g += dg;
            m_InLP += ( 1.0f - m_InCoef ) * ( x - m_InLP );
            m_Pre[ m_PreWrite ] = m_InLP;

            float early[ 2 ] = { 0.0f, 0.0f };
            for ( int t = 0; t < NUM_TAPS; t++ )
                early[ t & 1 ] += Delayed( m_TapDelay[ t ] ) * m_TapGain[ t ];
            float late = Delayed( m_LateDelay ) + 1e-18f;          // keeps the loops out of denormals
            if ( ++m_PreWrite == m_PreSize ) m_PreWrite = 0;

            for ( int ch = 0; ch < 2; ch++ )
            {
                float sum = 0.0f;
                for ( int c = 0; c < NUM_COMBS; c++ ) sum += m_Comb[ ch ][ c ].Run( late );
                for ( int a = 0; a < NUM_ALLPASSES; a++ ) sum = m_Allpass[ ch ][ a ].Run( sum );
                float y = early[ ch ] * m_ReflGain + sum * m_LateGain;
                out[ i * 2 + ch ] += y;
                float ay = y < 0.0f ? -y : y;
                if ( ay > peak ) peak = ay;
            }
        }
        // after the input stops, run until the tail has died away (then skip the work)
        if ( feeding || peak > 1e-5f ) { m_Active = true; m_Silent = 0; }
        else if ( ++m_Silent > 8 ) m_Active = false;
    }

private:
    struct Comb
    {
        float* buf; int size, pos; float feedback, damp, inGain, store;
        float Run( float x )
        {
            float y = buf[ pos ];
            store = y + damp * ( store - y );
            buf[ pos ] = x * inGain + store * feedback;
            if ( ++pos == size ) pos = 0;
            return y;
        }
    };
    struct Allpass
    {
        float* buf; int size, pos; float feedback;
        float Run( float x )
        {
            float b = buf[ pos ];                   // v[n - size]
            float v = x + b * feedback;             // H = ( z^-size - g ) / ( 1 - g z^-size ): flat
            buf[ pos ] = v;
            if ( ++pos == size ) pos = 0;
            return b - v * feedback;
        }
    };

    float Delayed( int samples ) const
    {
        int r = m_PreWrite - samples;
        if ( r < 0 ) r += m_PreSize;
        return m_Pre[ r ];
    }

    int m_Rate;
    float* m_Mem;
    bool m_Enabled;
    float m_Gain;
    radSoundLinuxReverbParams m_Params;
    float m_ReflGain, m_LateGain, m_InCoef, m_InLP;
    float* m_Pre;
    int m_PreSize, m_PreWrite;
    int m_TapDelay[ NUM_TAPS ];
    float m_TapGain[ NUM_TAPS ];
    int m_LateDelay;
    Comb m_Comb[ 2 ][ NUM_COMBS ];
    Allpass m_Allpass[ 2 ][ NUM_ALLPASSES ];
    bool m_Active;
    int m_Silent;
};

static radSoundLinuxReverb s_Reverb;

//=============================================================================
// Voice
//=============================================================================

class radSoundHalVoiceLinux
    :
    public IRadSoundHalVoice,
    public radSoundHalPositionalEntity,
    public radSoundUpdatableObject,
    public radLinkedClass< radSoundHalVoiceLinux >
{
public:
    IMPLEMENT_REFCOUNTED( "radSoundHalVoiceLinux" )

    radSoundHalVoiceLinux( void )
        :
        m_Priority( 0 ), m_Playing( false ), m_InUpdateList( false ), m_ActiveIndex( -1 ), m_PositionFrames( 0.0 ),
        m_Volume( 1.0f ), m_Trim( 1.0f ), m_Muted( false ), m_Pan( 0.0f ), m_Pitch( 1.0f ),
        m_LastGainL( 0.0f ), m_LastGainR( 0.0f ), m_LastWet( 0.0f ), m_WetDistanceFactor( 1.0f )
    {
        for ( unsigned int i = 0; i < RSD_LINUX_MAX_AUX_SENDS; i++ )
        {
            m_AuxMode[ i ] = radSoundAuxMode_PostFader;
            m_AuxGain[ i ] = 1.0f;
        }
    }

    virtual void SetPriority( unsigned int priority ) { m_Priority = priority; }
    virtual unsigned int GetPriority( void ) { return m_Priority; }

    virtual void SetBuffer( IRadSoundHalBuffer* pBuffer )
    {
        Stop( );
        radSoundMixLock lock;
        m_refBuffer = static_cast< radSoundHalBufferLinux* >( pBuffer );
        m_PositionFrames = 0.0;
    }
    virtual IRadSoundHalBuffer* GetBuffer( void ) { return m_refBuffer; }

    virtual void Play( void )
    {
        if ( m_refBuffer == NULL ) return;
        {
            radSoundMixLock lock;
            if ( m_Playing ) return;                    // checked under the lock: the mixer may just have finished it
            if ( s_NumActiveVoices >= MAX_ACTIVE_VOICES )
            {
                if ( SoundLogOn( ) ) fprintf( stderr, "soundlog: mixer: voice not played, %u voices already playing\n", s_NumActiveVoices );
                return;
            }
            m_ActiveIndex = s_NumActiveVoices;
            s_ActiveVoices[ s_NumActiveVoices++ ] = this;
            m_LastGainL = m_LastGainR = -1.0f;          // first mixed block starts at the target gain
            m_Playing = true;
        }
        if ( !m_InUpdateList )
        {
            AddToUpdateList( );
            m_InUpdateList = true;
        }
    }

    virtual void Stop( void )
    {
        {
            radSoundMixLock lock;
            if ( m_Playing ) Deactivate( );
        }
        if ( m_InUpdateList )
        {
            RemoveFromUpdateList( );
            m_InUpdateList = false;
        }
    }

    virtual bool IsPlaying( void ) { return m_Playing; }

    virtual void SetPlaybackPositionInSamples( unsigned int position )
    {
        radSoundMixLock lock;
        if ( m_refBuffer != NULL )
            m_PositionFrames = m_refBuffer->GetFormat( )->SamplesToFrames( position );
    }
    virtual unsigned int GetPlaybackPositionInSamples( void )
    {
        return m_refBuffer != NULL ? m_refBuffer->GetFormat( )->FramesToSamples( (unsigned int) m_PositionFrames ) : 0;
    }

    virtual void SetVolume( float volume ) { m_Volume = volume; }
    virtual float GetVolume( void ) { return m_Volume; }
    virtual void SetTrim( float trim ) { m_Trim = trim; }
    virtual float GetTrim( void ) { return m_Trim; }
    virtual void SetMuted( bool muteOn ) { m_Muted = muteOn; }
    virtual bool GetMuted( void ) { return m_Muted; }
    virtual void SetPan( float pan ) { m_Pan = pan; }
    virtual float GetPan( void ) { return m_Pan; }
    virtual void SetPitch( float pitch ) { m_Pitch = pitch; }
    virtual float GetPitch( void ) { return m_Pitch; }

    virtual void SetAuxMode( unsigned int aux, radSoundAuxMode mode ) { if ( aux < RSD_LINUX_MAX_AUX_SENDS ) m_AuxMode[ aux ] = mode; }
    virtual radSoundAuxMode GetAuxMode( unsigned int aux ) { return aux < RSD_LINUX_MAX_AUX_SENDS ? radSoundAuxMode( m_AuxMode[ aux ] ) : radSoundAuxMode_PostFader; }
    virtual void SetAuxGain( unsigned int aux, float gain ) { if ( aux < RSD_LINUX_MAX_AUX_SENDS ) m_AuxGain[ aux ] = gain; }
    virtual float GetAuxGain( unsigned int aux ) { return aux < RSD_LINUX_MAX_AUX_SENDS ? float( m_AuxGain[ aux ] ) : 0.0f; }

    virtual void SetPositionalGroup( IRadSoundHalPositionalGroup* pGroup )
    {
        radSoundMixLock lock;
        if ( m_xPositionalGroup != NULL ) m_xPositionalGroup->RemovePositionalEntity( this );
        m_xPositionalGroup = static_cast< radSoundHalPositionalGroup* >( pGroup );
        if ( m_xPositionalGroup != NULL ) m_xPositionalGroup->AddPositionalEntity( this );
    }
    virtual IRadSoundHalPositionalGroup* GetPositionalGroup( void ) { return m_xPositionalGroup; }

    // radSoundHalPositionalEntity: the group recomputed its volume/pan/pitch adjustments (the mixer reads
    // them). Here: the reverb send's distance factor, EAX's room rolloff (DirectSound's rolloff law applied
    // to the reflected sound; 0 = none) and silence past the maximum distance (DSBCAPS_MUTE3DATMAXDISTANCE).
    virtual void OnApplyPositionalSettings( void )
    {
        if ( m_xPositionalGroup == NULL ) return;
        radSoundHalPositionalInformation& pi = m_xPositionalGroup->m_RadSoundHalPositionalInformation;
        radSoundVector listener;
        radSoundHalListener::GetInstance( )->GetPosition( &listener );
        float d = listener.GetDistanceBetween( pi.m_Position );
        float rolloff = s_Reverb.RoomRolloff( );
        float minD = pi.m_MinDistance > 0.1f ? pi.m_MinDistance : 0.1f;
        float f = 1.0f;
        if ( !( d < pi.m_MaxDistance ) ) f = 0.0f;
        else if ( rolloff > 0.0f && d > minD ) f = minD / ( minD + rolloff * ( d - minD ) );
        m_WetDistanceFactor = f;
    }

private:
    virtual ~radSoundHalVoiceLinux( void )
    {
        Stop( );
        if ( m_xPositionalGroup != NULL ) m_xPositionalGroup->RemovePositionalEntity( this );
    }

    // radSoundUpdatableObject: called from IRadSoundHalSystem::Service with the elapsed milliseconds.
    // With an audio device the mixer advances the position; this only drops voices it has finished.
    virtual void Update( unsigned int elapsed )
    {
        if ( !m_Playing )
        {
            if ( m_InUpdateList )
            {
                RemoveFromUpdateList( );
                m_InUpdateList = false;
            }
            return;
        }
        if ( s_AudioDevice != 0 || m_refBuffer == NULL ) return;

        // no audio output: advance in real time
        IRadSoundHalAudioFormat* f = m_refBuffer->GetFormat( );
        unsigned int size = m_refBuffer->GetSizeInFrames( );
        if ( f == NULL || size == 0 ) { Stop( ); return; }
        double framesPerMs = (double) f->GetSampleRate( ) / (double) f->GetFrameSizeInSamples( ) / 1000.0;
        m_PositionFrames += framesPerMs * EffectivePitch( ) * (double) elapsed;
        if ( m_PositionFrames >= (double) size )
        {
            if ( m_refBuffer->IsLooping( ) )
            {
                while ( m_PositionFrames >= (double) size ) m_PositionFrames -= (double) size;
            }
            else
            {
                m_PositionFrames = 0.0;               // DirectSound rewinds a finished one-shot buffer
                Stop( );
            }
        }
    }

    float EffectivePitch( void )
    {
        float pitch = m_Pitch;
        if ( m_xPositionalGroup != NULL )
            pitch *= m_xPositionalGroup->m_RadSoundHalPositionalInformation.m_PitchAdjust;
        return pitch;
    }

    // remove from the mixer's play list (device lock held)
    void Deactivate( void )
    {
        m_Playing = false;
        if ( m_ActiveIndex >= 0 )
        {
            radSoundHalVoiceLinux* last = s_ActiveVoices[ --s_NumActiveVoices ];
            s_ActiveVoices[ m_ActiveIndex ] = last;
            last->m_ActiveIndex = m_ActiveIndex;
            m_ActiveIndex = -1;
        }
    }

public:
    // Mix this voice into out (interleaved stereo float, frames output frames) and, when wet is not NULL,
    // its send into the reverb input wet (mono) for reverbAux. Audio thread, lock held.
    // Returns false when a one-shot finished.
    bool MixInto( float* out, float* wet, unsigned int reverbAux, int frames )
    {
        radSoundHalBufferLinux* b = m_refBuffer;
        if ( b == NULL ) return false;
        IRadSoundHalAudioFormat* f = b->GetFormat( );
        unsigned int size = b->GetSizeInFrames( );
        if ( f == NULL || size == 0 || b->GetMemoryObject( ) == NULL ) return false;
        const unsigned char* data = (const unsigned char*) b->GetMemoryObject( )->GetMemoryAddress( );
        int channels = f->GetNumberOfChannels( );
        int bits = f->GetBitResolution( );
        bool looping = b->IsLooping( );

        // gains: volume/trim are analog (amplitude = analog^2), positional rolloff is an amplitude
        float analog = m_Muted ? 0.0f : m_Volume * m_Trim;
        if ( analog < 0.0f ) analog = 0.0f;
        float amp = analog * analog;
        float pan = m_Pan;
        if ( m_xPositionalGroup != NULL )
        {
            radSoundHalPositionalInformation& pi = m_xPositionalGroup->m_RadSoundHalPositionalInformation;
            amp *= pi.m_VolumeAdjust;
            pan = pi.m_PanAdjust * 0.75f;               // keep some of a fully-sided source in the far ear
        }
        if ( pan > 1.0f ) pan = 1.0f; else if ( pan < -1.0f ) pan = -1.0f;
        // DirectSound pan law: attenuate the opposite side (analog), centre = full volume in both
        float l = pan > 0.0f ? ( 1.0f - pan ) : 1.0f;
        float r = pan < 0.0f ? ( 1.0f + pan ) : 1.0f;
        float gainL = amp * l * l, gainR = amp * r * r;
        // reverb send: positional voices only (EAX acted on DirectSound 3D buffers)
        float wetGain = 0.0f;
        if ( wet != NULL && m_xPositionalGroup != NULL && reverbAux < RSD_LINUX_MAX_AUX_SENDS &&
             m_xPositionalGroup->m_RadSoundHalPositionalInformation.m_EnvAuxSend == reverbAux )
        {
            wetGain = ( m_AuxMode[ reverbAux ] == radSoundAuxMode_PreFader ? 1.0f : analog * analog ) *
                      m_AuxGain[ reverbAux ] * m_WetDistanceFactor;
            if ( !( wetGain >= 0.0f && wetGain < 16.0f ) ) wetGain = 0.0f;
        }
        if ( m_LastGainL < 0.0f ) { m_LastGainL = gainL; m_LastGainR = gainR; m_LastWet = wetGain; }
        float dL = ( gainL - m_LastGainL ) / frames, dR = ( gainR - m_LastGainR ) / frames;
        float gL = m_LastGainL, gR = m_LastGainR;
        float dW = ( wetGain - m_LastWet ) / frames, gW = m_LastWet;
        m_LastGainL = gainL; m_LastGainR = gainR; m_LastWet = wetGain;
        if ( wetGain == 0.0f && gW == 0.0f ) wet = NULL;

        double step = (double) f->GetSampleRate( ) * EffectivePitch( ) / (double) s_OutputRate;
        // a NaN from the game (e.g. a positional source at a NaN position) would poison the whole mix
        if ( !( step > 0.0 && step < 64.0 ) || !( gainL >= 0.0f && gainL < 16.0f ) || !( gainR >= 0.0f && gainR < 16.0f ) )
        {
            m_LastGainL = m_LastGainR = -1.0f;
            return true;
        }
        double pos = m_PositionFrames;
        bool finished = false;

        for ( int i = 0; i < frames; i++ )
        {
            if ( pos >= (double) size )
            {
                if ( looping ) { while ( pos >= (double) size ) pos -= (double) size; }
                else { finished = true; break; }
            }
            unsigned int i0 = (unsigned int) pos;
            unsigned int i1 = i0 + 1;
            if ( i1 >= size ) i1 = looping ? 0 : i0;
            float t = (float) ( pos - (double) i0 );
            float sl, sr;
            if ( bits == 16 )
            {
                const short* p = (const short*) data;
                float a0 = p[ i0 * channels ], a1 = p[ i1 * channels ];
                sl = ( a0 + ( a1 - a0 ) * t ) * ( 1.0f / 32768.0f );
                if ( channels > 1 )
                {
                    float b0 = p[ i0 * channels + 1 ], b1 = p[ i1 * channels + 1 ];
                    sr = ( b0 + ( b1 - b0 ) * t ) * ( 1.0f / 32768.0f );
                }
                else sr = sl;
            }
            else
            {
                float a0 = (float) data[ i0 * channels ] - 128.0f, a1 = (float) data[ i1 * channels ] - 128.0f;
                sl = ( a0 + ( a1 - a0 ) * t ) * ( 1.0f / 128.0f );
                if ( channels > 1 )
                {
                    float b0 = (float) data[ i0 * channels + 1 ] - 128.0f, b1 = (float) data[ i1 * channels + 1 ] - 128.0f;
                    sr = ( b0 + ( b1 - b0 ) * t ) * ( 1.0f / 128.0f );
                }
                else sr = sl;
            }
            out[ i * 2 ]     += sl * gL;
            out[ i * 2 + 1 ] += sr * gR;
            gL += dL; gR += dR;
            if ( wet != NULL ) { wet[ i ] += ( sl + sr ) * 0.5f * gW; gW += dW; }
            pos += step;
        }

        if ( finished )
        {
            m_PositionFrames = 0.0;                     // DirectSound rewinds a finished one-shot buffer
            return false;
        }
        if ( looping ) { while ( pos >= (double) size ) pos -= (double) size; }   // never report pos == size
        else if ( pos >= (double) size ) { m_PositionFrames = 0.0; return false; }
        m_PositionFrames = pos;
        return true;
    }

    // Audio thread: finish a one-shot (lock held). The update list entry is dropped by Update.
    void FinishFromMixer( void ) { Deactivate( ); }

private:
    unsigned int m_Priority;
    radSoundShared< bool > m_Playing;       // set under the lock, read without it
    bool   m_InUpdateList;                  // main thread only
    int    m_ActiveIndex;                   // slot in s_ActiveVoices, -1 when not playing
    radSoundShared< double > m_PositionFrames;
    float  m_LastGainL, m_LastGainR;        // gains at the end of the last mixed block (ramped)
    float  m_LastWet;                       // reverb send gain, likewise
    radSoundShared< float > m_WetDistanceFactor;    // main thread (OnApplyPositionalSettings)
    radSoundShared< float > m_Volume, m_Trim;
    radSoundShared< bool > m_Muted;
    radSoundShared< float > m_Pan, m_Pitch;
    radSoundShared< radSoundAuxMode > m_AuxMode[ RSD_LINUX_MAX_AUX_SENDS ];
    radSoundShared< float > m_AuxGain[ RSD_LINUX_MAX_AUX_SENDS ];
    ref< radSoundHalBufferLinux > m_refBuffer;
    ref< radSoundHalPositionalGroup > m_xPositionalGroup;
};

template<> radSoundHalVoiceLinux* radLinkedClass< radSoundHalVoiceLinux >::s_pLinkedClassHead = NULL;
template<> radSoundHalVoiceLinux* radLinkedClass< radSoundHalVoiceLinux >::s_pLinkedClassTail = NULL;

IRadSoundHalVoice* radSoundHalVoiceCreate( radMemoryAllocator allocator )
{
    return new( "radSoundHalVoiceLinux", allocator ) radSoundHalVoiceLinux( );
}

//=============================================================================
// EAX2 reverb effect: holds the parameters and hands them to the mixer's reverb while attached to an aux
// send (the defaults are radsound's win32 ones)
//=============================================================================

class radSoundHalEffectEAX2ReverbLinux : public IRadSoundHalEffectEAX2Reverb, public radSoundObject
{
public:
    IMPLEMENT_REFCOUNTED( "radSoundHalEffectEAX2ReverbLinux" )

    radSoundHalEffectEAX2ReverbLinux( void ) : m_AuxSend( 0xFFFFFFFF ), m_EnvironmentSize( 7.5f ), m_AirAbsorptionHF( -5.0f )
    {
        radSoundLinuxReverbParams p = { -1000, -500, 0.0f, 3.92f, 0.70f, -1230, 0.020f, -2, 0.029f, 1.0f };
        m_P = p;
    }

    virtual void Attach( unsigned int auxSend )
    {
        m_AuxSend = auxSend;
        const char* e = getenv( "SHAR_REVERB" );
        radSoundMixLock lock;
        s_ReverbAux = auxSend;
        s_Reverb.SetParams( m_P );
        s_Reverb.SetEnabled( e == NULL || *e != '0' );
    }
    virtual void Detach( void )
    {
        radSoundMixLock lock;
        if ( s_ReverbAux == m_AuxSend )
        {
            s_ReverbAux = 0xFFFFFFFF;
            s_Reverb.SetEnabled( false );
        }
        m_AuxSend = 0xFFFFFFFF;
    }
    virtual void Update( void ) { }

    virtual void  SetRoom( int v ) { m_P.room = v; Changed( ); }                     virtual int   GetRoom( void ) { return m_P.room; }
    virtual void  SetRoomHF( int v ) { m_P.roomHF = v; Changed( ); }                 virtual int   GetRoomHF( void ) { return m_P.roomHF; }
    virtual void  SetRoomRolloffFactor( float v ) { m_P.roomRolloff = v; Changed( ); } virtual float GetRoomRolloffFactor( void ) { return m_P.roomRolloff; }
    virtual void  SetDecayTime( float v ) { m_P.decayTime = v; Changed( ); }         virtual float GetDecayTime( void ) { return m_P.decayTime; }
    virtual void  SetDecayHFRatio( float v ) { m_P.decayHFRatio = v; Changed( ); }   virtual float GetDecayHFRatio( void ) { return m_P.decayHFRatio; }
    virtual void  SetReflections( int v ) { m_P.reflections = v; Changed( ); }       virtual int   GetReflections( void ) { return m_P.reflections; }
    virtual void  SetReflectionsDelay( float v ) { m_P.reflectionsDelay = v; Changed( ); } virtual float GetReflectionsDelay( void ) { return m_P.reflectionsDelay; }
    virtual void  SetReverb( int v ) { m_P.reverb = v; Changed( ); }                 virtual int   GetReverb( void ) { return m_P.reverb; }
    virtual void  SetReverbDelay( float v ) { m_P.reverbDelay = v; Changed( ); }     virtual float GetReverbDelay( void ) { return m_P.reverbDelay; }
    virtual void  SetEnvironmentDiffusion( float v ) { m_P.diffusion = v; Changed( ); } virtual float GetEnvironmentDiffusion( void ) { return m_P.diffusion; }
    // not modelled: environment size only scales EAX's presets, air absorption is a per-distance HF loss
    virtual void  SetEnvironmentSize( float v ) { m_EnvironmentSize = v; }           virtual float GetEnvironmentSize( void ) { return m_EnvironmentSize; }
    virtual void  SetAirAbsorptionHF( float v ) { m_AirAbsorptionHF = v; }           virtual float GetAirAbsorptionHF( void ) { return m_AirAbsorptionHF; }

private:
    // the game sets the parameters one by one (Win32ReverbController::SetReverbOn); each applies at once
    void Changed( void )
    {
        if ( m_AuxSend == 0xFFFFFFFF ) return;
        radSoundMixLock lock;
        s_Reverb.SetParams( m_P );
    }

    unsigned int m_AuxSend;
    radSoundLinuxReverbParams m_P;
    float m_EnvironmentSize, m_AirAbsorptionHF;
};

IRadSoundHalEffectEAX2Reverb* radSoundHalEffectEAX2ReverbCreate( radMemoryAllocator allocator )
{
    return new( "radSoundHalEffectEAX2ReverbLinux", allocator ) radSoundHalEffectEAX2ReverbLinux( );
}

//=============================================================================
// System
//=============================================================================

static void SDLCALL radSoundMixCallback( void* /*user*/, Uint8* stream, int len )
{
    float* out = (float*) stream;
    int frames = len / ( 2 * sizeof( float ) );
    memset( stream, 0, len );
    unsigned int reverbAux = s_ReverbAux;
    bool reverb = s_Reverb.IsEnabled( ) && reverbAux < RSD_LINUX_MAX_AUX_SENDS &&
                  radSoundHalListener::GetInstance( )->GetEnvEffectsEnabled( ) &&
                  radSoundHalListener::GetInstance( )->GetEnvironmentAuxSend( ) == reverbAux;
    if ( reverb && frames > s_WetCapacity )
    {
        free( s_WetIn );
        s_WetIn = (float*) malloc( frames * sizeof( float ) );
        s_WetCapacity = s_WetIn != NULL ? frames : 0;
        reverb = s_WetIn != NULL;
    }
    if ( reverb ) memset( s_WetIn, 0, frames * sizeof( float ) );
    for ( unsigned int i = 0; i < s_NumActiveVoices; )
    {
        radSoundHalVoiceLinux* v = s_ActiveVoices[ i ];
        if ( v->MixInto( out, reverb ? s_WetIn : NULL, reverbAux, frames ) )
        {
            i++;
        }
        else
        {
            v->FinishFromMixer( );                      // swaps the last voice into slot i
        }
    }
    if ( reverb ) s_Reverb.Process( s_WetIn, out, frames, s_AuxGain[ reverbAux ] );
    radSoundMixMovie( out, frames );
    for ( int i = 0; i < frames * 2; i++ )             // soft limit instead of hard clipping
    {
        float x = out[ i ];
        if ( x > 1.0f || x < -1.0f ) out[ i ] = x / ( 1.0f + ( ( x > 0 ? x : -x ) - 1.0f ) );
    }
}

static void radSoundOpenAudio( void )
{
    if ( getenv( "SHAR_NO_AUDIO" ) != NULL )
    {
        fprintf( stderr, "radsound: SHAR_NO_AUDIO set, running silent\n" );
        return;
    }
    if ( SDL_InitSubSystem( SDL_INIT_AUDIO ) != 0 )
    {
        fprintf( stderr, "radsound: SDL audio init failed (%s), running silent\n", SDL_GetError( ) );
        return;
    }
    SDL_AudioSpec want, have;
    memset( &want, 0, sizeof( want ) );
    want.freq = 48000;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = radSoundMixCallback;
    s_AudioDevice = SDL_OpenAudioDevice( NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE );
    if ( s_AudioDevice == 0 )
    {
        fprintf( stderr, "radsound: no audio device (%s), running silent\n", SDL_GetError( ) );
        return;
    }
    s_OutputRate = have.freq;
    s_Reverb.Open( s_OutputRate );
    s_WetCapacity = have.samples * 2;
    s_WetIn = (float*) malloc( s_WetCapacity * sizeof( float ) );
    fprintf( stderr, "radsound: SDL audio driver %s, %d Hz, %d-frame buffer\n",
             SDL_GetCurrentAudioDriver( ), have.freq, have.samples );
    SDL_PauseAudioDevice( s_AudioDevice, 0 );
}

static void radSoundCloseAudio( void )
{
    if ( s_AudioDevice != 0 )
    {
        SDL_CloseAudioDevice( s_AudioDevice );
        s_AudioDevice = 0;
        SDL_QuitSubSystem( SDL_INIT_AUDIO );
    }
}

class radSoundHalSystemLinux : public IRadSoundHalSystem, public radSoundObject
{
public:
    IMPLEMENT_REFCOUNTED( "radSoundHalSystemLinux" )

    static radSoundHalSystemLinux* s_pSystem;

    radSoundHalSystemLinux( void )
        : m_NumAuxSends( 0 ), m_pSoundMemory( NULL ), m_LastServiceTime( ::radTimeGetMilliseconds( ) ), m_LastLogTime( 0 )
    {
        s_pSystem = this;
        ::radSoundPrintBanner( );
    }

    virtual void Initialize( const SystemDescription& desc )
    {
        m_NumAuxSends = desc.m_NumAuxSends < RSD_LINUX_MAX_AUX_SENDS ? desc.m_NumAuxSends : RSD_LINUX_MAX_AUX_SENDS;
        radSoundHalListener::Initialize( GetThisAllocator( ) );
        m_pSoundMemory = ::radMemoryAllocAligned( GetThisAllocator( ), desc.m_ReservedSoundMemory,
                                                  radSoundHalDataSourceReadAlignmentGet( ) );
        radSoundHalMemoryRegion::Initialize( m_pSoundMemory, desc.m_ReservedSoundMemory, desc.m_MaxRootAllocations,
                                             radSoundHalDataSourceReadAlignmentGet( ), radMemorySpace_Local,
                                             GetThisAllocator( ) );
        radSoundOpenAudio( );
    }

    virtual IRadSoundHalMemoryRegion* GetRootMemoryRegion( void ) { return radSoundHalMemoryRegion::GetRootRegion( ); }
    virtual unsigned int GetNumAuxSends( ) { return m_NumAuxSends; }

    virtual void SetAuxEffect( unsigned int aux, IRadSoundHalEffect* pEffect )
    {
        rAssert( aux < m_NumAuxSends );
        if ( m_refEffect[ aux ] != NULL ) m_refEffect[ aux ]->Detach( );
        m_refEffect[ aux ] = pEffect;
        if ( m_refEffect[ aux ] != NULL ) m_refEffect[ aux ]->Attach( aux );
    }
    virtual IRadSoundHalEffect* GetAuxEffect( unsigned int aux ) { return aux < m_NumAuxSends ? (IRadSoundHalEffect*) m_refEffect[ aux ] : NULL; }
    // the reverb's input fades with this (ReverbController; DirectSound's HAL ignored it)
    virtual void SetAuxGain( unsigned int aux, float gain )
    {
        if ( aux >= RSD_LINUX_MAX_AUX_SENDS ) return;
        if ( !( gain >= 0.0f ) ) gain = 0.0f;
        if ( radSoundReverbLog( ) && aux == s_ReverbAux && ( gain == 0.0f ) != ( s_AuxGain[ aux ] == 0.0f ) )
            fprintf( stderr, "[reverb] %s\n", gain > 0.0f ? "fading in" : "off" );
        s_AuxGain[ aux ] = gain;
    }
    virtual float GetAuxGain( unsigned int aux ) { return aux < RSD_LINUX_MAX_AUX_SENDS ? float( s_AuxGain[ aux ] ) : 0.0f; }
    virtual void SetOutputMode( radSoundOutputMode mode ) { }
    virtual radSoundOutputMode GetOutputMode( void ) { return radSoundOutputMode_Stereo; }

    virtual void Service( void )
    {
        unsigned int now = ::radTimeGetMilliseconds( );
        radSoundUpdatableObject::UpdateAll( now - m_LastServiceTime );     // advances playing voices
        m_LastServiceTime = now;
        if ( SoundLogOn( ) && now - m_LastLogTime >= 5000 )
        {
            m_LastLogTime = now;
            Stats st;
            GetStats( &st );
            unsigned int active;
            { radSoundMixLock lock; active = s_NumActiveVoices; }
            fprintf( stderr, "soundlog: mixer: voices %u (%u playing), positional %u (%u playing), in mix %u; buffers %u (%u KB), free sound memory %u KB\n",
                     st.m_NumVoices, st.m_NumVoicesPlaying, st.m_NumPosVoices, st.m_NumPosVoicesPlaying, active,
                     st.m_NumBuffers, st.m_BufferMemoryUsed / 1024, st.m_TotalFreeSoundMemory / 1024 );
        }
    }

    virtual void ServiceOncePerFrame( void )
    {
        radSoundHalListener::GetInstance( )->UpdatePositionalSettings( );
    }

    virtual void GetStats( IRadSoundHalSystem::Stats* pStats )
    {
        memset( pStats, 0, sizeof( *pStats ) );
        for ( radSoundHalVoiceLinux* v = radSoundHalVoiceLinux::GetLinkedClassHead( ); v; v = v->GetLinkedClassNext( ) )
        {
            bool positional = v->GetPositionalGroup( ) != NULL;
            ( positional ? pStats->m_NumPosVoices : pStats->m_NumVoices )++;
            if ( v->IsPlaying( ) ) ( positional ? pStats->m_NumPosVoicesPlaying : pStats->m_NumVoicesPlaying )++;
        }
        for ( radSoundHalBufferLinux* b = radSoundHalBufferLinux::GetLinkedClassHead( ); b; b = b->GetLinkedClassNext( ) )
        {
            pStats->m_NumBuffers++;
            pStats->m_BufferMemoryUsed += b->GetSizeInBytes( );
        }
        radSoundHalMemoryRegion::GetRootRegion( )->GetStats( &pStats->m_TotalFreeSoundMemory, NULL, NULL, true );
    }

    virtual ~radSoundHalSystemLinux( void )
    {
        radSoundCloseAudio( );
        radSoundHalListener::Terminate( );
        radSoundHalMemoryRegion::Terminate( );
        if ( m_pSoundMemory ) ::radMemoryFreeAligned( GetThisAllocator( ), m_pSoundMemory );
        s_pSystem = NULL;
    }

private:
    unsigned int m_NumAuxSends;
    void* m_pSoundMemory;
    unsigned int m_LastServiceTime;
    unsigned int m_LastLogTime;         // SHAR_SOUND_LOG stats
    ref< IRadSoundHalEffect > m_refEffect[ RSD_LINUX_MAX_AUX_SENDS ];
};

radSoundHalSystemLinux* radSoundHalSystemLinux::s_pSystem = NULL;

void radSoundHalSystemInitialize( radMemoryAllocator allocator )
{
    rAssert( radSoundHalSystemLinux::s_pSystem == NULL );
    new( "radSoundHalSystem", allocator ) radSoundHalSystemLinux( );
    radSoundHalSystemLinux::s_pSystem->AddRef( );
}

void radSoundHalSystemTerminate( void )
{
    radRelease( radSoundHalSystemLinux::s_pSystem, NULL );
}

IRadSoundHalSystem* radSoundHalSystemGet( void )
{
    return radSoundHalSystemLinux::s_pSystem;
}
