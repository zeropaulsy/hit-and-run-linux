// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Mod Launcher CustomTrafficSupport.xml (see shar_modtraffic.h). */
#include "shar_modtraffic.h"
#include "shar_mods.h"
#include "shar_modxml.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace
{

const int MAX_COLOURS = 64;
const int MAX_CARS = 32;            // per scope
const int MAX_GROUPS = 10;          // TrafficManager::MAX_TRAFFIC_MODEL_GROUPS
const int MAX_GROUP_CARS = 16;
const int NUM_SCOPES = 8;           // 0 = root, 1-7 = levels 0-6

struct Colours
{
    bool present;
    bool inherit;
    int count;
    unsigned char rgb[ MAX_COLOURS ][ 3 ];
};

struct Car
{
    char name[ 32 ];
    Colours colours;
};

struct GroupCar
{
    char name[ 32 ];
    float weight;
};

struct Group
{
    bool present;
    float multiplier;
    int numCars;
    GroupCar cars[ MAX_GROUP_CARS ];
};

struct Scope
{
    SharModTrafficSettings settings;
    Colours colours;
    int numCars;
    Car cars[ MAX_CARS ];
    Group groups[ MAX_GROUPS ];
};

Scope* s_scopes = NULL;     // NUM_SCOPES, malloc'd (large)
bool s_loaded = false;
bool s_active = false;

enum Kind { K_OTHER, K_LEVEL, K_CAR, K_COLOURS, K_GROUP };

void ClearSettings( SharModTrafficSettings* s )
{
    float* f = (float*)s;
    for( size_t i = 0; i < sizeof( *s ) / sizeof( float ); i++ )
    {
        f[ i ] = SHAR_MODTRAFFIC_UNSET;
    }
}

float* SettingField( SharModTrafficSettings* s, const SharXmlElement* e, const char** attr )
{
    static const struct { const char* name; const char* attr; size_t offset; } FIELDS[] = {
        { "PreallocatedCars", "Amount", offsetof( SharModTrafficSettings, preallocatedCars ) },
        { "AllocatedCars", "Amount", offsetof( SharModTrafficSettings, allocatedCars ) },
        { "MaxTraffic", "Amount", offsetof( SharModTrafficSettings, maxTraffic ) },
        { "MaxTrafficOnFoot", "Amount", offsetof( SharModTrafficSettings, maxTrafficOnFoot ) },
        { "RoadTrafficLimitMultiplier", "Value", offsetof( SharModTrafficSettings, roadTrafficLimitMultiplier ) },
        { "RoadLaneMaxTrafficLimit", "Amount", offsetof( SharModTrafficSettings, roadLaneMaxTrafficLimit ) },
        { "RemoveInterval", "Value", offsetof( SharModTrafficSettings, removeInterval ) },
        { "AddInterval", "Value", offsetof( SharModTrafficSettings, addInterval ) },
        { "PopulationDuration", "Value", offsetof( SharModTrafficSettings, populationDuration ) },
        { "FadeDistance", "Value", offsetof( SharModTrafficSettings, fadeDistance ) },
        { "CentreOffset", "Value", offsetof( SharModTrafficSettings, centreOffset ) },
        { "AddDistance", "Value", offsetof( SharModTrafficSettings, addDistance ) },
        { "RemoveDistance", "Value", offsetof( SharModTrafficSettings, removeDistance ) },
        { "PopulateAddDistance", "Value", offsetof( SharModTrafficSettings, populateAddDistance ) },
        { "DecelerationSpeed", "Value", offsetof( SharModTrafficSettings, decelerationSpeed ) },
        { "AccelerationSpeed", "Value", offsetof( SharModTrafficSettings, accelerationSpeed ) },
        { "DecelerationBrakeThreshold", "Value", offsetof( SharModTrafficSettings, decelerationBrakeThreshold ) },
    };
    for( size_t i = 0; i < sizeof( FIELDS ) / sizeof( FIELDS[ 0 ] ); i++ )
    {
        if( shar_XmlIs( e, FIELDS[ i ].name ) )
        {
            *attr = FIELDS[ i ].attr;
            return (float*)( (char*)s + FIELDS[ i ].offset );
        }
    }
    return NULL;
}

Car* FindCar( Scope* scope, const char* name, bool create )
{
    for( int i = 0; i < scope->numCars; i++ )
    {
        if( strcasecmp( scope->cars[ i ].name, name ) == 0 )
        {
            return &scope->cars[ i ];
        }
    }
    if( !create || scope->numCars >= MAX_CARS )
    {
        return NULL;
    }
    Car* car = &scope->cars[ scope->numCars++ ];
    memset( car, 0, sizeof( *car ) );
    snprintf( car->name, sizeof( car->name ), "%s", name );
    return car;
}

void Load()
{
    s_loaded = true;
    if( !shar_ModHackRequired( "CustomTrafficSupport" ) )
    {
        return;
    }
    char* text = shar_ModConfig( "CustomTrafficSupport.xml" );
    if( text == NULL )
    {
        return;
    }
    s_scopes = (Scope*)calloc( NUM_SCOPES, sizeof( Scope ) );
    for( int i = 0; i < NUM_SCOPES; i++ )
    {
        ClearSettings( &s_scopes[ i ].settings );
    }
    s_active = true;

    const int MAX_DEPTH = 16;
    Kind kinds[ MAX_DEPTH ];
    void* objects[ MAX_DEPTH ];     // the Car / Colours / Group of that depth
    int scopeAt[ MAX_DEPTH ];       // the scope in effect at that depth
    SharXmlReader reader = { text, 0 };
    SharXmlElement e;
    while( shar_XmlNext( &reader, &e ) )
    {
        if( e.depth >= MAX_DEPTH )
        {
            continue;
        }
        Kind parent = e.depth > 0 ? kinds[ e.depth - 1 ] : K_OTHER;
        void* parentObject = e.depth > 0 ? objects[ e.depth - 1 ] : NULL;
        int scopeIndex = e.depth > 0 ? scopeAt[ e.depth - 1 ] : 0;
        Scope* scope = &s_scopes[ scopeIndex ];
        kinds[ e.depth ] = K_OTHER;
        objects[ e.depth ] = NULL;
        scopeAt[ e.depth ] = scopeIndex;

        char value[ 64 ];
        const char* attr = NULL;
        float* field;
        if( shar_XmlIs( &e, "Level" ) )
        {
            int level = -1;
            if( shar_XmlAttr( &e, "Index", value, sizeof( value ) ) )
            {
                level = atoi( value );
            }
            else if( shar_XmlAttr( &e, "Number", value, sizeof( value ) ) )
            {
                level = atoi( value ) - 1;
            }
            kinds[ e.depth ] = K_LEVEL;
            scopeAt[ e.depth ] = ( level >= 0 && level <= 6 ) ? level + 1 : 0;
            if( level < 0 || level > 6 )
            {
                fprintf( stderr, "mods: CustomTrafficSupport: bad Level, its settings go to the root\n" );
            }
        }
        else if( shar_XmlIs( &e, "DynamicTraffic" ) )
        {
            if( shar_XmlAttr( &e, "Type", value, sizeof( value ) ) )
            {
                scope->settings.dynamicType = strcasecmp( value, "Slots" ) == 0 ? SHAR_TRAFFIC_DYNAMIC_SLOTS
                                            : strcasecmp( value, "Models" ) == 0 ? SHAR_TRAFFIC_DYNAMIC_MODELS
                                            : SHAR_TRAFFIC_DYNAMIC_NONE;
            }
        }
        else if( ( field = SettingField( &scope->settings, &e, &attr ) ) != NULL )
        {
            if( shar_XmlAttr( &e, attr, value, sizeof( value ) ) )
            {
                *field = (float)atof( value );
            }
        }
        else if( shar_XmlIs( &e, "Group" ) )
        {
            int index = shar_XmlAttr( &e, "Index", value, sizeof( value ) ) ? atoi( value ) : -1;
            if( index >= 0 && index < MAX_GROUPS )
            {
                Group* group = &scope->groups[ index ];
                group->present = true;
                group->multiplier = shar_XmlAttr( &e, "Multiplier", value, sizeof( value ) ) ? (float)atof( value ) : 1.0f;
                kinds[ e.depth ] = K_GROUP;
                objects[ e.depth ] = group;
            }
        }
        else if( shar_XmlIs( &e, "Car" ) && shar_XmlAttr( &e, "Name", value, sizeof( value ) ) )
        {
            if( parent == K_GROUP )
            {
                Group* group = (Group*)parentObject;
                if( group->numCars < MAX_GROUP_CARS )
                {
                    GroupCar* car = &group->cars[ group->numCars++ ];
                    snprintf( car->name, sizeof( car->name ), "%s", value );
                    char weight[ 32 ];
                    car->weight = shar_XmlAttr( &e, "Weight", weight, sizeof( weight ) ) ? (float)atof( weight ) : -1.0f;
                }
            }
            else
            {
                Car* car = FindCar( scope, value, true );
                kinds[ e.depth ] = K_CAR;
                objects[ e.depth ] = car;
            }
        }
        else if( shar_XmlIs( &e, "Colours" ) )
        {
            Colours* colours = parent == K_CAR ? ( parentObject != NULL ? &( (Car*)parentObject )->colours : NULL )
                                               : &scope->colours;
            if( colours != NULL )
            {
                colours->present = true;
                colours->inherit = shar_XmlAttr( &e, "Inherit", value, sizeof( value ) ) &&
                                   ( strcasecmp( value, "true" ) == 0 || atoi( value ) != 0 );
                kinds[ e.depth ] = K_COLOURS;
                objects[ e.depth ] = colours;
            }
        }
        else if( shar_XmlIs( &e, "Colour" ) && parent == K_COLOURS )
        {
            Colours* colours = (Colours*)parentObject;
            if( colours->count < MAX_COLOURS )
            {
                char r[ 16 ] = "0", g[ 16 ] = "0", b[ 16 ] = "0";
                shar_XmlAttr( &e, "Red", r, sizeof( r ) );
                shar_XmlAttr( &e, "Green", g, sizeof( g ) );
                shar_XmlAttr( &e, "Blue", b, sizeof( b ) );
                colours->rgb[ colours->count ][ 0 ] = (unsigned char)atoi( r );
                colours->rgb[ colours->count ][ 1 ] = (unsigned char)atoi( g );
                colours->rgb[ colours->count ][ 2 ] = (unsigned char)atoi( b );
                colours->count++;
            }
        }
        else if( !shar_XmlIs( &e, "CustomTrafficSupport" ) )
        {
            fprintf( stderr, "mods: CustomTrafficSupport: unknown element %.*s, ignored\n", e.nameLength, e.name );
        }
    }
    free( text );
    fprintf( stderr, "mods: CustomTrafficSupport: read\n" );
}

void EnsureLoaded()
{
    if( !s_loaded )
    {
        Load();
    }
}

int AddColours( const Colours* c, unsigned char ( *rgb )[ 3 ], int count, int max )
{
    for( int i = 0; i < c->count && count < max; i++, count++ )
    {
        memcpy( rgb[ count ], c->rgb[ i ], 3 );
    }
    return count;
}

} // namespace

