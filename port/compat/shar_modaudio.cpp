// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Mod Launcher CustomAudioSupport.xml (see shar_modaudio.h). */
#include "shar_modaudio.h"
#include "shar_mods.h"
#include "shar_modxml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace
{

const int NUM_LEVELS = 7;
const int NUM_MISSIONS = 8;
const int MAX_INTERIORS = 64;
const int UNSET = -1;

struct BirdSound
{
    bool set;
    char name[ 64 ];        // empty: none
};

struct Interior
{
    char name[ 64 ];
    int musicEvent;
};

int s_levelAmbience[ NUM_LEVELS ];
int s_missionAmbience[ NUM_LEVELS ][ NUM_MISSIONS ];
BirdSound s_levelBird[ NUM_LEVELS ];
BirdSound s_missionBird[ NUM_LEVELS ][ NUM_MISSIONS ];
Interior s_interiors[ MAX_INTERIORS ];
int s_numInteriors = 0;
unsigned int s_samplingRate = 0;
char s_forcedMusic[ 128 ] = "";
bool s_loaded = false;
bool s_log = false;          // SHAR_MOD_LOG: values the mod replaces

// Index="0-based" or Number="1-based"; -1 when neither is given
int ReadIndex( const SharXmlElement* e )
{
    char value[ 16 ];
    if( shar_XmlAttr( e, "Index", value, sizeof( value ) ) )
    {
        return atoi( value );
    }
    if( shar_XmlAttr( e, "Number", value, sizeof( value ) ) )
    {
        return atoi( value ) - 1;
    }
    return -1;
}

void ReadAmbience( const SharXmlElement* e, int* out )
{
    char value[ 16 ];
    if( shar_XmlAttr( e, "StartAmbienceEventIndex", value, sizeof( value ) ) && value[ 0 ] != '\0' )
    {
        *out = atoi( value );
    }
}

void ReadBird( const SharXmlElement* e, BirdSound* out )
{
    char value[ 64 ];
    if( shar_XmlAttr( e, "BirdSound", value, sizeof( value ) ) )
    {
        out->set = true;
        snprintf( out->name, sizeof( out->name ), "%s", strcasecmp( value, "null" ) == 0 ? "" : value );
    }
}

void Load()
{
    s_loaded = true;
    s_log = getenv( "SHAR_MOD_LOG" ) != NULL;
    for( int l = 0; l < NUM_LEVELS; l++ )
    {
        s_levelAmbience[ l ] = UNSET;
        for( int m = 0; m < NUM_MISSIONS; m++ )
        {
            s_missionAmbience[ l ][ m ] = UNSET;
        }
    }
    if( !shar_ModHackRequired( "CustomAudioSupport" ) )
    {
        return;
    }
    char* text = shar_ModConfig( "CustomAudioSupport.xml" );
    if( text == NULL )
    {
        return;
    }
    SharXmlReader reader = { text, 0 };
    SharXmlElement e;
    int level = -1;
    int levelDepth = -1;
    while( shar_XmlNext( &reader, &e ) )
    {
        if( e.depth <= levelDepth )
        {
            level = -1;
            levelDepth = -1;
        }
        char value[ 128 ];
        if( shar_XmlIs( &e, "CustomAudioSupport" ) )
        {
            if( shar_XmlAttr( &e, "MusicSamplingRate", value, sizeof( value ) ) && atoi( value ) > 0 )
            {
                s_samplingRate = static_cast< unsigned int >( atoi( value ) );
            }
            if( shar_XmlAttr( &e, "ForcedMusicRMSLevel", value, sizeof( value ) ) && value[ 0 ] != '\0' )
            {
                char* end;
                long number = strtol( value, &end, 10 );
                if( *end == '\0' )
                {
                    if( number >= 1 && number <= NUM_LEVELS )
                    {
                        snprintf( s_forcedMusic, sizeof( s_forcedMusic ), "sound\\music\\L%ld_music.rms", number );
                    }
                }
                else
                {
                    snprintf( s_forcedMusic, sizeof( s_forcedMusic ), "%s", value );
                    for( char* p = s_forcedMusic; *p != '\0'; p++ )
                    {
                        if( *p == '/' )
                        {
                            *p = '\\';
                        }
                    }
                }
            }
        }
        else if( shar_XmlIs( &e, "Interior" ) )
        {
            if( s_numInteriors < MAX_INTERIORS && shar_XmlAttr( &e, "Name", value, sizeof( value ) )
                && value[ 0 ] != '\0' )
            {
                Interior& interior = s_interiors[ s_numInteriors ];
                snprintf( interior.name, sizeof( interior.name ), "%s", value );
                if( shar_XmlAttr( &e, "MusicEventIndex", value, sizeof( value ) ) && value[ 0 ] != '\0' )
                {
                    interior.musicEvent = atoi( value );
                    s_numInteriors++;
                }
            }
        }
        else if( shar_XmlIs( &e, "Level" ) )
        {
            level = ReadIndex( &e );
            if( level < 0 || level >= NUM_LEVELS )
            {
                level = -1;
                continue;
            }
            levelDepth = e.depth;
            ReadAmbience( &e, &s_levelAmbience[ level ] );
            ReadBird( &e, &s_levelBird[ level ] );
        }
        else if( shar_XmlIs( &e, "Mission" ) && level >= 0 )
        {
            int mission = ReadIndex( &e );
            if( mission >= 0 && mission < NUM_MISSIONS )
            {
                ReadAmbience( &e, &s_missionAmbience[ level ][ mission ] );
                ReadBird( &e, &s_missionBird[ level ][ mission ] );
            }
        }
    }
    free( text );
    fprintf( stderr, "mods: CustomAudioSupport: %d interiors, music %u Hz%s%s\n", s_numInteriors,
             s_samplingRate != 0 ? s_samplingRate : 24000, s_forcedMusic[ 0 ] != '\0' ? ", always " : "",
             s_forcedMusic );
}

void EnsureLoaded()
{
    if( !s_loaded )
    {
        Load();
    }
}

bool InRange( int levelIndex, int missionIndex )
{
    return levelIndex >= 0 && levelIndex < NUM_LEVELS && missionIndex >= 0 && missionIndex < NUM_MISSIONS;
}

} // namespace

