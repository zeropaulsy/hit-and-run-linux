// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Minimal reader for the Mod Launcher's XML config files (CustomShopSupport.xml, ...): walks the start tags in
 * document order. No allocation, no entities beyond the five predefined ones, comments, <?...?> and <!...> skipped.
 * Enough for the flat attribute-and-text style these files use.
 *
 *   SharXmlReader r = { text, 0 };
 *   SharXmlElement e;
 *   while ( shar_XmlNext( &r, &e ) )
 *       if ( shar_XmlIs( &e, "CarShop" ) ) { char level[ 8 ]; shar_XmlAttr( &e, "Level", level, sizeof( level ) ); }
 */
#ifndef SHAR_COMPAT_SHAR_MODXML_H
#define SHAR_COMPAT_SHAR_MODXML_H

#include <string.h>
#include <strings.h>

typedef struct SharXmlElement
{
    const char* name;           /* not terminated: nameLength characters */
    int nameLength;
    const char* attrs;          /* the rest of the start tag, up to '>' or "/>" */
    int attrsLength;
    const char* text;           /* text right after the start tag, up to the next '<' (empty for "<x/>") */
    int textLength;
    int depth;                  /* 0 = the root element */
} SharXmlElement;

typedef struct SharXmlReader
{
    const char* p;              /* the text, then the position after the last start tag */
    int depth;                  /* 0 to start */
} SharXmlReader;

static inline int shar_XmlIsSpace( char c )
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* The next start tag; 0 at the end of the text. */
static inline int shar_XmlNext( SharXmlReader* r, SharXmlElement* e )
{
    const char* p = r->p;
    if ( p == NULL )
    {
        return 0;
    }
    while ( *p != '\0' )
    {
        if ( *p != '<' )
        {
            p++;
            continue;
        }
        if ( strncmp( p, "<!--", 4 ) == 0 )
        {
            const char* end = strstr( p + 4, "-->" );
            p = end != NULL ? end + 3 : p + strlen( p );
            continue;
        }
        if ( p[ 1 ] == '?' || p[ 1 ] == '!' )
        {
            const char* end = strchr( p, '>' );
            p = end != NULL ? end + 1 : p + strlen( p );
            continue;
        }
        if ( p[ 1 ] == '/' )
        {
            if ( r->depth > 0 )
            {
                r->depth--;
            }
            const char* end = strchr( p, '>' );
            p = end != NULL ? end + 1 : p + strlen( p );
            continue;
        }
        /* a start tag */
        const char* name = p + 1;
        const char* q = name;
        while ( *q != '\0' && !shar_XmlIsSpace( *q ) && *q != '>' && *q != '/' )
        {
            q++;
        }
        e->name = name;
        e->nameLength = (int)( q - name );
        const char* attrs = q;
        char quote = 0;
        while ( *q != '\0' && ( quote != 0 || *q != '>' ) )
        {
            if ( quote == 0 && ( *q == '"' || *q == '\'' ) )
            {
                quote = *q;
            }
            else if ( *q == quote )
            {
                quote = 0;
            }
            q++;
        }
        int selfClosing = q > attrs && q[ -1 ] == '/';
        e->attrs = attrs;
        e->attrsLength = (int)( q - attrs ) - ( selfClosing ? 1 : 0 );
        e->depth = r->depth;
        p = *q == '>' ? q + 1 : q;
        if ( selfClosing )
        {
            e->text = p;
            e->textLength = 0;
        }
        else
        {
            r->depth++;
            e->text = p;
            const char* lt = strchr( p, '<' );
            e->textLength = (int)( ( lt != NULL ? lt : p + strlen( p ) ) - p );
        }
        r->p = p;
        return 1;
    }
    r->p = p;
    return 0;
}

static inline int shar_XmlIs( const SharXmlElement* e, const char* name )
{
    return (int)strlen( name ) == e->nameLength && strncasecmp( e->name, name, e->nameLength ) == 0;
}

/* Copies src[0, length) to out with the predefined entities decoded and surrounding blanks trimmed. */
static inline void shar_XmlCopy( const char* src, int length, char* out, int size )
{
    while ( length > 0 && shar_XmlIsSpace( *src ) )
    {
        src++;
        length--;
    }
    while ( length > 0 && shar_XmlIsSpace( src[ length - 1 ] ) )
    {
        length--;
    }
    static const char* const entities[] = { "&amp;", "&", "&lt;", "<", "&gt;", ">", "&quot;", "\"", "&apos;", "'" };
    int n = 0;
    for ( int i = 0; i < length && n < size - 1; )
    {
        int matched = 0;
        if ( src[ i ] == '&' )
        {
            for ( unsigned k = 0; k < sizeof( entities ) / sizeof( entities[ 0 ] ); k += 2 )
            {
                int len = (int)strlen( entities[ k ] );
                if ( i + len <= length && strncmp( src + i, entities[ k ], len ) == 0 )
                {
                    out[ n++ ] = entities[ k + 1 ][ 0 ];
                    i += len;
                    matched = 1;
                    break;
                }
            }
        }
        if ( !matched )
        {
            out[ n++ ] = src[ i++ ];
        }
    }
    if ( size > 0 )
    {
        out[ n ] = '\0';
    }
}

/* An attribute's value (case-insensitive name) into out; 0 when the element does not have it. */
static inline int shar_XmlAttr( const SharXmlElement* e, const char* name, char* out, int size )
{
    int nameLength = (int)strlen( name );
    const char* p = e->attrs;
    const char* end = e->attrs + e->attrsLength;
    while ( p < end )
    {
        while ( p < end && shar_XmlIsSpace( *p ) )
        {
            p++;
        }
        const char* key = p;
        while ( p < end && *p != '=' && !shar_XmlIsSpace( *p ) )
        {
            p++;
        }
        int keyLength = (int)( p - key );
        while ( p < end && ( shar_XmlIsSpace( *p ) || *p == '=' ) )
        {
            p++;
        }
        if ( p >= end || ( *p != '"' && *p != '\'' ) )
        {
            break;
        }
        char quote = *p++;
        const char* value = p;
        while ( p < end && *p != quote )
        {
            p++;
        }
        if ( keyLength == nameLength && strncasecmp( key, name, nameLength ) == 0 )
        {
            shar_XmlCopy( value, (int)( p - value ), out, size );
            return 1;
        }
        p++;
    }
    return 0;
}

/* The element's text (trimmed, entities decoded) into out. */
static inline void shar_XmlText( const SharXmlElement* e, char* out, int size )
{
    shar_XmlCopy( e->text, e->textLength, out, size );
}

#endif
