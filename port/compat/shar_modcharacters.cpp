/* Mod Launcher CustomCharacterSupport.ini (see shar_modcharacters.h). */
#include "shar_modcharacters.h"
#include "shar_mods.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace
{

const int MAX_CHARACTERS = 128;

SharModCharacter s_characters[ MAX_CHARACTERS ];
int s_numCharacters = 0;
bool s_loaded = false;

SharModCharacter* Get( const char* name, bool create )
{
    for( int i = 0; i < s_numCharacters; i++ )
    {
        if( strcasecmp( s_characters[ i ].name, name ) == 0 )
        {
            return &s_characters[ i ];
        }
    }
    if( !create || s_numCharacters == MAX_CHARACTERS )
    {
        return NULL;
    }
    SharModCharacter* c = &s_characters[ s_numCharacters++ ];
    memset( c, 0, sizeof( *c ) );
    snprintf( c->name, sizeof( c->name ), "%s", name );
    c->isLisa = -1;
    c->isMarge = -1;
    return c;
}

void OnIni( const char* section, const char* key, const char* value, void* )
{
    if( key == NULL || value[ 0 ] == '\0' )
    {
        return;
    }
    SharModCharacter* c = Get( section, true );
    if( c == NULL )
    {
        return;
    }
    if( strcasecmp( key, "BlendModeSupport" ) == 0 )
    {
        c->blendModeSupport = atoi( value ) != 0;
    }
    else if( strcasecmp( key, "IsLisa" ) == 0 )
    {
        c->isLisa = atoi( value ) != 0;
    }
    else if( strcasecmp( key, "IsMarge" ) == 0 )
    {
        c->isMarge = atoi( value ) != 0;
    }
    else if( strcasecmp( key, "IdleAnimationWeight" ) == 0 && c->numIdleWeights < SHAR_MODCHAR_MAX_IDLES )
    {
        float weight = static_cast< float >( atof( value ) );
        c->idleWeights[ c->numIdleWeights++ ] = weight > 0.0f ? weight : 0.0f;
    }
}

}

extern "C" const SharModCharacter* shar_ModCharacter( const char* modelName )
{
    if( !s_loaded )
    {
        s_loaded = true;
        if( shar_ModHackRequired( "CustomCharacterSupport" ) )
        {
            shar_ModConfigIni( "CustomCharacterSupport.ini", OnIni, NULL, NULL );
        }
    }
    return modelName != NULL ? Get( modelName, false ) : NULL;
}
