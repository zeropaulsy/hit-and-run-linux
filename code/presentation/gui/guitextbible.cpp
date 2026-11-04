//===========================================================================
// Copyright (C) 2003 Radical Entertainment Ltd.  All rights reserved.
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2003/02/26		TChu		Created
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================

#include <presentation/gui/guitextbible.h>

#include <App.h>
#include <TextBible.h>
#include <raddebug.hpp>
#ifdef RAD_LINUX
#include <radfile.hpp>
#endif

#include <sound/soundmanager.h>

#ifdef RAD_LINUX
#include <shar_mods.h>
#include <mission/gameplaymanager.h>
#include <mission/mission.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

//===========================================================================
// Linux port: Mod Launcher CustomText (CustomText.ini or CustomText/<language>.ini, UTF-8 or UTF-16): strings by
// name in [CustomText] (everywhere), [CustomTextL1] (level 1), [CustomTextL1M2] / L1SR1 / L1GR1 / L1BM1 (that
// mission, by its script name), $(Variables). They replace or add text bible strings through Scrooby's lookup
// hook. With several language files: the mod's Language setting (ModSettings.ini [InternalName] Language=<Title>),
// else the file titled as the game's language, else English, else the first.
//===========================================================================
namespace
{
    struct ModText
    {
        char scope[ 16 ];       // "", "L1", "L1M2" (upper case)
        char* name;             // upper case
        UnicodeChar* text;
        unsigned int modulo;    // hash cached for this language modulo
        unsigned int hash;
    };

    ModText* s_modTexts = NULL;
    int s_numModTexts = 0;

    // A file's bytes as UTF-8 (UTF-16 with a byte-order mark converted), malloc'd
    char* ToUtf8( const char* data, size_t size )
    {
        const unsigned char* b = reinterpret_cast<const unsigned char*>( data );
        bool le = size >= 2 && b[ 0 ] == 0xFF && b[ 1 ] == 0xFE;
        bool be = size >= 2 && b[ 0 ] == 0xFE && b[ 1 ] == 0xFF;
        if( !le && !be )
        {
            size_t skip = ( size >= 3 && b[ 0 ] == 0xEF && b[ 1 ] == 0xBB && b[ 2 ] == 0xBF ) ? 3 : 0;
            char* out = static_cast<char*>( malloc( size - skip + 1 ) );
            memcpy( out, data + skip, size - skip );
            out[ size - skip ] = '\0';
            return out;
        }
        char* out = static_cast<char*>( malloc( ( size / 2 ) * 3 + 1 ) );
        size_t n = 0;
        for( size_t i = 2; i + 1 < size; i += 2 )
        {
            unsigned int c = le ? ( b[ i ] | ( b[ i + 1 ] << 8 ) ) : ( ( b[ i ] << 8 ) | b[ i + 1 ] );
            if( c < 0x80 ) out[ n++ ] = static_cast<char>( c );
            else if( c < 0x800 ) { out[ n++ ] = static_cast<char>( 0xC0 | ( c >> 6 ) ); out[ n++ ] = static_cast<char>( 0x80 | ( c & 0x3F ) ); }
            else { out[ n++ ] = static_cast<char>( 0xE0 | ( c >> 12 ) ); out[ n++ ] = static_cast<char>( 0x80 | ( ( c >> 6 ) & 0x3F ) ); out[ n++ ] = static_cast<char>( 0x80 | ( c & 0x3F ) ); }
        }
        out[ n ] = '\0';
        return out;
    }

    // UTF-8 to the game's UTF-16 (BMP only), NUL-terminated, at least 256 characters (as the Mod Launcher)
    UnicodeChar* ToUnicode( const char* s )
    {
        size_t length = strlen( s );
        UnicodeChar* out = static_cast<UnicodeChar*>( malloc( sizeof( UnicodeChar ) * ( length + 256 ) ) );
        size_t n = 0;
        const unsigned char* p = reinterpret_cast<const unsigned char*>( s );
        while( *p != 0 )
        {
            unsigned int c = *p++;
            if( c >= 0xE0 && p[ 0 ] && p[ 1 ] ) { c = ( ( c & 0x0F ) << 12 ) | ( ( p[ 0 ] & 0x3F ) << 6 ) | ( p[ 1 ] & 0x3F ); p += 2; }
            else if( c >= 0xC0 && p[ 0 ] ) { c = ( ( c & 0x1F ) << 6 ) | ( p[ 0 ] & 0x3F ); p += 1; }
            out[ n++ ] = static_cast<UnicodeChar>( c );
        }
        out[ n ] = 0;
        return out;
    }

