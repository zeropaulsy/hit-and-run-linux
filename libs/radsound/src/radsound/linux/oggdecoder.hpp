//=============================================================================
// Linux port: Ogg Vorbis and FLAC decoding for mods (the Mod Launcher's OggVorbisSupport and FLACSupport: .ogg
// and .flac files in place of .rsd ones, see rsdfiledatasource.cpp). FFmpeg over an in-memory copy of the file,
// 16-bit interleaved PCM out at the file's own rate and channel count. malloc only.
//=============================================================================

#ifndef RADSOUND_LINUX_OGGDECODER_HPP
#define RADSOUND_LINUX_OGGDECODER_HPP

struct radOggDecoder;

// data is malloc'd and owned by the decoder from here on (also on failure). NULL if it cannot be decoded.
radOggDecoder* radOggDecoderOpen( unsigned char* data, unsigned int size, const char* nameForErrors );
unsigned int radOggDecoderRate( radOggDecoder* dec );
unsigned int radOggDecoderChannels( radOggDecoder* dec );
unsigned int radOggDecoderTotalFrames( radOggDecoder* dec );   // from the last page's granule position
// Decodes up to frames frames into out (NULL: decode and drop); returns how many it produced (fewer at the end).
unsigned int radOggDecoderRead( radOggDecoder* dec, short* out, unsigned int frames );
void radOggDecoderClose( radOggDecoder* dec );

inline bool radOggIsOgg( const void* header )
{
    const unsigned char* h = (const unsigned char*) header;
    return h[ 0 ] == 'O' && h[ 1 ] == 'g' && h[ 2 ] == 'g' && h[ 3 ] == 'S';
}

// FLAC, or a file with an ID3v2 tag first (FLAC files may have one; FFmpeg then probes the format)
inline bool radOggIsFlac( const void* header )
{
    const unsigned char* h = (const unsigned char*) header;
    return ( h[ 0 ] == 'f' && h[ 1 ] == 'L' && h[ 2 ] == 'a' && h[ 3 ] == 'C' ) ||
           ( h[ 0 ] == 'I' && h[ 1 ] == 'D' && h[ 2 ] == '3' );
}

#endif
