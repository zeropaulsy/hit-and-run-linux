//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   CardsDB
//
// Description: Implementation of the CardsDB class.
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2002/08/06      TChu        Created for SRR2
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================
#include <cards/cardsdb.h>
#include <cards/collectorcard.h>
#include <cards/bonuscard.h>

// cards database header file
#include <cards/cards.h>
#ifdef RAD_LINUX
#include <shar_modcards.h>
#endif

#include <memory/srrmemory.h>

#include <p3d/entity.hpp>

//===========================================================================
// Local Constants
//===========================================================================

//===========================================================================
// Public Member Functions
//===========================================================================

//===========================================================================
// CardsDB::CardsDB
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      
//
//===========================================================================
CardsDB::CardsDB()
:   m_cards( NULL ),
    m_numCards( 0 )
{
MEMTRACK_PUSH_GROUP( "CardsDB" );

    #ifdef RAD_GAMECUBE
        HeapMgr()->PushHeap( GMA_GC_VMM );
    #else
        HeapMgr()->PushHeap( GMA_PERSISTENT );
    #endif

    m_cards = new Card*[ MAX_NUM_CARDS ];
    rAssert( m_cards );

    for( unsigned int i = 0; i < MAX_NUM_CARDS; i++ )
    {
        m_cards[ i ] = NULL;
    }

    #ifdef RAD_GAMECUBE
        HeapMgr()->PopHeap( GMA_GC_VMM );
    #else
        HeapMgr()->PopHeap( GMA_PERSISTENT );
    #endif
MEMTRACK_POP_GROUP( "CardsDB" );
}

//===========================================================================
// CardsDB::~CardsDB
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      
//
//===========================================================================
CardsDB::~CardsDB()
{
    // Clean-up cards DB memory
    //
    if( m_cards != NULL )
    {
        for( unsigned int i = 0; i < MAX_NUM_CARDS; i++ )
        {
            if( m_cards[ i ] != NULL )
            {
                delete m_cards[ i ];
                m_cards[ i ] = NULL;
            }
        }

        delete [] m_cards;
        m_cards = NULL;
    }
}

//===========================================================================
// CardsDB::LoadCards
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      
//
//===========================================================================
void
CardsDB::LoadCards()
{
    // load cards from memory (static array in Cards.h header file)
    //
    for( unsigned int i = 0; i < s_numCardsInDB; i++ )
    {
        int cardID = s_cardsDB[ i ].imageID;

        if( s_cardsDB[ i ].type == COLLECTOR_CARD )
        {
            const eQuoteID* quotes = s_cardsDB[ i ].quotes;
#ifdef RAD_LINUX
            // Linux port: mods' CustomCollectorCardSupport <Quotes>: the card's quote characters, all replaced
            eQuoteID modQuotes[ MAX_NUM_QUOTES ];
            const SharModCard* modCard = shar_ModCollectorCard( s_cardsDB[ i ].level - 1, s_cardsDB[ i ].levelID - 1 );
            if( modCard != NULL && modCard->hasQuotes )
            {
                for( unsigned int q = 0; q < MAX_NUM_QUOTES; q++ )
                {
                    modQuotes[ q ] = static_cast< int >( q ) < modCard->numQuotes ?
                                     static_cast< eQuoteID >( modCard->quotes[ q ] ) : EMPTY_QUOTE;
                }
                quotes = modQuotes;
            }
#endif
            // create new collector card
            //
            m_cards[ cardID ] = new CollectorCard( cardID,
                                                   s_cardsDB[ i ].level,
                                                   s_cardsDB[ i ].levelID,
                                                   tEntity::MakeUID( s_cardsDB[ i ].name ),
                                                   quotes,
                                                   MAX_NUM_QUOTES );
        }
        else if( s_cardsDB[ i ].type == BONUS_CARD )
        {
            // create new bonus card
            //
            m_cards[ cardID ] = new BonusCard( cardID,
                                               s_cardsDB[ i ].level,
                                               s_cardsDB[ i ].levelID,
                                               tEntity::MakeUID( s_cardsDB[ i ].name ),
                                               s_cardsDB[ i ].quotes,
                                               MAX_NUM_QUOTES );
        }
        else
        {
            rAssertMsg( 0, "ERROR: *** Invalid card type in database file!\n" );
        }

        m_numCards++;
    }
}

//===========================================================================
// CardsDB::GetCardByID
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      
//
//===========================================================================
Card*
CardsDB::GetCardByID( unsigned int cardID )
{
    rAssert( cardID < MAX_NUM_CARDS );

    return m_cards[ cardID ];
}

//===========================================================================
// CardsDB::GetCardByName
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      
//
//===========================================================================
Card*
CardsDB::GetCardByName( tUID cardName )
{
    Card* card = NULL;

    // search for card with cardName
    for( unsigned int i = 0; i < MAX_NUM_CARDS; i++ )
    {
        if( m_cards[ i ] != NULL )
        {
            if( m_cards[ i ]->GetCardName() == cardName )
            {
                // found it!
                card = m_cards[ i ];
                break;
            }
        }
    }

    return card;
}

//===========================================================================
// Private Member Functions
//===========================================================================