    char* Trim( char* s )
    {
        while( isspace( static_cast<unsigned char>( *s ) ) ) s++;
        char* e = s + strlen( s );
        while( e > s && isspace( static_cast<unsigned char>( e[ -1 ] ) ) ) *--e = '\0';
        return s;
    }

    typedef void ( *IniLineCallback )( const char* section, const char* key, const char* value, void* user );

    // Calls cb( section, key, value, user ) for each key=value line of an ini text (modified in place)
    void ForEachIniLine( char* text, IniLineCallback cb, void* user )
    {
        char section[ 64 ] = "";
        char* next = text;
        while( next != NULL && *next != '\0' )
        {
            char* line = next;
            next = strpbrk( line, "\r\n" );
            if( next != NULL )
            {
                *next++ = '\0';
            }
            line = Trim( line );
            if( *line == ';' || *line == '\0' )
            {
                continue;
            }
            if( *line == '[' )
            {
                char* end = strchr( line, ']' );
                if( end != NULL )
                {
                    *end = '\0';
                    snprintf( section, sizeof( section ), "%s", line + 1 );
                }
                continue;
            }
            char* eq = strchr( line, '=' );
            if( eq == NULL )
            {
                continue;
            }
            *eq = '\0';
            cb( section, Trim( line ), Trim( eq + 1 ), user );
        }
    }

    struct Variable { char name[ 64 ]; char* value; };

    // value with $(Name) replaced and \\ unescaped, malloc'd
    char* Expand( const char* value, const Variable* vars, int numVars, int depth = 0 )
    {
        size_t capacity = strlen( value ) + 1, n = 0;
        char* out = static_cast<char*>( malloc( capacity ) );
        for( const char* p = value; *p != '\0'; )
        {
            const char* insert = NULL;
            size_t insertLength = 0;
            char* expanded = NULL;
            if( p[ 0 ] == '$' && p[ 1 ] == '(' && strchr( p, ')' ) != NULL && depth < 8 )
            {
                const char* end = strchr( p, ')' );
                for( int i = 0; i < numVars; i++ )
                {
                    if( strlen( vars[ i ].name ) == static_cast<size_t>( end - p - 2 ) &&
                        strncasecmp( vars[ i ].name, p + 2, end - p - 2 ) == 0 )
                    {
                        expanded = Expand( vars[ i ].value, vars, numVars, depth + 1 );
                        insert = expanded;
                        insertLength = strlen( expanded );
                        p = end + 1;
                        break;
                    }
                }
            }
            if( insert == NULL )
            {
                if( p[ 0 ] == '\\' && p[ 1 ] == '\\' )
                {
                    p++;
                }
                insert = p++;
                insertLength = 1;
            }
            if( n + insertLength + 1 > capacity )
            {
                capacity = ( n + insertLength + 1 ) * 2;
                out = static_cast<char*>( realloc( out, capacity ) );
            }
            memcpy( out + n, insert, insertLength );
            n += insertLength;
            free( expanded );
        }
        out[ n ] = '\0';
        return out;
    }

    struct Variables
    {
        Variable vars[ 128 ];
        int count;
    };

    void OnVariable( const char* section, const char* key, const char* value, void* user )
    {
        Variables* v = static_cast<Variables*>( user );
        if( strcasecmp( section, "Variables" ) == 0 && v->count < 128 )
        {
            snprintf( v->vars[ v->count ].name, sizeof( v->vars[ v->count ].name ), "%s", key );
            v->vars[ v->count ].value = strdup( value );
            v->count++;
        }
    }

    void OnText( const char* section, const char* key, const char* value, void* user )
    {
        const Variables* v = static_cast<const Variables*>( user );
        const Variable* vars = v->vars;
        int numVars = v->count;
        {
            if( strncasecmp( section, "CustomText", 10 ) != 0 )
            {
                return;
            }
            const char* scope = section + 10;
            size_t valueLength = strlen( value );
            char* unquoted = strdup( value );
            if( valueLength >= 2 && value[ 0 ] == '"' && value[ valueLength - 1 ] == '"' )
            {
                unquoted[ valueLength - 1 ] = '\0';
                memmove( unquoted, unquoted + 1, valueLength - 1 );
            }
            char* expanded = Expand( unquoted, vars, numVars );
            free( unquoted );

            s_modTexts = static_cast<ModText*>( realloc( s_modTexts, sizeof( ModText ) * ( s_numModTexts + 1 ) ) );
            ModText& t = s_modTexts[ s_numModTexts++ ];
            snprintf( t.scope, sizeof( t.scope ), "%s", scope );
            for( char* c = t.scope; *c; c++ ) *c = static_cast<char>( toupper( *c ) );
            t.name = strdup( key );
            for( char* c = t.name; *c; c++ ) *c = static_cast<char>( toupper( *c ) );
            t.text = ToUnicode( expanded );
            t.modulo = 0;
            t.hash = 0;
            free( expanded );
        }
    }

