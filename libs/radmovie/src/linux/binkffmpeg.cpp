//=============================================================================
// Linux port: the subset of the Bink 1.5 SDK that radmovie's binkmovieplayer.cpp uses, implemented on
// FFmpeg (libavformat's Bink demuxer, libavcodec's Bink video/audio decoders).
//
// Timing: frame n (1 based) is due (n-1) / fps seconds after Play, on a wall clock that stops while paused.
// Audio: the selected track is decoded as the demuxer reaches it (kept ~0.5 s ahead of the clock), resampled
// to stereo float at the mixer rate and pushed into the radsound mixer's movie stream (linuxhal.cpp), which
// starts and stops with the clock. Video frames are converted to BGRA (BINKSURFACE32, as the GL pddi's
// native 32-bit texture lock expects).
//=============================================================================

#include "pch.hpp"
#include <bink.h>
#include <radtime.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <shar_mods.h>
#include <unistd.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

// libs/radcore/src/radfile/linux/linuxdrive.cpp: game path ("movies\intro.rmv") -> real path, any case
void radLinuxResolvePath( const char* gamePath, char* realPath, unsigned int size, bool leafMayNotExist );

// libs/radsound/src/hal/linux/linuxhal.cpp
void radSoundLinuxMovieStreamOpen( unsigned int seconds );
void radSoundLinuxMovieStreamClose( void );
unsigned int radSoundLinuxMovieStreamOutputRate( void );
unsigned int radSoundLinuxMovieStreamWrite( const float* stereo, unsigned int frames );
void radSoundLinuxMovieStreamSetRunning( bool running );
void radSoundLinuxMovieStreamSetGains( float left, float right );

static const unsigned int MAX_QUEUED_VIDEO = 64;    // video packets read ahead while looking for audio
static const double AUDIO_LEAD_SECONDS = 0.5;       // decode audio this far ahead of the clock
static const unsigned int AUDIO_RING_SECONDS = 4;

static char s_Error[ 256 ] = "";
static U32 s_SoundTrack = 0;
static bool s_HasSoundTrack = false;

struct BinkFFmpegState
{
    AVFormatContext* fmt;
    AVCodecContext* video;
    AVCodecContext* audio;
    int videoStream;
    int audioStream;
    SwsContext* sws;
    SwrContext* swr;
    AVFrame* frame;
    AVPacket* packet;
    AVPacket* queue[ MAX_QUEUED_VIDEO ];
    unsigned int queueHead;
    unsigned int queueCount;
    bool eof;

    unsigned char* rgba;            // decoded frame, BGRA, top row first
    int rgbaPitch;
    bool haveFrame;

    double audioDecodedSeconds;     // audio pushed to the mixer so far
    float* mixBuffer;
    int mixBufferFrames;
    S32 volume;
    S32 pan;

    bool paused;
    unsigned long long startMicros; // clock origin (shifted forward by time spent paused)
    unsigned long long pauseMicros;
};

static double ClockSeconds( BinkFFmpegState* s )
{
    unsigned long long now = s->paused ? s->pauseMicros : ::radTimeGetMicroseconds64( );
    return ( now - s->startMicros ) / 1000000.0;
}

static void UpdateGains( BinkFFmpegState* s )
{
    float v = s->volume / 32768.0f;
    float p = s->pan / 32768.0f;                    // 0 left, 1 centre, 2 right
    float l = p > 1.0f ? 2.0f - p : 1.0f;
    float r = p < 1.0f ? p : 1.0f;
    radSoundLinuxMovieStreamSetGains( v * l, v * r );
}

static void DecodeAudio( BinkFFmpegState* s, AVPacket* pkt )
{
    if ( avcodec_send_packet( s->audio, pkt ) < 0 )
    {
        return;
    }
    while ( avcodec_receive_frame( s->audio, s->frame ) == 0 )
    {
        int outRate = (int) radSoundLinuxMovieStreamOutputRate( );
        int maxOut = swr_get_out_samples( s->swr, s->frame->nb_samples );
        if ( maxOut > s->mixBufferFrames )
        {
            free( s->mixBuffer );
            s->mixBufferFrames = maxOut;
            s->mixBuffer = (float*) malloc( maxOut * 2 * sizeof( float ) );
        }
        uint8_t* out = (uint8_t*) s->mixBuffer;
        int got = swr_convert( s->swr, &out, maxOut, (const uint8_t**) s->frame->extended_data, s->frame->nb_samples );
        if ( got > 0 )
        {
            radSoundLinuxMovieStreamWrite( s->mixBuffer, (unsigned int) got );
            s->audioDecodedSeconds += (double) got / outRate;
        }
        av_frame_unref( s->frame );
    }
}

