//=============================================================================
// Linux port: stand-in for the Bink 1.5 SDK header (bink.h), declaring only what radmovie's
// binkmovieplayer.cpp uses. Implemented on FFmpeg in binkffmpeg.cpp; movie audio goes to the
// radsound SDL mixer (linuxhal.cpp) instead of DirectSound.
//=============================================================================

#ifndef BINK_LINUX_SHIM_H
#define BINK_LINUX_SHIM_H

#define BINKMAJORVERSION 1
#define BINKMINORVERSION 5
#define BINKSUBVERSION   19

typedef unsigned int U32;
typedef int S32;
typedef unsigned char U8;

#define RADLINK
#define RADEXPLINK
#define RADEXPFUNC
#define PTR4

#define BINKSNDTRACK      0x00004000L
#define BINKIOPROCESSOR   0x02000000L
#define BINKNOTHREADEDIO  0x08000000L
#define BINKCOPYALL       0x80000000L
#define BINKSURFACE32     3
#define BINKLINUXNOAUDIO  0x00080000L     // Linux port: open no audio track (mods' video textures, bink_videotexture.cpp)

struct BINKIO;
typedef S32 ( RADLINK PTR4* BINKIOOPEN )( struct BINKIO PTR4* Bnkio, const char PTR4* name, U32 flags );

struct BinkFFmpegState;

typedef struct BINK
{
    U32 Width;              // 1 based
    U32 Height;
    U32 Frames;             // number of frames
    U32 FrameNum;           // frame to *be* displayed (1 based)
    U32 LastFrameNum;       // last frame decompressed (1 based)
    U32 FrameRate;          // frame rate = FrameRate / FrameRateDiv
    U32 FrameRateDiv;
    BinkFFmpegState* ffmpeg;
} BINK, PTR4* HBINK;

typedef struct BINKSUMMARY
{
    U32 Width, Height, TotalTime, FileFrameRate, FileFrameRateDiv, FrameRate, FrameRateDiv;
    U32 TotalOpenTime, TotalFrames, TotalPlayedFrames, SkippedFrames, SkippedBlits, SoundSkips;
    U32 TotalBlitTime, TotalReadTime, TotalVideoDecompTime, TotalAudioDecompTime, TotalIdleReadTime;
    U32 TotalBackReadTime, TotalReadSpeed, SlowestFrameTime, Slowest2FrameTime, SlowestFrameNum;
    U32 Slowest2FrameNum, AverageDataRate, AverageFrameSize, HighestMemAmount, TotalIOMemory;
    U32 HighestIOUsed, Highest1SecRate, Highest1SecFrame;
} BINKSUMMARY;

typedef void* ( RADLINK PTR4* RADMEMALLOC )( unsigned long bytes );
typedef void  ( RADLINK PTR4* RADMEMFREE )( void PTR4* ptr );

void  RADSetMemory( RADMEMALLOC a, RADMEMFREE f );     // ignored: FFmpeg allocates with malloc

HBINK BinkOpen( const char* name, U32 flags );           // name may be a mods' virtual path ("/GameData/...")
void  BinkClose( HBINK bnk );
S32   BinkDoFrame( HBINK bnk );                        // 0 = decoded
void  BinkNextFrame( HBINK bnk );
S32   BinkWait( HBINK bnk );                           // 0 = time to show the next frame
S32   BinkPause( HBINK bnk, S32 pause );
S32   BinkCopyToBufferRect( HBINK bnk, void* dest, S32 destpitch, U32 destheight, U32 destx, U32 desty,
                            U32 srcx, U32 srcy, U32 srcw, U32 srch, U32 flags );   // 0 = copied
void  BinkSetSoundTrack( U32 total_tracks, U32* tracks );
void  BinkSetIO( BINKIOOPEN io );                      // ignored: files are opened through the Linux drive paths
void  BinkSetVolume( HBINK bnk, U32 trackid, S32 volume );     // 0..32768 (amplitude)
void  BinkSetPan( HBINK bnk, U32 trackid, S32 pan );           // 0 left, 32768 centre, 65536 right
void  BinkGetSummary( HBINK bnk, BINKSUMMARY* sum );
char* BinkGetError( void );

// binkradfile.cpp replacements (radmovie's radFile-based Bink IO is not used)
#include <radmemory.hpp>
void radBinkFileInitialize( radMemoryAllocator alloc );
void radBinkFileTerminate( void );
S32  radBinkFileOpen( struct BINKIO* bio, const char* name, U32 flags );

#endif // BINK_LINUX_SHIM_H