    void LoadModTexts( char* text )
    {
        Variables* v = static_cast<Variables*>( malloc( sizeof( Variables ) ) );
        v->count = 0;
        char* copy = strdup( text );
        ForEachIniLine( copy, OnVariable, v );
        free( copy );
        ForEachIniLine( text, OnText, v );
        for( int i = 0; i < v->count; i++ )
        {
            free( v->vars[ i ].value );
        }
        free( v );
    }

    // [Miscellaneous] Title=, the name the Language setting lists the file under
    void OnTitle( const char* section, const char* key, const char* value, void* user )
    {
        if( strcasecmp( section, "Miscellaneous" ) == 0 && strcasecmp( key, "Title" ) == 0 )
        {
            snprintf( static_cast<char*>( user ), 64, "%s", value );
        }
    }

    // a file's title, or its name without .ini when it has none
    void FileTitle( const char* utf8, const char* fileName, char* title )
    {
        title[ 0 ] = '\0';
        char* copy = strdup( utf8 );
        ForEachIniLine( copy, OnTitle, title );
        free( copy );
        if( title[ 0 ] == '\0' )
        {
            snprintf( title, 64, "%s", fileName );
            char* dot = strrchr( title, '.' );
            if( dot != NULL && strcasecmp( dot, ".ini" ) == 0 )
            {
                *dot = '\0';
            }
        }
    }

    // FeLanguage::GetHash
    unsigned int TextHash( const char* name, unsigned int modulo )
    {
        unsigned int hash = 0;
        for( const char* p = name; *p != '\0'; p++ )
        {
            hash = ( 64 * hash + *p ) % modulo;
        }
        return hash;
    }

    UnicodeChar* ModTextOverride( unsigned int hash, unsigned int modulo )
    {
        // the scopes that apply now, most specific first
        char level[ 16 ] = "", mission[ 32 ] = "";
        GameplayManager* gm = GetGameplayManager();
        if( gm != NULL )
        {
            snprintf( level, sizeof( level ), "L%d", static_cast<int>( gm->GetCurrentLevelIndex() ) + 1 );
            Mission* m = gm->GetCurrentMission();
            if( m != NULL && m->GetName() != NULL )
            {
                snprintf( mission, sizeof( mission ), "%s%s", level, m->GetName() );
                for( char* c = mission; *c; c++ ) *c = static_cast<char>( toupper( *c ) );
            }
        }

        UnicodeChar* best = NULL;
        int bestRank = -1;
        for( int i = 0; i < s_numModTexts; i++ )
        {
            ModText& t = s_modTexts[ i ];
            if( t.modulo != modulo )
            {
                t.hash = TextHash( t.name, modulo );
                t.modulo = modulo;
            }
            if( t.hash != hash )
            {
                continue;
            }
            int rank = t.scope[ 0 ] == '\0' ? 0 :
                       strcmp( t.scope, level ) == 0 ? 1 :
                       ( mission[ 0 ] != '\0' && strcmp( t.scope, mission ) == 0 ) ? 2 : -1;
            if( rank > bestRank )
            {
                best = t.text;
                bestRank = rank;
            }
        }
        return best;
    }