extern "C" int shar_ModAudioMissionAmbience( int levelIndex, int missionIndex, int defaultEvent )
{
    EnsureLoaded();
    if( InRange( levelIndex, missionIndex ) && s_missionAmbience[ levelIndex ][ missionIndex ] != UNSET )
    {
        static int logged = -1;
        if( s_log && logged != levelIndex * NUM_MISSIONS + missionIndex )
        {
            logged = levelIndex * NUM_MISSIONS + missionIndex;
            fprintf( stderr, "mods: CustomAudioSupport: L%d M%d ambience %d (game's %d)\n", levelIndex + 1,
                     missionIndex, s_missionAmbience[ levelIndex ][ missionIndex ], defaultEvent );
        }
        return s_missionAmbience[ levelIndex ][ missionIndex ];
    }
    return defaultEvent;
}

extern "C" int shar_ModAudioLevelAmbience( int levelIndex, int defaultEvent )
{
    EnsureLoaded();
    if( levelIndex >= 0 && levelIndex < NUM_LEVELS && s_levelAmbience[ levelIndex ] != UNSET )
    {
        static int logged = -1;
        if( s_log && logged != levelIndex )
        {
            logged = levelIndex;
            fprintf( stderr, "mods: CustomAudioSupport: L%d exterior ambience %d (game's %d)\n", levelIndex + 1,
                     s_levelAmbience[ levelIndex ], defaultEvent );
        }
        return s_levelAmbience[ levelIndex ];
    }
    return defaultEvent;
}

extern "C" int shar_ModAudioInteriorCount( void )
{
    EnsureLoaded();
    return s_numInteriors;
}

extern "C" const char* shar_ModAudioInterior( int i, int* musicEvent )
{
    EnsureLoaded();
    if( i < 0 || i >= s_numInteriors )
    {
        return NULL;
    }
    *musicEvent = s_interiors[ i ].musicEvent;
    return s_interiors[ i ].name;
}

extern "C" const char* shar_ModAudioBirdSound( int levelIndex, int missionIndex, const char* defaultName )
{
    EnsureLoaded();
    const BirdSound* bird = NULL;
    if( InRange( levelIndex, missionIndex ) && s_missionBird[ levelIndex ][ missionIndex ].set )
    {
        bird = &s_missionBird[ levelIndex ][ missionIndex ];
    }
    else if( levelIndex >= 0 && levelIndex < NUM_LEVELS && s_levelBird[ levelIndex ].set )
    {
        bird = &s_levelBird[ levelIndex ];
    }
    if( bird == NULL )
    {
        return defaultName;
    }
    if( s_log )
    {
        fprintf( stderr, "mods: CustomAudioSupport: bird sound %s\n", bird->name[ 0 ] != '\0' ? bird->name : "none" );
    }
    return bird->name[ 0 ] != '\0' ? bird->name : NULL;
}

extern "C" unsigned int shar_ModAudioMusicSamplingRate( unsigned int defaultRate )
{
    EnsureLoaded();
    return s_samplingRate != 0 ? s_samplingRate : defaultRate;
}

extern "C" const char* shar_ModAudioForcedMusicFile( void )
{
    EnsureLoaded();
    return s_forcedMusic[ 0 ] != '\0' ? s_forcedMusic : NULL;
}
