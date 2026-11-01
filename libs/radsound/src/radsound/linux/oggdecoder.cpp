//=============================================================================
// Linux port: Ogg Vorbis decoding for mods, see oggdecoder.hpp.
//=============================================================================

#include "oggdecoder.hpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libswresample/swresample.h>
}

struct radOggDecoder
{
    unsigned char* data;
    unsigned int size;
    unsigned int readPos;
    AVIOContext* io;
    AVFormatContext* fmt;
    AVCodecContext* codec;
    SwrContext* swr;
    AVPacket* packet;
    AVFrame* frame;
    int stream;
    unsigned int rate;
    unsigned int channels;
    unsigned int totalFrames;
    short* pending;             // decoded frames not handed out yet
    unsigned int pendingCapacity;
    unsigned int pendingOffset;
    unsigned int pendingFrames;
    bool eof;
};

static int ReadPacket( void* opaque, uint8_t* buf, int size )
{
    radOggDecoder* d = (radOggDecoder*) opaque;
    unsigned int left = d->size - d->readPos;
    if ( left == 0 )
    {
        return AVERROR_EOF;
    }
    if ( (unsigned int) size > left )
    {
        size = (int) left;
    }
    memcpy( buf, d->data + d->readPos, size );
    d->readPos += size;
    return size;
}

static int64_t Seek( void* opaque, int64_t offset, int whence )
{
    radOggDecoder* d = (radOggDecoder*) opaque;
    if ( whence == AVSEEK_SIZE )
    {
        return d->size;
    }
    int64_t pos = offset;
    if ( ( whence & ~AVSEEK_FORCE ) == SEEK_CUR )
    {
        pos += d->readPos;
    }
    else if ( ( whence & ~AVSEEK_FORCE ) == SEEK_END )
    {
        pos += d->size;
    }
    if ( pos < 0 || pos > d->size )
    {
        return -1;
    }
    d->readPos = (unsigned int) pos;
    return pos;
}

void radOggDecoderClose( radOggDecoder* d )
{
    if ( d == NULL )
    {
        return;
    }
    av_frame_free( &d->frame );
    av_packet_free( &d->packet );
    swr_free( &d->swr );
    avcodec_free_context( &d->codec );
    avformat_close_input( &d->fmt );
    if ( d->io != NULL )
    {
        av_freep( &d->io->buffer );
        avio_context_free( &d->io );
    }
    free( d->pending );
    free( d->data );
    free( d );
}

radOggDecoder* radOggDecoderOpen( unsigned char* data, unsigned int size, const char* name )
{
    av_log_set_level( AV_LOG_ERROR );      // e.g. "Could not update timestamps for discarded samples" is routine
    radOggDecoder* d = (radOggDecoder*) calloc( 1, sizeof( radOggDecoder ) );
    d->data = data;
    d->size = size;
    d->stream = -1;

    const int IO_BUFFER = 16384;
    unsigned char* ioBuffer = (unsigned char*) av_malloc( IO_BUFFER );
    d->io = avio_alloc_context( ioBuffer, IO_BUFFER, 0, d, ReadPacket, NULL, Seek );
    d->fmt = avformat_alloc_context( );
    d->fmt->pb = d->io;
    d->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;

    const AVCodec* codec = NULL;
    AVCodecParameters* par = NULL;
    AVChannelLayout layout;
    const char* format = size >= 4 && radOggIsOgg( data ) ? "ogg" : size >= 4 && data[ 0 ] == 'f' ? "flac" : NULL;
    if ( avformat_open_input( &d->fmt, NULL, format ? av_find_input_format( format ) : NULL, NULL ) < 0 ||
         avformat_find_stream_info( d->fmt, NULL ) < 0 ||
         ( d->stream = av_find_best_stream( d->fmt, AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0 ) ) < 0 )
    {
        fprintf( stderr, "radsound: %s: not a decodable Ogg or FLAC file\n", name );
        radOggDecoderClose( d );
        return NULL;
    }
    par = d->fmt->streams[ d->stream ]->codecpar;
    d->codec = avcodec_alloc_context3( codec );
    if ( avcodec_parameters_to_context( d->codec, par ) < 0 || avcodec_open2( d->codec, codec, NULL ) < 0 ||
         par->ch_layout.nb_channels < 1 || par->ch_layout.nb_channels > 2 || par->sample_rate <= 0 )
    {
        fprintf( stderr, "radsound: %s: unsupported audio stream (%d channels)\n", name, par->ch_layout.nb_channels );
        radOggDecoderClose( d );
        return NULL;
    }
    d->rate = (unsigned int) par->sample_rate;
    d->channels = (unsigned int) par->ch_layout.nb_channels;
    av_channel_layout_default( &layout, (int) d->channels );
    if ( swr_alloc_set_opts2( &d->swr, &layout, AV_SAMPLE_FMT_S16, par->sample_rate,
                              &d->codec->ch_layout, d->codec->sample_fmt, par->sample_rate, 0, NULL ) < 0 ||
         swr_init( d->swr ) < 0 )
    {
        fprintf( stderr, "radsound: %s: cannot convert its samples\n", name );
        radOggDecoderClose( d );
        return NULL;
    }

    // Length: the stream duration (the last granule position), in samples
    AVStream* st = d->fmt->streams[ d->stream ];
    int64_t duration = st->duration;
    if ( duration != AV_NOPTS_VALUE && duration > 0 )
    {
        d->totalFrames = (unsigned int) av_rescale_q( duration, st->time_base, AVRational{ 1, par->sample_rate } );
    }
    else if ( d->fmt->duration != AV_NOPTS_VALUE && d->fmt->duration > 0 )
    {
        d->totalFrames = (unsigned int) av_rescale( d->fmt->duration, par->sample_rate, AV_TIME_BASE );
    }

    d->packet = av_packet_alloc( );
    d->frame = av_frame_alloc( );
    return d;
}

