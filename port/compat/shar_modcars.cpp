// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Mod Launcher CustomCarSupport.ini (see shar_modcars.h). */
#include "shar_modcars.h"
#include "shar_mods.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace
{

const int MAX_CARS = 128;
const int MAX_FAKE_WHEELS = 64;

SharModCar s_cars[ MAX_CARS ];
int s_numCars = 0;
SharModFakeWheel s_fakeWheels[ MAX_FAKE_WHEELS ];
int s_numFakeWheels = 0;
// the FakeWheel= names of each car, resolved once the whole file is read
char s_fakeWheelNames[ MAX_CARS ][ SHAR_MODCAR_MAX_FAKE_WHEELS ][ 32 ];
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

SharModCar* NewCar( const char* name )
{
    if( s_numCars >= MAX_CARS )
    {
        return NULL;
    }
    SharModCar* car = &s_cars[ s_numCars++ ];
    memset( car, 0, sizeof( *car ) );
    snprintf( car->name, sizeof( car->name ), "%s", name );
    car->index = -1;
    car->noHusk = car->noSkidMarks = car->noFrontSkidMarks = car->shadowVisible = car->abductable =
        car->recolourable = car->invincible = car->noBumperCam = car->rearWheelSparks = car->isHusk = -1;
    car->previewScale = car->frontSteering = car->rearSteering = car->frontSkidWidth = car->rearSkidWidth =
        SHAR_MODCAR_UNSET;
    for( int i = 0; i < 4; i++ )
    {
        car->wheelSteering[ i ] = car->wheelSkidWidth[ i ] = SHAR_MODCAR_UNSET;
    }
    return car;
}

SharModFakeWheel* NewFakeWheel()
{
    if( s_numFakeWheels >= MAX_FAKE_WHEELS )
    {
        return NULL;
    }
    SharModFakeWheel* wheel = &s_fakeWheels[ s_numFakeWheels++ ];
    memset( wheel, 0, sizeof( *wheel ) );
    wheel->skidMarkWidthMultiplier = 1.0f;
    return wheel;
}

// "Wheel2SteeringVisualMultiplier" -> 2 (with the suffix given), else -1
int WheelKey( const char* key, const char* suffix )
{
    if( strncasecmp( key, "Wheel", 5 ) != 0 || key[ 5 ] < '0' || key[ 5 ] > '3' || strcasecmp( key + 6, suffix ) != 0 )
    {
        return -1;
    }
    return key[ 5 ] - '0';
}

int WheelBit( const char* value )
{
    int wheel = atoi( value );
    return ( wheel >= 0 && wheel <= 3 ) ? 1 << wheel : 0;
}

void SetCarKey( SharModCar* car, int carIndex, const char* key, const char* value )
{
    int number = atoi( value );
    float real = static_cast< float >( atof( value ) );
    int wheel;
    if( strcasecmp( key, "Index" ) == 0 ) car->index = number;
    else if( strcasecmp( key, "NoHusk" ) == 0 ) car->noHusk = number != 0;
    else if( strcasecmp( key, "NoSkidMarks" ) == 0 ) car->noSkidMarks = number != 0;
    else if( strcasecmp( key, "NoFrontSkidMarks" ) == 0 ) car->noFrontSkidMarks = number != 0;
    else if( strcasecmp( key, "ShadowVisible" ) == 0 ) car->shadowVisible = number != 0;
    else if( strcasecmp( key, "Abductable" ) == 0 ) car->abductable = number != 0;
    else if( strcasecmp( key, "Recolourable" ) == 0 ) car->recolourable = number != 0;
    else if( strcasecmp( key, "Invincible" ) == 0 ) car->invincible = number != 0;
    else if( strcasecmp( key, "NoBumperCam" ) == 0 ) car->noBumperCam = number != 0;
    else if( strcasecmp( key, "RearWheelSparks" ) == 0 || strcasecmp( key, "BackWheelSparks" ) == 0 ) car->rearWheelSparks = number != 0;
    else if( strcasecmp( key, "IsHusk" ) == 0 ) car->isHusk = number != 0;
    else if( strcasecmp( key, "Husk" ) == 0 ) snprintf( car->husk, sizeof( car->husk ), "%s", value );
    else if( strcasecmp( key, "PreviewScale" ) == 0 ) car->previewScale = real;
    else if( strcasecmp( key, "FrontSteeringVisualMultiplier" ) == 0 ) car->frontSteering = real;
    else if( strcasecmp( key, "RearSteeringVisualMultiplier" ) == 0 ) car->rearSteering = real;
    else if( ( wheel = WheelKey( key, "SteeringVisualMultiplier" ) ) >= 0 ) car->wheelSteering[ wheel ] = real;
    else if( strcasecmp( key, "FrontSkidMarkWidthMultiplier" ) == 0 ) car->frontSkidWidth = real;
    else if( strcasecmp( key, "RearSkidMarkWidthMultiplier" ) == 0 ) car->rearSkidWidth = real;
    else if( ( wheel = WheelKey( key, "SkidMarkWidthMultiplier" ) ) >= 0 ) car->wheelSkidWidth[ wheel ] = real;
    else if( strcasecmp( key, "FakeWheel" ) == 0 && car->numFakeWheels < SHAR_MODCAR_MAX_FAKE_WHEELS )
    {
        snprintf( s_fakeWheelNames[ carIndex ][ car->numFakeWheels++ ], 32, "%s", value );
    }
}

void SetFakeWheelKey( SharModFakeWheel* wheel, const char* key, const char* value )
{
    if( strcasecmp( key, "Name" ) == 0 ) snprintf( wheel->name, sizeof( wheel->name ), "%s", value );
    else if( strcasecmp( key, "Joint" ) == 0 ) snprintf( wheel->joint, sizeof( wheel->joint ), "%s", value );
    else if( strcasecmp( key, "CopyRotation" ) == 0 ) wheel->copyRotation |= WheelBit( value );
    else if( strcasecmp( key, "CopySuspension" ) == 0 ) wheel->copySuspension |= WheelBit( value );
    else if( strcasecmp( key, "CopySkidMarks" ) == 0 ) wheel->copySkidMarks |= WheelBit( value );
    else if( strcasecmp( key, "SteeringMultiplier" ) == 0 ) wheel->steeringMultiplier = static_cast< float >( atof( value ) );
    else if( strcasecmp( key, "SkidMarkWidthMultiplier" ) == 0 ) wheel->skidMarkWidthMultiplier = static_cast< float >( atof( value ) );
}

void Load()
{
    s_loaded = true;
    char* text = shar_ModConfig( "CustomCarSupport.ini" );
    if( text == NULL )
    {
        return;
    }

    char* start = text;
    if( (unsigned char) start[ 0 ] == 0xEF && (unsigned char) start[ 1 ] == 0xBB && (unsigned char) start[ 2 ] == 0xBF )
    {
        start += 3; // UTF-8 byte order mark
    }
    // a [Car] section gets its name from Name=, which may follow other keys
    SharModCar* car = NULL;
    SharModFakeWheel* fakeWheel = NULL;
    bool inCar = false;
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
            char* section = Trim( line + 1 );
            car = NULL;
            fakeWheel = NULL;
            inCar = false;
            if( strcasecmp( section, "Car" ) == 0 )
            {
                inCar = true;
                car = NewCar( "" );
            }
            else if( strcasecmp( section, "FakeWheel" ) == 0 )
            {
                fakeWheel = NewFakeWheel();
            }
            else
            {
                car = NewCar( section ); // the old format: the section is the car's name
            }
            continue;
        }
        char* eq = strchr( line, '=' );
        if( eq == NULL )
        {
            continue;
        }
        *eq = '\0';
        char* key = Trim( line );
        char* value = Trim( eq + 1 );
        if( car != NULL )
        {
            if( inCar && strcasecmp( key, "Name" ) == 0 )
            {
                snprintf( car->name, sizeof( car->name ), "%s", value );
            }
            else
            {
                SetCarKey( car, static_cast< int >( car - s_cars ), key, value );
            }
        }
        else if( fakeWheel != NULL )
        {
            SetFakeWheelKey( fakeWheel, key, value );
        }
    }
    free( text );

    for( int i = 0; i < s_numCars; i++ )
    {
        SharModCar* c = &s_cars[ i ];
        int resolved = 0;
        for( int j = 0; j < c->numFakeWheels; j++ )
        {
            for( int k = 0; k < s_numFakeWheels; k++ )
            {
                if( strcasecmp( s_fakeWheels[ k ].name, s_fakeWheelNames[ i ][ j ] ) == 0 && s_fakeWheels[ k ].joint[ 0 ] != '\0' )
                {
                    c->fakeWheels[ resolved++ ] = &s_fakeWheels[ k ];
                    break;
                }
            }
        }
        c->numFakeWheels = resolved;
    }

    if( getenv( "SHAR_MOD_LOG" ) != NULL )
    {
        for( int i = 0; i < s_numCars; i++ )
        {
            const SharModCar* c = &s_cars[ i ];
            fprintf( stderr, "mods: CustomCarSupport %s: index %d, nohusk %d, noskid %d/%d, shadow %d, abductable %d, "
                     "recolourable %d, invincible %d, nobumpercam %d, sparks %d, ishusk %d, husk [%s], %d fake wheel(s)\n",
                     c->name, c->index, c->noHusk, c->noSkidMarks, c->noFrontSkidMarks, c->shadowVisible, c->abductable,
                     c->recolourable, c->invincible, c->noBumperCam, c->rearWheelSparks, c->isHusk, c->husk,
                     c->numFakeWheels );
        }
    }
}

}

