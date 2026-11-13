// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Mod Launcher CustomInteriorSupport.ini (see shar_modinteriors.h). */
#include "shar_modinteriors.h"
#include "shar_mods.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace
{

const int MAX_INTERIORS = 64;

SharModInterior s_interiors[ MAX_INTERIORS ];
int s_numInteriors = 0;
bool s_loaded = false;

char* Trim( char* s )
{
    while( *s == ' ' || *s == '\t' )
    {
        s++;
    }
    char* e = s + strlen( s );
    while( e > s && ( e[ -1 ] == ' ' || e[ -1 ] == '\t' || e[ -1 ] == '\r' ) )
    {
        *--e = '\0';
    }
    return s;
}

SharModInterior* NewInterior()
{
    if( s_numInteriors >= MAX_INTERIORS )
    {
        return NULL;
    }
    SharModInterior* interior = &s_interiors[ s_numInteriors++ ];
    memset( interior, 0, sizeof( *interior ) );
    for( int i = 0; i < 4; i++ )
    {
        interior->mirror[ i * 5 ] = 1.0f;
    }
    return interior;
}

// "MirrorZX" -> 8 (row Z, column X), else -1
int MirrorKey( const char* key )
{
    static const char AXES[] = "xyzw";
    if( strncasecmp( key, "Mirror", 6 ) != 0 || strlen( key ) != 8 )
    {
        return -1;
    }
    const char* row = strchr( AXES, key[ 6 ] | 0x20 );
    const char* column = strchr( AXES, key[ 7 ] | 0x20 );
    if( row == NULL || column == NULL )
    {
        return -1;
    }
    return static_cast< int >( row - AXES ) * 4 + static_cast< int >( column - AXES );
}

void SetKey( SharModInterior* interior, const char* key, const char* value )
{
    float real = static_cast< float >( atof( value ) );
    int number = atoi( value );
    int mirror;
    if( strcasecmp( key, "Name" ) == 0 ) snprintf( interior->name, sizeof( interior->name ), "%s", value );
    else if( strcasecmp( key, "Level" ) == 0 )
    {
        if( number >= 1 && number <= 7 ) interior->levels |= 1 << ( number - 1 );
    }
    else if( strcasecmp( key, "ShapeType" ) == 0 ) interior->isBox = strcasecmp( value, "Box" ) == 0;
    else if( strcasecmp( key, "X" ) == 0 ) interior->centre[ 0 ] = real;
    else if( strcasecmp( key, "Y" ) == 0 ) interior->centre[ 1 ] = real;
    else if( strcasecmp( key, "Z" ) == 0 ) interior->centre[ 2 ] = real;
    else if( strcasecmp( key, "Radius" ) == 0 ) interior->radius = real;
    else if( strcasecmp( key, "MinX" ) == 0 ) interior->min[ 0 ] = real;
    else if( strcasecmp( key, "MinY" ) == 0 ) interior->min[ 1 ] = real;
    else if( strcasecmp( key, "MinZ" ) == 0 ) interior->min[ 2 ] = real;
    else if( strcasecmp( key, "MaxX" ) == 0 ) interior->max[ 0 ] = real;
    else if( strcasecmp( key, "MaxY" ) == 0 ) interior->max[ 1 ] = real;
    else if( strcasecmp( key, "MaxZ" ) == 0 ) interior->max[ 2 ] = real;
    else if( ( mirror = MirrorKey( key ) ) >= 0 )
    {
        interior->mirror[ mirror ] = real;
        interior->hasMirror = 1;
    }
    else if( strcasecmp( key, "ResetAmbientCharacter" ) == 0 )
    {
        if( interior->numResets < SHAR_MODINTERIOR_MAX_RESETS )
        {
            snprintf( interior->resetCharacters[ interior->numResets++ ], 64, "%s", value );
        }
    }
    else if( strcasecmp( key, "AllowJumping" ) == 0 ) interior->allowJumping = number != 0;
    else if( strcasecmp( key, "AllowKicking" ) == 0 ) interior->allowKicking = number != 0;
    else if( strcasecmp( key, "AllowSprinting" ) == 0 ) interior->allowSprinting = number != 0;
}

void Load()
{
    s_loaded = true;
    if( !shar_ModHackRequired( "CustomInteriorSupport" ) )
    {
        return;
    }
    char* text = shar_ModConfig( "CustomInteriorSupport.ini" );
    if( text == NULL )
    {
        return;
    }

    char* start = text;
    if( (unsigned char) start[ 0 ] == 0xEF && (unsigned char) start[ 1 ] == 0xBB && (unsigned char) start[ 2 ] == 0xBF )
    {
        start += 3; // UTF-8 byte order mark
    }
    SharModInterior* interior = NULL;
    for( char* line = strtok( start, "\n" ); line != NULL; line = strtok( NULL, "\n" ) )
    {
        line = Trim( line );
        if( *line == '\0' || *line == ';' || *line == '#' )
        {
            continue;
        }
        if( *line == '[' )
        {
            char* end = strchr( line, ']' );
            if( end != NULL )
            {
                *end = '\0';
            }
            interior = strcasecmp( Trim( line + 1 ), "Interior" ) == 0 ? NewInterior() : NULL;
            continue;
        }
        char* eq = strchr( line, '=' );
        if( eq == NULL || interior == NULL )
        {
            continue;
        }
        *eq = '\0';
        SetKey( interior, Trim( line ), Trim( eq + 1 ) );
    }
    free( text );

    // drop nameless sections
    int kept = 0;
    for( int i = 0; i < s_numInteriors; i++ )
    {
        if( s_interiors[ i ].name[ 0 ] != '\0' )
        {
            s_interiors[ kept++ ] = s_interiors[ i ];
        }
    }
    s_numInteriors = kept;
    fprintf( stderr, "mods: CustomInteriorSupport: %d interiors\n", s_numInteriors );
}

void EnsureLoaded()
{
    if( !s_loaded )
    {
        Load();
    }
}

} // namespace

extern "C" int shar_ModInteriorCount( void )
{
    EnsureLoaded();
    return s_numInteriors;
}

extern "C" const SharModInterior* shar_ModInteriorAt( int i )
{
    EnsureLoaded();
    return ( i >= 0 && i < s_numInteriors ) ? &s_interiors[ i ] : NULL;
}

extern "C" const SharModInterior* shar_ModInterior( const char* name )
{
    EnsureLoaded();
    for( int i = 0; i < s_numInteriors; i++ )
    {
        if( strcasecmp( s_interiors[ i ].name, name ) == 0 )
        {
            return &s_interiors[ i ];
        }
    }
    return NULL;
}

extern "C" int shar_ModInteriorContains( const SharModInterior* interior, float x, float y, float z )
{
    if( interior->isBox )
    {
        return x >= interior->min[ 0 ] && x <= interior->max[ 0 ] && y >= interior->min[ 1 ] &&
               y <= interior->max[ 1 ] && z >= interior->min[ 2 ] && z <= interior->max[ 2 ];
    }
    float dx = x - interior->centre[ 0 ], dy = y - interior->centre[ 1 ], dz = z - interior->centre[ 2 ];
    return dx * dx + dy * dy + dz * dz < interior->radius * interior->radius;
}