unsigned int radOggDecoderRate( radOggDecoder* d )          { return d->rate; }
unsigned int radOggDecoderChannels( radOggDecoder* d )      { return d->channels; }
unsigned int radOggDecoderTotalFrames( radOggDecoder* d )   { return d->totalFrames; }

// Decodes the next packet's worth of frames into pending. False at the end of the stream.
static bool DecodeMore( radOggDecoder* d )
{
    while ( true )
    {
        int r = avcodec_receive_frame( d->codec, d->frame );
        if ( r == 0 )
        {
            unsigned int maxOut = (unsigned int) swr_get_out_samples( d->swr, d->frame->nb_samples );
            unsigned int need = d->pendingFrames + maxOut;
            if ( need > d->pendingCapacity )
            {
                d->pendingCapacity = need * 2;
                d->pending = (short*) realloc( d->pending, d->pendingCapacity * d->channels * sizeof( short ) );
            }
            if ( d->pendingOffset > 0 )
            {
                memmove( d->pending, d->pending + d->pendingOffset * d->channels,
                         d->pendingFrames * d->channels * sizeof( short ) );
                d->pendingOffset = 0;
            }
            uint8_t* out = (uint8_t*) ( d->pending + d->pendingFrames * d->channels );
            int got = swr_convert( d->swr, &out, (int) maxOut, (const uint8_t**) d->frame->extended_data,
                                   d->frame->nb_samples );
            av_frame_unref( d->frame );
            if ( got > 0 )
            {
                d->pendingFrames += (unsigned int) got;
                return true;
            }
            continue;
        }
        if ( r == AVERROR_EOF )
        {
            return false;
        }
        // AVERROR(EAGAIN): feed the next packet
        if ( d->eof )
        {
            return false;
        }
        r = av_read_frame( d->fmt, d->packet );
        if ( r < 0 )
        {
            d->eof = true;
            avcodec_send_packet( d->codec, NULL );      // drain
            continue;
        }
        if ( d->packet->stream_index == d->stream )
        {
            avcodec_send_packet( d->codec, d->packet );
        }
        av_packet_unref( d->packet );
    }
}

unsigned int radOggDecoderRead( radOggDecoder* d, short* out, unsigned int frames )
{
    unsigned int done = 0;
    while ( done < frames )
    {
        if ( d->pendingFrames == 0 && !DecodeMore( d ) )
        {
            break;
        }
        unsigned int n = frames - done < d->pendingFrames ? frames - done : d->pendingFrames;
        if ( out != NULL )
        {
            memcpy( out + done * d->channels, d->pending + d->pendingOffset * d->channels, n * d->channels * sizeof( short ) );
        }
        d->pendingOffset += n;
        d->pendingFrames -= n;
        done += n;
    }
    return done;
}