extern "C" int shar_ModTrafficActive( void )
{
    EnsureLoaded();
    return s_active;
}

extern "C" void shar_ModTrafficSettings( int level, SharModTrafficSettings* out )
{
    EnsureLoaded();
    ClearSettings( out );
    if( !s_active )
    {
        return;
    }
    *out = s_scopes[ 0 ].settings;
    if( level >= 0 && level <= 6 )
    {
        const float* from = (const float*)&s_scopes[ level + 1 ].settings;
        float* to = (float*)out;
        for( size_t i = 0; i < sizeof( *out ) / sizeof( float ); i++ )
        {
            if( from[ i ] != SHAR_MODTRAFFIC_UNSET )
            {
                to[ i ] = from[ i ];
            }
        }
    }
}

extern "C" int shar_ModTrafficMaxAllocated( void )
{
    EnsureLoaded();
    int most = 0;
    for( int i = 0; s_active && i < NUM_SCOPES; i++ )
    {
        float a = s_scopes[ i ].settings.allocatedCars;
        if( a != SHAR_MODTRAFFIC_UNSET && (int)a > most )
        {
            most = (int)a;
        }
    }
    return most;
}

extern "C" int shar_ModTrafficColours( int level, const char* car, unsigned char ( *rgb )[ 3 ], int max, int* withGame )
{
    EnsureLoaded();
    *withGame = 1;
    if( !s_active )
    {
        return 0;
    }
    Scope* levelScope = ( level >= 0 && level <= 6 ) ? &s_scopes[ level + 1 ] : NULL;
    Car* levelCar = levelScope != NULL && car != NULL ? FindCar( levelScope, car, false ) : NULL;
    Car* rootCar = car != NULL ? FindCar( &s_scopes[ 0 ], car, false ) : NULL;
    const Colours* chain[ 4 ] = { levelCar != NULL ? &levelCar->colours : NULL, rootCar != NULL ? &rootCar->colours : NULL,
                                  levelScope != NULL ? &levelScope->colours : NULL, &s_scopes[ 0 ].colours };
    int count = 0;
    for( int i = 0; i < 4; i++ )
    {
        if( chain[ i ] == NULL || !chain[ i ]->present )
        {
            continue;
        }
        count = AddColours( chain[ i ], rgb, count, max );
        if( !chain[ i ]->inherit )
        {
            *withGame = 0;
            return count;
        }
    }
    return count;
}

