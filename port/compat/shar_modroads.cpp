// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Mod Launcher CustomRoadBehaviour.xml (see shar_modroads.h). */
#include "shar_modroads.h"
#include "shar_mods.h"
#include "shar_modxml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace
{

const int MAX_ROADS = 256;

struct RoadRule
{
    int level;
    char name[ 64 ];
};

RoadRule s_noSnap[ MAX_ROADS ];
int s_numNoSnap = 0;
bool s_loaded = false;

bool IsTrue( const char* value )
{
    return strcasecmp( value, "true" ) == 0 || atoi( value ) != 0;
}

void Load()
{
    s_loaded = true;
    if( !shar_ModHackRequired( "CustomRoadBehaviour" ) )
    {
        return;
    }
    char* text = shar_ModConfig( "CustomRoadBehaviour.xml" );
    if( text == NULL )
    {
        return;
    }
    SharXmlReader reader = { text, 0 };
    SharXmlElement e;
    int level = -1;
    int levelDepth = -1, roadDepth = -1;
    char road[ 64 ] = "";
    bool reportedUTurn = false;
    while( shar_XmlNext( &reader, &e ) )
    {
        if( e.depth <= levelDepth )
        {
            level = -1;
            levelDepth = -1;
        }
        if( e.depth <= roadDepth )
        {
            road[ 0 ] = '\0';
            roadDepth = -1;
        }
        char value[ 64 ];
        if( shar_XmlIs( &e, "Level" ) )
        {
            level = -1;
            if( shar_XmlAttr( &e, "Index", value, sizeof( value ) ) )
            {
                level = atoi( value );
            }
            else if( shar_XmlAttr( &e, "Number", value, sizeof( value ) ) )
            {
                level = atoi( value ) - 1;
            }
            levelDepth = e.depth;
        }
        else if( shar_XmlIs( &e, "Road" ) && level >= 0 )
        {
            road[ 0 ] = '\0';
            shar_XmlAttr( &e, "Name", road, sizeof( road ) );
            roadDepth = e.depth;
        }
        else if( shar_XmlIs( &e, "Behaviour" ) && level >= 0 )
        {
            char name[ 64 ];
            if( !shar_XmlAttr( &e, "Name", name, sizeof( name ) ) || !shar_XmlAttr( &e, "Value", value, sizeof( value ) ) )
            {
                continue;
            }
            if( strcasecmp( name, "DisableTrafficGroundSnapping" ) == 0 && road[ 0 ] != '\0' )
            {
                if( IsTrue( value ) && s_numNoSnap < MAX_ROADS )
                {
                    RoadRule& rule = s_noSnap[ s_numNoSnap++ ];
                    rule.level = level;
                    snprintf( rule.name, sizeof( rule.name ), "%s", road );
                }
            }
            else if( strcasecmp( name, "UTurnTolerance" ) == 0 )
            {
                if( !reportedUTurn )
                {
                    fprintf( stderr, "mods: CustomRoadBehaviour: UTurnTolerance not supported yet, ignored\n" );
                    reportedUTurn = true;
                }
            }
            else
            {
                fprintf( stderr, "mods: CustomRoadBehaviour: unknown behaviour %s, ignored\n", name );
            }
        }
    }
    free( text );
    fprintf( stderr, "mods: CustomRoadBehaviour: %d roads without traffic ground snapping\n", s_numNoSnap );
}

} // namespace

extern "C" int shar_ModRoadNoGroundSnap( int levelIndex, const char* roadName )
{
    if( !s_loaded )
    {
        Load();
    }
    for( int i = 0; i < s_numNoSnap; i++ )
    {
        if( s_noSnap[ i ].level == levelIndex && strcasecmp( s_noSnap[ i ].name, roadName ) == 0 )
        {
            return 1;
        }
    }
    return 0;
}