// Demux until a video packet is queued and the audio is AUDIO_LEAD_SECONDS ahead of the clock.
static void ReadAhead( BinkFFmpegState* s )
{
    while ( !s->eof && s->queueCount < MAX_QUEUED_VIDEO )
    {
        bool needAudio = s->audio != NULL && s->audioDecodedSeconds < ClockSeconds( s ) + AUDIO_LEAD_SECONDS;
        if ( s->queueCount > 0 && !needAudio )
        {
            break;
        }
        if ( av_read_frame( s->fmt, s->packet ) < 0 )
        {
            s->eof = true;
            break;
        }
        if ( s->packet->stream_index == s->videoStream )
        {
            AVPacket* q = av_packet_alloc( );
            av_packet_move_ref( q, s->packet );
            s->queue[ ( s->queueHead + s->queueCount ) % MAX_QUEUED_VIDEO ] = q;
            s->queueCount++;
        }
        else if ( s->packet->stream_index == s->audioStream )
        {
            DecodeAudio( s, s->packet );
        }
        av_packet_unref( s->packet );
    }
}

static AVCodecContext* OpenDecoder( AVStream* st )
{
    const AVCodec* codec = avcodec_find_decoder( st->codecpar->codec_id );
    if ( codec == NULL )
    {
        return NULL;
    }
    AVCodecContext* c = avcodec_alloc_context3( codec );
    avcodec_parameters_to_context( c, st->codecpar );
    c->thread_count = 1;
    if ( avcodec_open2( c, codec, NULL ) < 0 )
    {
        avcodec_free_context( &c );
        return NULL;
    }
    return c;
}

void RADSetMemory( RADMEMALLOC, RADMEMFREE ) { }
void BinkSetIO( BINKIOOPEN ) { }
char* BinkGetError( void ) { return s_Error; }

void BinkSetSoundTrack( U32 total_tracks, U32* tracks )
{
    s_HasSoundTrack = total_tracks > 0 && tracks != NULL;
    s_SoundTrack = s_HasSoundTrack ? tracks[ 0 ] : 0;
}