extern "C" float shar_ModTrafficGroupMultiplier( int level, int group )
{
    EnsureLoaded();
    if( !s_active || group < 0 || group >= MAX_GROUPS )
    {
        return 1.0f;
    }
    if( level >= 0 && level <= 6 && s_scopes[ level + 1 ].groups[ group ].present )
    {
        return s_scopes[ level + 1 ].groups[ group ].multiplier;
    }
    return s_scopes[ 0 ].groups[ group ].present ? s_scopes[ 0 ].groups[ group ].multiplier : 1.0f;
}

extern "C" float shar_ModTrafficCarWeight( int level, int group, const char* car, float defaultWeight )
{
    EnsureLoaded();
    if( !s_active || group < 0 || group >= MAX_GROUPS )
    {
        return defaultWeight;
    }
    for( int pass = 0; pass < 2; pass++ )
    {
        int scope = pass == 0 ? level + 1 : 0;
        if( scope < 0 || scope >= NUM_SCOPES || ( pass == 0 && ( level < 0 || level > 6 ) ) )
        {
            continue;
        }
        const Group* g = &s_scopes[ scope ].groups[ group ];
        for( int i = 0; g->present && i < g->numCars; i++ )
        {
            if( strcasecmp( g->cars[ i ].name, car ) == 0 && g->cars[ i ].weight >= 0.0f )
            {
                return g->cars[ i ].weight;
            }
        }
    }
    return defaultWeight;
}
