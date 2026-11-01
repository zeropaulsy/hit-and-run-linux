/* Mod Launcher CustomCollectorCardSupport.xml (see shar_modcards.h). */
#include "shar_modcards.h"
#include "shar_mods.h"
#include "shar_modxml.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace
{

const int NUM_LEVELS = 7;
const int CARDS_PER_LEVEL = 7;
const int MAX_CUSTOM_CHARACTERS = 64;

SharModCard s_cards[ NUM_LEVELS ][ CARDS_PER_LEVEL ];
bool s_hasCard[ NUM_LEVELS ][ CARDS_PER_LEVEL ];
pthread_once_t s_once = PTHREAD_ONCE_INIT;

struct Character
{
    char name[ 32 ];
    int index;
};

// the game's quote characters (eQuoteID, card.h), with the launcher's alternative spellings
const Character DEFAULT_CHARACTERS[] =
{
    { "ANNOUNCER", 0 }, { "APU", 1 }, { "BART", 2 }, { "BROCKMAN", 3 }, { "BURNS", 4 }, { "CARL", 5 },
    { "CHILD", 6 }, { "DRWOLFF", 7 }, { "DR WOLFF", 7 }, { "DR. WOLFF", 7 }, { "GIL", 8 }, { "HOMER", 9 },
    { "JASPER", 10 }, { "JIMBO", 11 }, { "KANG", 12 }, { "KRUSTY", 13 }, { "LENNY", 14 }, { "LISA", 15 },
    { "MAGGIE", 16 }, { "MANJULA", 17 }, { "MARGE", 18 }, { "MEYERS", 19 }, { "MILHOUSE", 20 }, { "MOTHER", 21 },
    { "MRSPARKLE", 22 }, { "MR SPARKLE", 22 }, { "MR. SPARKLE", 22 }, { "OTTO", 23 }, { "PHOTOGRAPHER", 24 },
    { "RALPH", 25 }, { "SKINNER", 26 }, { "SMITHERS", 27 }, { "STACY", 28 }, { "WIGGUM", 29 }, { "WILLIE", 30 }
};

Character s_custom[ MAX_CUSTOM_CHARACTERS ];
int s_numCustom = 0;

int CharacterIndex( const char* name )
{
    for( int i = 0; i < s_numCustom; i++ )
    {
        if( strcasecmp( s_custom[ i ].name, name ) == 0 )
        {
            return s_custom[ i ].index;
        }
    }
    for( size_t i = 0; i < sizeof( DEFAULT_CHARACTERS ) / sizeof( DEFAULT_CHARACTERS[ 0 ] ); i++ )
    {
        if( strcasecmp( DEFAULT_CHARACTERS[ i ].name, name ) == 0 )
        {
            return DEFAULT_CHARACTERS[ i ].index;
        }
    }
    return -1;
}

// Index="0-based" or Number="1-based"; -1 when neither
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

void Load()
{
    if( !shar_ModHackRequired( "CustomCollectorCardSupport" ) )
    {
        return;
    }
    char* text = shar_ModConfig( "CustomCollectorCardSupport.xml" );
    if( text == NULL )
    {
        return;
    }

    // custom characters first: quotes may come before their declaration
    SharXmlReader reader = { text, 0 };
    SharXmlElement e;
    while( shar_XmlNext( &reader, &e ) )
    {
        char value[ 16 ];
        if( shar_XmlIs( &e, "CustomCharacter" ) && s_numCustom < MAX_CUSTOM_CHARACTERS &&
            shar_XmlAttr( &e, "Name", s_custom[ s_numCustom ].name, sizeof( s_custom[ 0 ].name ) ) &&
            shar_XmlAttr( &e, "Index", value, sizeof( value ) ) && value[ 0 ] != '\0' )
        {
            s_custom[ s_numCustom++ ].index = atoi( value );
        }
    }

    bool log = getenv( "SHAR_MOD_LOG" ) != NULL;
    SharXmlReader cards = { text, 0 };
    int level = -1, levelDepth = -1;
    SharModCard* card = NULL;
    int cardDepth = -1;
    while( shar_XmlNext( &cards, &e ) )
    {
        if( e.depth <= cardDepth )
        {
            card = NULL;
            cardDepth = -1;
        }
        if( e.depth <= levelDepth )
        {
            level = -1;
            levelDepth = -1;
        }
        if( shar_XmlIs( &e, "Level" ) )
        {
            level = IndexOrNumber( &e );
            levelDepth = e.depth;
            if( level < 0 || level >= NUM_LEVELS )
            {
                fprintf( stderr, "mods: CustomCollectorCardSupport.xml: bad level, ignored\n" );
                level = -1;
            }
        }
        else if( shar_XmlIs( &e, "CollectorCard" ) && level >= 0 )
        {
            int index = IndexOrNumber( &e );
            if( index < 0 || index >= CARDS_PER_LEVEL )
            {
                fprintf( stderr, "mods: CustomCollectorCardSupport.xml: bad card in level %d, ignored\n", level + 1 );
                continue;
            }
            card = &s_cards[ level ][ index ];
            memset( card, 0, sizeof( *card ) );
            s_hasCard[ level ][ index ] = true;
            cardDepth = e.depth;
            shar_XmlAttr( &e, "Drawable", card->drawable, sizeof( card->drawable ) );
            shar_XmlAttr( &e, "CollectDrawable", card->collectDrawable, sizeof( card->collectDrawable ) );
            if( log )
            {
                fprintf( stderr, "mods: collector card L%d-%d drawable %s collect %s\n", level + 1, index + 1,
                         card->drawable[ 0 ] != '\0' ? card->drawable : "(game's)",
                         card->collectDrawable[ 0 ] != '\0' ? card->collectDrawable : "(game's)" );
            }
        }
        else if( shar_XmlIs( &e, "Quotes" ) && card != NULL )
        {
            card->hasQuotes = 1;
        }
        else if( shar_XmlIs( &e, "Quote" ) && card != NULL && card->hasQuotes )
        {
            char name[ 32 ];
            int index = -1;
            if( shar_XmlAttr( &e, "Name", name, sizeof( name ) ) && name[ 0 ] != '\0' )
            {
                index = CharacterIndex( name );
                if( index < 0 )
                {
                    fprintf( stderr, "mods: CustomCollectorCardSupport.xml: unknown quote character %s\n", name );
                }
            }
            else if( shar_XmlAttr( &e, "Index", name, sizeof( name ) ) && name[ 0 ] != '\0' )
            {
                index = atoi( name );
            }
            if( index >= 0 && card->numQuotes < SHAR_MODCARD_MAX_QUOTES )
            {
                card->quotes[ card->numQuotes++ ] = index;
            }
        }
    }
    free( text );
}

}

extern "C" const SharModCard* shar_ModCollectorCard( int level, int cardIndex )
{
    pthread_once( &s_once, Load );
    if( level < 0 || level >= NUM_LEVELS || cardIndex < 0 || cardIndex >= CARDS_PER_LEVEL ||
        !s_hasCard[ level ][ cardIndex ] )
    {
        return NULL;
    }
    return &s_cards[ level ][ cardIndex ];
}