HBINK BinkOpen( const char* name, U32 flags )
{
    // Mods (port/compat/shar_mods.h): occluded movies do not exist, others may be redirected or replaced; a
    // replacement inside a .lmlm package is opened as a byte range of it (FFmpeg's subfile protocol)
    char path[ 1024 ];
    SharModFile modFile;
    const char* redirect = NULL;
    int modFd = -1;
    switch ( name[ 0 ] == '/' ? shar_ModVfsResolve( name, &modFile, &redirect ) : shar_ModResolve( name, &modFile, &redirect ) )
    {
    case SHAR_MOD_MISSING:
        snprintf( s_Error, sizeof( s_Error ), "%s: %s", name[ 0 ] == '/' ? "not found" : "occluded by a mod", name );
        return NULL;
    case SHAR_MOD_FILE:
        if ( modFile.fd >= 0 )
        {
            modFd = modFile.fd;
            snprintf( path, sizeof( path ), "/proc/self/fd/%d", modFd );
        }
        else if ( modFile.packaged )
        {
            snprintf( path, sizeof( path ), "subfile,,start,%llu,end,%llu,,:%s", modFile.offset,
                      modFile.offset + modFile.size, modFile.path );
        }
        else
        {
            snprintf( path, sizeof( path ), "%s", modFile.path );
        }
        break;
    case SHAR_MOD_REDIRECT:
        radLinuxResolvePath( redirect, path, sizeof( path ), false );
        break;
    default:
        radLinuxResolvePath( name, path, sizeof( path ), false );
    }

    av_log_set_level( AV_LOG_ERROR );
    AVFormatContext* fmt = NULL;
    int err = avformat_open_input( &fmt, path, NULL, NULL );
    if ( modFd >= 0 )
    {
        close( modFd );             // FFmpeg opened its own descriptor
    }
    if ( err < 0 || avformat_find_stream_info( fmt, NULL ) < 0 )
    {
        av_strerror( err, s_Error, sizeof( s_Error ) );
        if ( fmt != NULL ) avformat_close_input( &fmt );
        return NULL;
    }

    // Real Bink only opens Bink files. Some copies replace the logo movies with tiny Ogg files to
    // skip them; FFmpeg would play those (1280x720: more tiles than the render strategy holds).
    if ( strcmp( fmt->iformat->name, "bink" ) != 0 )
    {
        snprintf( s_Error, sizeof( s_Error ), "not a Bink file (%s): %s", fmt->iformat->name, path );
        avformat_close_input( &fmt );
        return NULL;
    }

    BinkFFmpegState* s = (BinkFFmpegState*) calloc( 1, sizeof( BinkFFmpegState ) );
    s->fmt = fmt;
    s->videoStream = av_find_best_stream( fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0 );
    s->audioStream = -1;
    s->volume = 32768;
    s->pan = 32768;
    s->paused = true;
    s->startMicros = s->pauseMicros = ::radTimeGetMicroseconds64( );
    s->frame = av_frame_alloc( );
    s->packet = av_packet_alloc( );

    if ( s->videoStream < 0 || ( s->video = OpenDecoder( fmt->streams[ s->videoStream ] ) ) == NULL )
    {
        snprintf( s_Error, sizeof( s_Error ), "no decodable video stream in %s", path );
        av_frame_free( &s->frame );
        av_packet_free( &s->packet );
        avformat_close_input( &s->fmt );
        free( s );
        return NULL;
    }

    // Bink's track ids; FFmpeg keeps them as stream ids (fall back to the n-th audio stream)
    if ( s_HasSoundTrack && !( flags & BINKLINUXNOAUDIO ) )
    {
        int nth = 0, byIndex = -1;
        for ( unsigned int i = 0; i < fmt->nb_streams; i++ )
        {
            if ( fmt->streams[ i ]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO ) continue;
            if ( (U32) fmt->streams[ i ]->id == s_SoundTrack ) { s->audioStream = i; break; }
            if ( (U32) nth++ == s_SoundTrack ) byIndex = i;
        }
        if ( s->audioStream < 0 ) s->audioStream = byIndex;
    }
    if ( s->audioStream >= 0 )
    {
        s->audio = OpenDecoder( fmt->streams[ s->audioStream ] );
        if ( s->audio != NULL )
        {
            AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
            if ( swr_alloc_set_opts2( &s->swr, &stereo, AV_SAMPLE_FMT_FLT, (int) radSoundLinuxMovieStreamOutputRate( ),
                                      &s->audio->ch_layout, s->audio->sample_fmt, s->audio->sample_rate, 0, NULL ) < 0 ||
                 swr_init( s->swr ) < 0 )
            {
                swr_free( &s->swr );
                avcodec_free_context( &s->audio );
            }
        }
        if ( s->audio == NULL )
        {
            s->audioStream = -1;
        }
        else
        {
            radSoundLinuxMovieStreamOpen( AUDIO_RING_SECONDS );
            UpdateGains( s );
        }
    }

    AVStream* vs = fmt->streams[ s->videoStream ];
    HBINK bink = (HBINK) calloc( 1, sizeof( BINK ) );
    bink->ffmpeg = s;
    bink->Width = s->video->width;
    bink->Height = s->video->height;
    bink->Frames = (U32) ( vs->nb_frames > 0 ? vs->nb_frames : vs->duration );
    AVRational fps = vs->avg_frame_rate.num > 0 ? vs->avg_frame_rate : av_inv_q( vs->time_base );
    bink->FrameRate = fps.num;
    bink->FrameRateDiv = fps.den;
    bink->FrameNum = 1;
    bink->LastFrameNum = 0;

    s->rgbaPitch = bink->Width * 4;
    s->rgba = (unsigned char*) calloc( bink->Height, s->rgbaPitch );
    s->sws = sws_getContext( bink->Width, bink->Height, s->video->pix_fmt, bink->Width, bink->Height,
                             AV_PIX_FMT_BGRA, SWS_POINT, NULL, NULL, NULL );

    ReadAhead( s );                                     // preload the first frame and the audio lead
    return bink;
}

void BinkClose( HBINK bink )
{
    if ( bink == NULL ) return;
    BinkFFmpegState* s = bink->ffmpeg;
    if ( s->audio != NULL )
    {
        radSoundLinuxMovieStreamClose( );
    }
    for ( unsigned int i = 0; i < s->queueCount; i++ )
    {
        av_packet_free( &s->queue[ ( s->queueHead + i ) % MAX_QUEUED_VIDEO ] );
    }
    sws_freeContext( s->sws );
    swr_free( &s->swr );
    avcodec_free_context( &s->video );
    avcodec_free_context( &s->audio );
    av_frame_free( &s->frame );
    av_packet_free( &s->packet );
    avformat_close_input( &s->fmt );
    free( s->mixBuffer );
    free( s->rgba );
    free( s );
    free( bink );
}

