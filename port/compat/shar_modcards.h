/* Mod Launcher CustomCollectorCardSupport: a mod's CustomCollectorCardSupport.xml (port/compat/shar_modcards.cpp)
 *
 *   <CustomCollectorCardSupport>
 *     <CustomCharacters><CustomCharacter Name="Loren" Index="32" /></CustomCharacters>   quote icon names
 *     <Level Number="1">                              (or Index="0"; levels 1-7)
 *       <CollectorCard Number="1" Drawable="phone_icon" CollectDrawable="bonestorm_explosion">   (or Index; 1-7)
 *         <Quotes>                                    replaces every game quote; empty: none
 *           <Quote Name="Homer" />                    up to 3; Name (the game's 31 + custom, any case) or Index
 *         </Quotes>
 *       </CollectorCard>
 *     </Level>
 *   </CustomCollectorCardSupport>
 *
 * Drawables: the world card (default card_idle) and its collection effect (card_collect), actionbuttonhandler.cpp.
 * Quotes: the card's quote icons (frontend/ingame ViewCard Quote0-2 multi sprite indices), cardsdb.cpp; quote text
 * stays the card's own. Only read when a mod requires the hack. Read once, on first use; static storage only. */
#ifndef SHAR_COMPAT_SHAR_MODCARDS_H
#define SHAR_COMPAT_SHAR_MODCARDS_H

#ifdef __cplusplus
extern "C" {
#endif

#define SHAR_MODCARD_MAX_QUOTES 3

typedef struct SharModCard
{
    char drawable[ 64 ];        /* empty: the game's */
    char collectDrawable[ 64 ]; /* empty: the game's */
    int hasQuotes;              /* 0: the game's quotes */
    int numQuotes;
    int quotes[ SHAR_MODCARD_MAX_QUOTES ];
} SharModCard;

/* The mod's settings for collector card cardIndex (0-6) of level (0-6), or NULL. */
const SharModCard* shar_ModCollectorCard( int level, int cardIndex );

#ifdef __cplusplus
}
#endif

#endif
