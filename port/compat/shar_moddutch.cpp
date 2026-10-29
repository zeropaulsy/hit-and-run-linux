/* Mod Launcher CustomDutchAngleNPCCameraTilt.xml (see shar_moddutch.h). */
#include "shar_moddutch.h"
#include "shar_mods.h"
#include "shar_modxml.h"

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace
{

const int NUM_LEVELS = 7;
const int MAX_MISSIONS = 16;

struct Setting
{
    int tilt;                   // -1: inherited
    bool hasDegrees;            // false: inherited
    float degrees;
};

bool s_required = false;
float s_globalDegrees = 0.176f * 180.0f / 3.14159265f;    // the game's CREEPY_TWIST_WALKER
Setting s_levels[ NUM_LEVELS ];
Setting s_missions[ NUM_LEVELS ][ MAX_MISSIONS ];
pthread_once_t s_once = PTHREAD_ONCE_INIT;

int IndexOrNumber( const SharXmlElement* e )
{
    char value[ 16 ];
    if( shar_XmlAttr( e, "Index", value, sizeof( value ) ) && value[ 0 ] != '\0' )
    {
        return atoi( value );
    }
    if( shar_XmlAttr( e, "Number", value, sizeof( value ) ) && value[ 0 ] != '\0' )
    {
        return atoi( value ) - 1;
    }
    return -1;
}

void ReadSetting( const SharXmlElement* e, Setting* s )
{
    char value[ 16 ];
    if( shar_XmlAttr( e, "DutchCameraTilt", value, sizeof( value ) ) && value[ 0 ] != '\0' )
    {
        s->tilt = ( strcasecmp( value, "true" ) == 0 || atoi( value ) != 0 ) ? 1 : 0;
    }
    if( shar_XmlAttr( e, "TiltAngle", value, sizeof( value ) ) && value[ 0 ] != '\0' )
    {
        s->degrees = static_cast< float >( atof( value ) );
        s->hasDegrees = true;
    }
}

void Load()
{
    for( int l = 0; l < NUM_LEVELS; l++ )
    {
        s_levels[ l ].tilt = l == NUM_LEVELS - 1 ? 1 : 0;   // the game's: level 7
        s_levels[ l ].hasDegrees = false;
        for( int m = 0; m < MAX_MISSIONS; m++ )
        {
            s_missions[ l ][ m ].tilt = -1;
            s_missions[ l ][ m ].hasDegrees = false;
        }
    }
    s_required = shar_ModHackRequired( "CustomDutchAngleNPCCameraTilt" ) != 0;
    if( !s_required )
    {
        return;
    }
    char* text = shar_ModConfig( "CustomDutchAngleNPCCameraTilt.xml" );
    if( text == NULL )
    {
        return;
    }
    SharXmlReader reader = { text, 0 };
    SharXmlElement e;
    int level = -1, levelDepth = -1;
    while( shar_XmlNext( &reader, &e ) )
    {
        if( e.depth <= levelDepth )
        {
            level = -1;
            levelDepth = -1;
        }
        if( shar_XmlIs( &e, "CustomDutchAngleNPCCameraTilt" ) )
        {
            char value[ 16 ];
            if( shar_XmlAttr( &e, "TiltAngle", value, sizeof( value ) ) && value[ 0 ] != '\0' )
            {
                s_globalDegrees = static_cast< float >( atof( value ) );
            }
        }
        else if( shar_XmlIs( &e, "Level" ) )
        {
            level = IndexOrNumber( &e );
            levelDepth = e.depth;
            if( level >= 0 && level < NUM_LEVELS )
            {
                ReadSetting( &e, &s_levels[ level ] );
            }
            else
            {
                level = -1;
            }
        }
        else if( shar_XmlIs( &e, "Mission" ) && level >= 0 )
        {
            int mission = IndexOrNumber( &e );
            if( mission >= 0 && mission < MAX_MISSIONS )
            {
                ReadSetting( &e, &s_missions[ level ][ mission ] );
            }
        }
    }
    free( text );
}

}

extern "C" int shar_ModDutchTilt( int level, int mission, float* degrees )
{
    pthread_once( &s_once, Load );
    if( !s_required )
    {
        return -1;
    }
    if( level < 0 || level >= NUM_LEVELS )
    {
        return 0;
    }
    const Setting* l = &s_levels[ level ];
    const Setting* m = mission >= 0 && mission < MAX_MISSIONS ? &s_missions[ level ][ mission ] : NULL;
    int tilt = m != NULL && m->tilt >= 0 ? m->tilt : l->tilt;
    *degrees = m != NULL && m->hasDegrees ? m->degrees : l->hasDegrees ? l->degrees : s_globalDegrees;
    return tilt;
}
