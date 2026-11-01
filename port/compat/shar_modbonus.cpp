/* Mod Launcher CustomBonusMissionSupport.ini (see shar_modbonus.h). */
#include "shar_modbonus.h"
#include "shar_mods.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace
{

const int MAX_ENTRIES = 512;

struct Entry
{
    char section[ 48 ];
    char key[ 48 ];
    char value[ 128 ];
};

Entry s_entries[ MAX_ENTRIES ];
int s_numEntries = 0;
bool s_loaded = false;
bool s_log = false;          // SHAR_MOD_LOG: values the mod replaces

void OnIni( const char* section, const char* key, const char* value, void* )
{
    if( key == NULL )
    {
        return;
    }
    if( s_numEntries == MAX_ENTRIES )
    {
        fprintf( stderr, "mods: CustomBonusMissionSupport.ini: more than %d keys, rest ignored\n", MAX_ENTRIES );
        s_numEntries++;
        return;
    }
    if( s_numEntries > MAX_ENTRIES )
    {
        return;
    }
    Entry* e = &s_entries[ s_numEntries++ ];
    snprintf( e->section, sizeof( e->section ), "%s", section );
    snprintf( e->key, sizeof( e->key ), "%s", key );
    snprintf( e->value, sizeof( e->value ), "%s", value );
}

void Load()
{
    if( s_loaded )
    {
        return;
    }
    s_loaded = true;
    s_log = getenv( "SHAR_MOD_LOG" ) != NULL;
    if( shar_ModHackRequired( "CustomBonusMissionSupport" ) )
    {
        shar_ModConfigIni( "CustomBonusMissionSupport.ini", OnIni, NULL, NULL );
        if( s_numEntries > MAX_ENTRIES )
        {
            s_numEntries = MAX_ENTRIES;
        }
    }
}

// the last value of [section] key, or NULL
const char* Find( const char* section, const char* key )
{
    Load();
    for( int i = s_numEntries - 1; i >= 0; i-- )
    {
        if( strcasecmp( s_entries[ i ].section, section ) == 0 && strcasecmp( s_entries[ i ].key, key ) == 0 )
        {
            return s_entries[ i ].value;
        }
    }
    return NULL;
}

// [<base>L<n>] key, else [<base>] key; blank values count as missing
const char* FindLevel( const char* base, int levelIndex, const char* key )
{
    char section[ 64 ];
    snprintf( section, sizeof( section ), "%sL%d", base, levelIndex + 1 );
    const char* value = Find( section, key );
    if( value == NULL || value[ 0 ] == '\0' )
    {
        value = Find( base, key );
    }
    return value != NULL && value[ 0 ] != '\0' ? value : NULL;
}

int MissionFlag( int levelIndex, const char* missionName, const char* key, int defaultValue )
{
    char section[ 64 ];
    snprintf( section, sizeof( section ), "L%d%s", levelIndex + 1, missionName );
    const char* value = Find( section, key );
    if( value == NULL || value[ 0 ] == '\0' )
    {
        return defaultValue;
    }
    int result = atoi( value ) != 0;
    if( s_log && result != defaultValue )
    {
        fprintf( stderr, "mods: [%s] %s=%d\n", section, key, result );
    }
    return result;
}

}

extern "C" int shar_ModBonusRaceIntroIndex( int levelIndex, const char* modelName )
{
    if( modelName == NULL )
    {
        return -1;
    }
    const char* value = FindLevel( "StreetRaceIntroDialogueMissionIndex", levelIndex, modelName );
    return value != NULL ? atoi( value ) : -1;
}

extern "C" int shar_ModBonusRaceIntroLastIndex( int defaultIndex )
{
    const char* value = Find( "Miscellaneous", "StreetRaceIntroDialogueMissionIndex" );
    return value != NULL && value[ 0 ] != '\0' ? atoi( value ) : defaultIndex;
}

extern "C" int shar_ModBonusResetPlayer( int levelIndex, const char* missionName, int defaultValue )
{
    return MissionFlag( levelIndex, missionName, "ResetPlayer", defaultValue );
}

extern "C" int shar_ModBonusForcePlayerInCar( int levelIndex, const char* missionName, int defaultValue )
{
    return MissionFlag( levelIndex, missionName, "ForcePlayerInCar", defaultValue );
}

extern "C" const char* shar_ModBonusStartBitmapAt( int levelIndex, int i, const char** hostName )
{
    Load();
    char levelSection[ 64 ];
    snprintf( levelSection, sizeof( levelSection ), "StartBitmapsL%d", levelIndex + 1 );
    // the level's section, then the global one; later keys win within each
    for( int pass = 0; pass < 2; pass++ )
    {
        const char* section = pass == 0 ? levelSection : "StartBitmaps";
        for( int e = s_numEntries - 1; e >= 0; e-- )
        {
            if( strcasecmp( s_entries[ e ].section, section ) == 0 && s_entries[ e ].value[ 0 ] != '\0' && i-- == 0 )
            {
                *hostName = s_entries[ e ].key;
                return s_entries[ e ].value;
            }
        }
    }
    return NULL;
}

extern "C" const char* shar_ModBonusEndBitmap( int levelIndex, const char* missionName )
{
    const char* value = FindLevel( "EndBitmaps", levelIndex, missionName );
    if( value == NULL )
    {
        value = FindLevel( "EndBitmaps", levelIndex, "Default" );
    }
    if( s_log && value != NULL )
    {
        fprintf( stderr, "mods: end bitmap for %s: %s\n", missionName, value );
    }
    return value;
}
