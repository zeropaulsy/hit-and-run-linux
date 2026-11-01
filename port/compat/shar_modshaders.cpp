/* Mod Launcher OverrideShaderParameters.xml (see shar_modshaders.h). */
#include "shar_modshaders.h"
#include "shar_mods.h"
#include "shar_modxml.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace
{

const int MAX_ENTRIES = 256;

struct Entry
{
    char pattern[ 64 ];         // empty: every shader
    SharModShaderParam value;
};

Entry s_entries[ MAX_ENTRIES ];
int s_numEntries = 0;
pthread_once_t s_once = PTHREAD_ONCE_INIT;

unsigned FourCC( const char* name )
{
    unsigned c = 0;
    for( int i = 0; i < 4 && name[ i ] != '\0'; i++ )
    {
        c |= static_cast< unsigned >( static_cast< unsigned char >( toupper( name[ i ] ) ) ) << ( 8 * i );
    }
    return c;
}

bool InList( const char* name, const char* const* list )
{
    for( ; *list != NULL; list++ )
    {
        if( strcasecmp( name, *list ) == 0 )
        {
            return true;
        }
    }
    return false;
}

// by the pddi's shader parameters (pddishade.hpp); everything not listed is an int
int TypeOf( const char* name )
{
    static const char* const textures[] = { "TEX", "TTEX", "LMAP", "BUMP", "REFL", "SPMP", "SHTX", "PRJT", "PRJ", "PRJ1",
                                            "TEX1", "TEX2", "TEX3", "TEX4", "TEX5", "TEX6", "TEX7", "TEX8", NULL };
    static const char* const colours[] = { "AMBI", "DIFF", "EMIS", "SPEC", "ENVB", "REFC", "SHCL", "CBVC", "CST1", "CST2",
                                           "CST3", "CST4", "CST5", "CST6", "CST7", "CST8", NULL };
    static const char* const floats[] = { "SHIN", "ACTH", "MSHP", "REFI", "REFB", "PSSZ", "PSSA", "PSSB", "PSCA", "PSCB",
                                          "PSCC", "PSCS", "CBVV", "TWTH", NULL };
    static const char* const vectors[] = { "ROTV", "LITV", "CAMV", NULL };
    if( InList( name, textures ) )
    {
        return SHAR_SHADERPARAM_TEXTURE;
    }
    if( InList( name, colours ) )
    {
        return SHAR_SHADERPARAM_COLOUR;
    }
    if( InList( name, floats ) )
    {
        return SHAR_SHADERPARAM_FLOAT;
    }
    if( InList( name, vectors ) )
    {
        return SHAR_SHADERPARAM_VECTOR;
    }
    return SHAR_SHADERPARAM_INT;
}

int Channel( const SharXmlElement* e, const char* attr, int defaultValue, bool* found )
{
    char value[ 16 ];
    if( shar_XmlAttr( e, attr, value, sizeof( value ) ) && value[ 0 ] != '\0' )
    {
        *found = true;
        int v = atoi( value );
        return v < 0 ? 0 : v > 255 ? 255 : v;
    }
    return defaultValue;
}

void ReadValue( const SharXmlElement* e, SharModShaderParam* p )
{
    char value[ 64 ] = "";
    bool hasValue = shar_XmlAttr( e, "Value", value, sizeof( value ) ) && value[ 0 ] != '\0';
    switch( p->type )
    {
    case SHAR_SHADERPARAM_COLOUR:
        if( hasValue )
        {
            const char* digits = value;
            if( digits[ 0 ] == '#' )
            {
                digits++;
            }
            else if( digits[ 0 ] == '0' && ( digits[ 1 ] == 'x' || digits[ 1 ] == 'X' ) )
            {
                digits += 2;
            }
            p->colour = static_cast< unsigned >( strtoul( digits, NULL, 16 ) );
            if( strlen( digits ) <= 6 )
            {
                p->colour |= 0xFF000000u;
            }
            p->hasValue = 1;
        }
        else
        {
            bool found = false;
            int r = Channel( e, "Red", 0, &found );
            int g = Channel( e, "Green", 0, &found );
            int b = Channel( e, "Blue", 0, &found );
            int a = Channel( e, "Alpha", 255, &found );
            if( found )
            {
                p->colour = static_cast< unsigned >( b | ( g << 8 ) | ( r << 16 ) | ( a << 24 ) );
                p->hasValue = 1;
            }
        }
        break;
    case SHAR_SHADERPARAM_FLOAT:
        if( hasValue )
        {
            p->floatValue = static_cast< float >( atof( value ) );
            p->hasValue = 1;
        }
        break;
    case SHAR_SHADERPARAM_VECTOR:
        if( hasValue )
        {
            p->hasValue = sscanf( value, "%f , %f , %f", &p->vector[ 0 ], &p->vector[ 1 ], &p->vector[ 2 ] ) == 3;
        }
        else
        {
            static const char* const axes[] = { "X", "Y", "Z" };
            for( int i = 0; i < 3; i++ )
            {
                char v[ 32 ];
                if( shar_XmlAttr( e, axes[ i ], v, sizeof( v ) ) && v[ 0 ] != '\0' )
                {
                    p->vector[ i ] = static_cast< float >( atof( v ) );
                    p->hasValue = 1;
                }
            }
        }
        break;
    case SHAR_SHADERPARAM_TEXTURE:
        if( hasValue )
        {
            snprintf( p->texture, sizeof( p->texture ), "%s", value );
            p->hasValue = 1;
        }
        break;
    default:
        if( hasValue )
        {
            p->intValue = strcasecmp( value, "true" ) == 0 ? 1 : strcasecmp( value, "false" ) == 0 ? 0 : atoi( value );
            p->hasValue = 1;
        }
        break;
    }
}

void Load()
{
    if( !shar_ModHackRequired( "OverrideShaderParameters" ) )
    {
        return;
    }
    char* text = shar_ModConfig( "OverrideShaderParameters.xml" );
    if( text == NULL )
    {
        return;
    }
    bool log = getenv( "SHAR_MOD_LOG" ) != NULL;
    SharXmlReader reader = { text, 0 };
    SharXmlElement e;
    char pattern[ 64 ] = "";
    int shaderDepth = -1;
    while( shar_XmlNext( &reader, &e ) )
    {
        if( e.depth <= shaderDepth )
        {
            pattern[ 0 ] = '\0';
            shaderDepth = -1;
        }
        if( shar_XmlIs( &e, "Shader" ) )
        {
            if( !shar_XmlAttr( &e, "Name", pattern, sizeof( pattern ) ) )
            {
                pattern[ 0 ] = '\0';
            }
            shaderDepth = e.depth;
            continue;
        }
        char name[ 8 ];
        if( !shar_XmlIs( &e, "Parameter" ) || !shar_XmlAttr( &e, "Name", name, sizeof( name ) ) || name[ 0 ] == '\0' )
        {
            continue;
        }
        if( s_numEntries == MAX_ENTRIES )
        {
            fprintf( stderr, "mods: OverrideShaderParameters.xml: more than %d parameters, rest ignored\n", MAX_ENTRIES );
            break;
        }
        Entry* entry = &s_entries[ s_numEntries++ ];
        memset( entry, 0, sizeof( *entry ) );
        snprintf( entry->pattern, sizeof( entry->pattern ), "%s", pattern );
        entry->value.param = FourCC( name );
        entry->value.type = TypeOf( name );
        ReadValue( &e, &entry->value );
        if( log )
        {
            fprintf( stderr, "mods: shader override %s %s%s\n", pattern[ 0 ] != '\0' ? pattern : "(every shader)", name,
                     entry->value.hasValue ? "" : " (file's value)" );
        }
    }
    free( text );
}

// glob with * and ?, any case
bool Match( const char* pattern, const char* name )
{
    while( *pattern != '\0' )
    {
        if( *pattern == '*' )
        {
            while( *pattern == '*' )
            {
                pattern++;
            }
            if( *pattern == '\0' )
            {
                return true;
            }
            for( ; *name != '\0'; name++ )
            {
                if( Match( pattern, name ) )
                {
                    return true;
                }
            }
            return false;
        }
        if( *name == '\0' || ( *pattern != '?' && tolower( static_cast< unsigned char >( *pattern ) ) !=
                                                     tolower( static_cast< unsigned char >( *name ) ) ) )
        {
            return false;
        }
        pattern++;
        name++;
    }
    return *name == '\0';
}

}

extern "C" int shar_ModShaderOverrides( const char* shaderName, SharModShaderParam* out, int max )
{
    pthread_once( &s_once, Load );
    int count = 0;
    for( int i = 0; i < s_numEntries; i++ )
    {
        const Entry* entry = &s_entries[ i ];
        if( entry->pattern[ 0 ] != '\0' && !Match( entry->pattern, shaderName ) )
        {
            continue;
        }
        int slot = 0;
        while( slot < count && out[ slot ].param != entry->value.param )
        {
            slot++;
        }
        if( slot == count )
        {
            if( count == max )
            {
                continue;
            }
            count++;
        }
        out[ slot ] = entry->value;
    }
    return count;
}