    void InstallModTexts()
    {
        static bool s_done = false;
        if( s_done )
        {
            return;
        }
        s_done = true;

        char* texts[ 16 ];
        size_t sizes[ 16 ];
        char* names[ 16 ];
        int count = 0;
        char* single = shar_ModConfig( "CustomText.ini" );
        if( single != NULL )
        {
            texts[ 0 ] = single;
            sizes[ 0 ] = 0;     // not needed: CustomText.ini read as text below
            names[ 0 ] = strdup( "CustomText.ini" );
            count = 1;
        }
        else
        {
            count = shar_ModConfigFolder( "CustomText", texts, sizes, names, 16 );
        }

        // the language: the mod's Language setting (ModSettings.ini, by title or file name), else the game's
        // language, else English, else the first file
        const char* setting = single == NULL ? shar_ModConfigFolderLanguage( "CustomText" ) : NULL;
        // by name: the Scrooby::XLLanguage values are not indexes into a short list (XL_SPANISH is 7)
        const char* game = "English";
        switch( CGuiTextBible::GetCurrentLanguage() )
        {
            case Scrooby::XL_FRENCH:  game = "French";  break;
            case Scrooby::XL_GERMAN:  game = "German";  break;
            case Scrooby::XL_SPANISH: game = "Spanish"; break;
            default: break;
        }
        int bySetting = -1, byGame = -1, byEnglish = -1;
        char* utf8[ 16 ];
        for( int i = 0; i < count; i++ )
        {
            utf8[ i ] = ToUtf8( texts[ i ], sizes[ i ] != 0 ? sizes[ i ] : strlen( texts[ i ] ) );
            char title[ 64 ];
            FileTitle( utf8[ i ], names[ i ], title );
            char fileName[ 64 ];
            FileTitle( "", names[ i ], fileName );
            if( bySetting < 0 && setting != NULL && ( strcasecmp( title, setting ) == 0 || strcasecmp( fileName, setting ) == 0 ) )
            {
                bySetting = i;
            }
            if( byGame < 0 && strcasecmp( title, game ) == 0 )
            {
                byGame = i;
            }
            if( byEnglish < 0 && strcasecmp( title, "English" ) == 0 )
            {
                byEnglish = i;
            }
        }
        if( setting != NULL && bySetting < 0 )
        {
            fprintf( stderr, "mods: CustomText: no language \"%s\" (ModSettings.ini Language)\n", setting );
        }
        int chosen = bySetting >= 0 ? bySetting : byGame >= 0 ? byGame : byEnglish >= 0 ? byEnglish : ( count > 0 ? 0 : -1 );
        if( chosen >= 0 )
        {
            LoadModTexts( utf8[ chosen ] );
            fprintf( stderr, "mods: CustomText: %d strings from %s\n", s_numModTexts, names[ chosen ] );
        }
        for( int i = 0; i < count; i++ )
        {
            free( utf8[ i ] );
            free( texts[ i ] );
            free( names[ i ] );
        }
        if( s_numModTexts > 0 )
        {
            Scrooby::g_TextOverride = ModTextOverride;
        }
    }
}
#endif

Scrooby::TextBible* CGuiTextBible::s_textBible = NULL;
Scrooby::XLLanguage CGuiTextBible::s_currentLanguage = Scrooby::XL_ENGLISH;
#ifdef RAD_LINUX
Scrooby::XLLanguage CGuiTextBible::s_startupLanguage = Scrooby::XL_ENGLISH;

bool CGuiTextBible::GameFileExists( const char* filename )
{
    IRadFile* file = NULL;
    radFileOpenSync( &file, filename, false, OpenExisting );
    const bool exists = file != NULL && file->IsOpen();
    if( file != NULL )
    {
        file->Release();
    }
    return exists;
}
#endif

//===========================================================================
// Public Member Functions
//===========================================================================

CGuiTextBible::CGuiTextBible()
{
}

CGuiTextBible::~CGuiTextBible()
{
    s_textBible = NULL;
}

void
CGuiTextBible::SetTextBible( const char* textBible )
{
    rAssert( textBible != NULL );
    s_textBible = Scrooby::App::GetInstance()->GetTextBible( textBible );
#ifdef RAD_LINUX
    InstallModTexts();
#endif
}

P3D_UNICODE*
CGuiTextBible::GetLocalizedText( const char* stringID )
{
    rAssert( stringID != NULL );
    rAssert( s_textBible != NULL );

    P3D_UNICODE* localizedText = static_cast<P3D_UNICODE*>( s_textBible->GetWChar( stringID ) );

/*
#ifndef RAD_RELEASE
    if( localizedText == NULL )
    {
        char msg[ 256 ];
        sprintf( msg, "Can't find text bible string for entry: %s!", stringID );
        rTuneWarningMsg( false, msg );
    }
#endif
*/

    return localizedText;
}

void
#ifdef RAD_LINUX
CGuiTextBible::SetCurrentLanguage( const Scrooby::XLLanguage language, bool dialogue )
#else
CGuiTextBible::SetCurrentLanguage( const Scrooby::XLLanguage language )
#endif
{
    Scrooby::App::GetInstance()->SetLocalizationLanguage( language );

    s_currentLanguage = language;

#ifdef RAD_LINUX
    if( dialogue )
#endif
    GetSoundManager()->SetDialogueLanguage( language );
}

Scrooby::XLLanguage
CGuiTextBible::GetCurrentLanguage()
{
    return s_currentLanguage;
}

bool CGuiTextBible::IsTextBibleLoaded()
{
    return s_textBible != NULL;
}


