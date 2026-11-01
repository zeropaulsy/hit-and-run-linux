//=============================================================================
// Linux port: the Mod Launcher's VideoTextureSupport. A shader texture named by a path in the mods' virtual
// file system ("/GameData/movies/x.bik", shader.cpp) plays that Bink file, looped and silent, into a texture
// of its size (any size: a dynamic texture, gltex.cpp). One texture per path, shared by every shader naming it;
// RenderManager advances them once a frame and drops those no shader holds any more.
//=============================================================================

#include "pch.hpp"
#include <bink.h>
#include <p3d/texture.hpp>
#include <pddi/pddi.hpp>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace
{

struct VideoTexture
{
    char* path;
    HBINK bink;
    tTexture* texture;
};

const int MAX_VIDEO_TEXTURES = 32;
VideoTexture s_Videos[ MAX_VIDEO_TEXTURES ];
int s_NumVideos = 0;
char* s_Failed[ MAX_VIDEO_TEXTURES ];                   // paths that did not play: tried and reported once
int s_NumFailed = 0;
pthread_mutex_t s_Lock = PTHREAD_MUTEX_INITIALIZER;    // shaders load on the loading thread

HBINK OpenVideo( const char* path )
{
    HBINK bink = BinkOpen( path, BINKLINUXNOAUDIO );
    if ( bink != NULL )
    {
        BinkPause( bink, 0 );
    }
    return bink;
}

void RemoveVideo( int i )
{
    VideoTexture& v = s_Videos[ i ];
    if ( v.bink != NULL )
    {
        BinkClose( v.bink );
    }
    v.texture->Release( );
    free( v.path );
    s_Videos[ i ] = s_Videos[ --s_NumVideos ];
}

// The decoded frame into the texture (Pure3D locks bottom-up: a negative pitch from the last row, as
// binkmovieplayer.cpp copies)
void UploadFrame( VideoTexture& v )
{
    pddiLockInfo* lock = v.texture->Lock( 0 );
    char* dest = (char*) lock->bits;
    if ( lock->pitch < 0 )
    {
        dest += ( lock->height - 1 ) * lock->pitch;
    }
    BinkCopyToBufferRect( v.bink, dest, lock->pitch, lock->height, 0, 0, 0, 0, v.bink->Width, v.bink->Height,
                          BINKSURFACE32 | BINKCOPYALL );
    v.texture->Unlock( 0 );
}

} // namespace

// Sets shader's param to the video texture of path; false (and a log line) when the video cannot be played
bool radMovieSetVideoTexture( pddiShader* shader, unsigned param, const char* path )
{
    pthread_mutex_lock( &s_Lock );
    int i = 0;
    while ( i < s_NumVideos && strcasecmp( s_Videos[ i ].path, path ) != 0 )
    {
        i++;
    }
    for ( int f = 0; i == s_NumVideos && f < s_NumFailed; f++ )
    {
        if ( strcasecmp( s_Failed[ f ], path ) == 0 )
        {
            pthread_mutex_unlock( &s_Lock );
            return false;
        }
    }
    if ( i == s_NumVideos )
    {
        HBINK bink = s_NumVideos < MAX_VIDEO_TEXTURES ? OpenVideo( path ) : NULL;
        tTexture* texture = NULL;
        if ( bink != NULL )
        {
            texture = new tTexture( );
            texture->AddRef( );
            if ( !texture->Create( bink->Width, bink->Height, 32, 8, 0, PDDI_TEXTYPE_RGB, PDDI_USAGE_DYNAMIC ) )
            {
                texture->Release( );
                texture = NULL;
            }
        }
        if ( texture == NULL )
        {
            fprintf( stderr, "mods: video texture %s: %s\n", path,
                     s_NumVideos == MAX_VIDEO_TEXTURES ? "too many" :
                     bink == NULL ? BinkGetError( ) : "no texture of its size" );
            if ( bink != NULL )
            {
                BinkClose( bink );
            }
            if ( s_NumFailed < MAX_VIDEO_TEXTURES )
            {
                s_Failed[ s_NumFailed++ ] = strdup( path );
            }
            pthread_mutex_unlock( &s_Lock );
            return false;
        }
        // transparent until the first frame
        pddiLockInfo* lock = texture->Lock( 0 );
        char* row = (char*) lock->bits;
        for ( int y = 0; y < lock->height; y++, row += lock->pitch )
        {
            memset( row, 0, lock->width * 4 );
        }
        texture->Unlock( 0 );
        VideoTexture v = { strdup( path ), bink, texture };
        s_Videos[ s_NumVideos++ ] = v;
        fprintf( stderr, "mods: video texture %s (%ux%u, %u frames)\n", path, bink->Width, bink->Height,
                 bink->Frames );
    }
    shader->SetTexture( param, s_Videos[ i ].texture->GetTexture( ) );
    pthread_mutex_unlock( &s_Lock );
    return true;
}

// Once a frame, on the render thread: the next frame of each video that is due; videos restart at their end
void radMovieUpdateVideoTextures( void )
{
    pthread_mutex_lock( &s_Lock );
    for ( int i = s_NumVideos - 1; i >= 0; i-- )
    {
        VideoTexture& v = s_Videos[ i ];
        if ( v.texture->GetTexture( )->GetRefCount( ) <= 1 )
        {
            RemoveVideo( i );           // only the texture itself holds it: its shaders are gone
            continue;
        }
        if ( v.bink == NULL || BinkWait( v.bink ) != 0 )
        {
            continue;
        }
        if ( v.bink->FrameNum > v.bink->Frames || BinkDoFrame( v.bink ) != 0 )
        {
            bool started = v.bink->FrameNum > 1;
            BinkClose( v.bink );
            v.bink = started ? OpenVideo( v.path ) : NULL;     // NULL: the last frame stays
            continue;
        }
        UploadFrame( v );
        BinkNextFrame( v.bink );
    }
    pthread_mutex_unlock( &s_Lock );
}