const SharModCar* shar_ModCar( const char* name )
{
    if( !s_loaded )
    {
        Load();
    }
    if( name == NULL )
    {
        return NULL;
    }
    for( int i = 0; i < s_numCars; i++ )
    {
        if( s_cars[ i ].name[ 0 ] != '\0' && strcasecmp( s_cars[ i ].name, name ) == 0 )
        {
            return &s_cars[ i ];
        }
    }
    return NULL;
}

int shar_ModCarCount( void )
{
    if( !s_loaded )
    {
        Load();
    }
    return s_numCars;
}

const SharModCar* shar_ModCarAt( int i )
{
    return ( i >= 0 && i < shar_ModCarCount() ) ? &s_cars[ i ] : NULL;
}

float shar_ModCarSteering( const SharModCar* car, int wheel )
{
    bool front = wheel >= 2;
    if( car != NULL && wheel >= 0 && wheel < 4 )
    {
        if( car->wheelSteering[ wheel ] != SHAR_MODCAR_UNSET )
        {
            return car->wheelSteering[ wheel ];
        }
        float side = front ? car->frontSteering : car->rearSteering;
        if( side != SHAR_MODCAR_UNSET )
        {
            return side;
        }
    }
    return front ? 1.0f : 0.0f;
}

float shar_ModCarSkidWidth( const SharModCar* car, int wheel )
{
    if( car != NULL && wheel >= 0 && wheel < 4 )
    {
        if( car->wheelSkidWidth[ wheel ] != SHAR_MODCAR_UNSET )
        {
            return car->wheelSkidWidth[ wheel ];
        }
        float side = wheel >= 2 ? car->frontSkidWidth : car->rearSkidWidth;
        if( side != SHAR_MODCAR_UNSET )
        {
            return side;
        }
    }
    return 1.0f;
}