S32 BinkPause( HBINK bink, S32 pause )
{
    BinkFFmpegState* s = bink->ffmpeg;
    bool p = pause != 0;
    if ( p != s->paused )
    {
        unsigned long long now = ::radTimeGetMicroseconds64( );
        if ( p )
        {
            s->pauseMicros = now;
        }
        else
        {
            s->startMicros += now - s->pauseMicros;
        }
        s->paused = p;
        if ( s->audio != NULL )
        {
            radSoundLinuxMovieStreamSetRunning( !p );
        }
    }
    return pause;
}

S32 BinkWait( HBINK bink )
{
    BinkFFmpegState* s = bink->ffmpeg;
    ReadAhead( s );                                     // keeps the audio flowing while we wait
    if ( s->paused )
    {
        return 1;
    }
    double due = (double) ( bink->FrameNum - 1 ) * bink->FrameRateDiv / bink->FrameRate;
    return ClockSeconds( s ) >= due ? 0 : 1;
}

S32 BinkDoFrame( HBINK bink )
{
    BinkFFmpegState* s = bink->ffmpeg;
    ReadAhead( s );
    if ( s->queueCount == 0 )
    {
        return 1;
    }
    AVPacket* pkt = s->queue[ s->queueHead ];
    s->queueHead = ( s->queueHead + 1 ) % MAX_QUEUED_VIDEO;
    s->queueCount--;
    int sent = avcodec_send_packet( s->video, pkt );
    av_packet_free( &pkt );
    if ( sent < 0 || avcodec_receive_frame( s->video, s->frame ) < 0 )
    {
        return 1;
    }
    uint8_t* dst[ 4 ] = { s->rgba, NULL, NULL, NULL };
    int dstPitch[ 4 ] = { s->rgbaPitch, 0, 0, 0 };
    sws_scale( s->sws, s->frame->data, s->frame->linesize, 0, bink->Height, dst, dstPitch );
    av_frame_unref( s->frame );
    s->haveFrame = true;
    bink->LastFrameNum = bink->FrameNum;
    return 0;
}

void BinkNextFrame( HBINK bink )
{
    bink->FrameNum++;
}

// Bink convention: dest is the top-left of the destination in memory; a negative pitch flips vertically
// (row 0 of the rect lands on the last of destheight rows), which is how radmovie feeds Pure3D's
// bottom-up texture locks.
S32 BinkCopyToBufferRect( HBINK bink, void* dest, S32 destpitch, U32 destheight, U32 destx, U32 desty,
                          U32 srcx, U32 srcy, U32 srcw, U32 srch, U32 /*flags*/ )
{
    BinkFFmpegState* s = bink->ffmpeg;
    if ( !s->haveFrame || srcx >= bink->Width || srcy >= bink->Height )
    {
        return 1;
    }
    if ( srcx + srcw > bink->Width ) srcw = bink->Width - srcx;
    if ( srcy + srch > bink->Height ) srch = bink->Height - srcy;
    unsigned char* base = (unsigned char*) dest;
    int step = destpitch;
    if ( destpitch < 0 )
    {
        base += ( destheight - 1 ) * (unsigned int) ( -destpitch );
    }
    for ( U32 r = 0; r < srch; r++ )
    {
        memcpy( base + (int) ( desty + r ) * step + destx * 4,
                s->rgba + ( srcy + r ) * s->rgbaPitch + srcx * 4, srcw * 4 );
    }
    return 0;
}

void BinkSetVolume( HBINK bink, U32 /*trackid*/, S32 volume )
{
    bink->ffmpeg->volume = volume;
    if ( bink->ffmpeg->audio != NULL ) UpdateGains( bink->ffmpeg );
}

void BinkSetPan( HBINK bink, U32 /*trackid*/, S32 pan )
{
    bink->ffmpeg->pan = pan;
    if ( bink->ffmpeg->audio != NULL ) UpdateGains( bink->ffmpeg );
}

void BinkGetSummary( HBINK bink, BINKSUMMARY* sum )
{
    memset( sum, 0, sizeof( *sum ) );
    sum->Width = bink->Width;
    sum->Height = bink->Height;
    sum->FileFrameRate = sum->FrameRate = bink->FrameRate;
    sum->FileFrameRateDiv = sum->FrameRateDiv = bink->FrameRateDiv;
    sum->TotalFrames = bink->Frames;
    sum->TotalPlayedFrames = bink->LastFrameNum;
}

void radBinkFileInitialize( radMemoryAllocator ) { }
void radBinkFileTerminate( void ) { }
S32 radBinkFileOpen( struct BINKIO*, const char*, U32 ) { return 0; }
