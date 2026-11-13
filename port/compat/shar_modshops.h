// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Mod Launcher CustomShopSupport: the <PhoneBooth> and <SkinShop> entries of a mod's CustomShopSupport.xml
 * (port/compat/shar_modshops.cpp; <CarShop> is read in code/ai/actionbuttonhandler.cpp).
 *
 *   <PhoneBooth Drawable="wrench" HideLocked="true">       (SkinShop: Drawable only)
 *       <Selector Level="1" Locator="Z1p1" />              (one or more; Locator optional)
 *       <FreeItems><Car Path="..." RepairCost="50">bart_v</Car></FreeItems>     (SkinShop: <Skin>)
 *       <IncludedItems><Car>...</Car></IncludedItems>      (only these of the game's own items; empty = none)
 *       <ExcludedItems><Car>...</Car></ExcludedItems>
 *   </PhoneBooth>
 *
 * Whitelist/Blacklist are the old names of IncludedItems/ExcludedItems. A booth or shop takes the entry with a
 * selector naming its level and locator, else the first one naming only its level.
 * Read once, on first use; static storage only. */
#ifndef SHAR_COMPAT_SHAR_MODSHOPS_H
#define SHAR_COMPAT_SHAR_MODSHOPS_H

#ifdef __cplusplus
extern "C" {
#endif

enum { SHAR_SHOP_PHONE_BOOTH = 0, SHAR_SHOP_SKIN = 1 };

typedef struct SharShopItem
{
    char name[ 32 ];
    char path[ 128 ];           /* "" = the game's own file for that name */
    int repairCost;             /* -1 = not given */
} SharShopItem;

#define SHAR_SHOP_MAX_ITEMS 32

typedef struct SharShop
{
    char drawable[ 64 ];        /* "" = the game's icon */
    int hideLocked;
    int hasIncluded;            /* an IncludedItems element was given (possibly empty) */
    int numIncluded, numExcluded, numFree;
    SharShopItem included[ SHAR_SHOP_MAX_ITEMS ];
    SharShopItem excluded[ SHAR_SHOP_MAX_ITEMS ];
    SharShopItem free[ SHAR_SHOP_MAX_ITEMS ];
} SharShop;

/* Whether the booth/shop is at the locator a selector names. */
typedef int ( *SharShopLocatorMatch )( const char* selectorLocator, void* context );

/* The entry for a phone booth or skin shop in level (1-7) whose locator match() accepts; NULL when no mod sets
 * one. */
const SharShop* shar_ModShop( int kind, int level, SharShopLocatorMatch match, void* context );

/* Whether name is among items[0, count) (case-insensitive). */
int shar_ModShopHas( const SharShopItem* items, int count, const char* name );

/* The locator (game object, opaque here) of the booth/shop whose menu was opened last, set when its button is
 * pressed; NULL before. */
void shar_ModShopSetCurrent( int kind, void* locator );
void* shar_ModShopCurrent( int kind );

#ifdef __cplusplus
}
#endif

#endif
