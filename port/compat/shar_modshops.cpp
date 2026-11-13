// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Mod Launcher CustomShopSupport phone booth / skin shop entries (see shar_modshops.h). */
#include "shar_modshops.h"
#include "shar_mods.h"
#include "shar_modxml.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

namespace
{

const int MAX_SHOPS = 16;
const int MAX_SELECTORS = 8;

struct Selector
{
    int level;
    char locator[ 64 ];         /* "" = any in the level */
};

struct ShopEntry
{
    int kind;
    int numSelectors;
    Selector selectors[ MAX_SELECTORS ];
    SharShop shop;
};

ShopEntry s_entries[ MAX_SHOPS ];
int s_numEntries = 0;
bool s_loaded = false;
void* s_current[ 2 ];

bool IsTrue( const char* value )
{
    return strcasecmp( value, "true" ) == 0 || strcmp( value, "1" ) == 0 || strcasecmp( value, "yes" ) == 0;
}

void AddItem( SharShopItem* items, int* count, const SharXmlElement* e )
{
    if( *count >= SHAR_SHOP_MAX_ITEMS )
    {
        return;
    }
    SharShopItem* item = &items[ *count ];
    shar_XmlText( e, item->name, sizeof( item->name ) );
    if( item->name[ 0 ] == '\0' )
    {
        return;
    }
    if( !shar_XmlAttr( e, "Path", item->path, sizeof( item->path ) ) )
    {
        item->path[ 0 ] = '\0';
    }
    for( char* c = item->path; *c != '\0'; c++ )
    {
        if( *c == '/' )
        {
            *c = '\\';
        }
    }
    char cost[ 16 ];
    item->repairCost = shar_XmlAttr( e, "RepairCost", cost, sizeof( cost ) ) ? atoi( cost ) : -1;
    ( *count )++;
}

void Load()
{
    s_loaded = true;
    char* text = shar_ModConfig( "CustomShopSupport.xml" );
    if( text == NULL )
    {
        return;
    }

    enum { LIST_NONE, LIST_FREE, LIST_INCLUDED, LIST_EXCLUDED };
    ShopEntry* entry = NULL;
    int list = LIST_NONE;
    SharXmlReader reader = { text, 0 };
    SharXmlElement e;
    while( shar_XmlNext( &reader, &e ) )
    {
        if( e.depth == 1 )
        {
            entry = NULL;
            int kind = shar_XmlIs( &e, "PhoneBooth" ) ? SHAR_SHOP_PHONE_BOOTH : shar_XmlIs( &e, "SkinShop" ) ? SHAR_SHOP_SKIN : -1;
            if( kind < 0 || s_numEntries >= MAX_SHOPS )
            {
                continue;
            }
            entry = &s_entries[ s_numEntries++ ];
            memset( entry, 0, sizeof( *entry ) );
            entry->kind = kind;
            shar_XmlAttr( &e, "Drawable", entry->shop.drawable, sizeof( entry->shop.drawable ) );
            char value[ 16 ];
            entry->shop.hideLocked = shar_XmlAttr( &e, "HideLocked", value, sizeof( value ) ) && IsTrue( value );
        }
        else if( entry != NULL && e.depth == 2 )
        {
            list = LIST_NONE;
            if( shar_XmlIs( &e, "Selector" ) )
            {
                char level[ 16 ];
                if( entry->numSelectors < MAX_SELECTORS && shar_XmlAttr( &e, "Level", level, sizeof( level ) ) )
                {
                    Selector* s = &entry->selectors[ entry->numSelectors++ ];
                    s->level = atoi( level );
                    if( !shar_XmlAttr( &e, "Locator", s->locator, sizeof( s->locator ) ) )
                    {
                        s->locator[ 0 ] = '\0';
                    }
                }
            }
            else if( shar_XmlIs( &e, "FreeItems" ) )
            {
                list = LIST_FREE;
            }
            else if( shar_XmlIs( &e, "IncludedItems" ) || shar_XmlIs( &e, "Whitelist" ) )
            {
                list = LIST_INCLUDED;
                entry->shop.hasIncluded = 1;
            }
            else if( shar_XmlIs( &e, "ExcludedItems" ) || shar_XmlIs( &e, "Blacklist" ) )
            {
                list = LIST_EXCLUDED;
            }
        }
        else if( entry != NULL && e.depth == 3 && list != LIST_NONE &&
                 shar_XmlIs( &e, entry->kind == SHAR_SHOP_PHONE_BOOTH ? "Car" : "Skin" ) )
        {
            SharShop* shop = &entry->shop;
            if( list == LIST_FREE )
            {
                AddItem( shop->free, &shop->numFree, &e );
            }
            else if( list == LIST_INCLUDED )
            {
                AddItem( shop->included, &shop->numIncluded, &e );
            }
            else
            {
                AddItem( shop->excluded, &shop->numExcluded, &e );
            }
        }
    }
    free( text );

    if( getenv( "SHAR_MOD_LOG" ) != NULL )
    {
        for( int i = 0; i < s_numEntries; i++ )
        {
            const SharShop* shop = &s_entries[ i ].shop;
            fprintf( stderr, "mods: CustomShopSupport %s %d: %d selector(s), %d free, %s%d included, %d excluded\n",
                     s_entries[ i ].kind == SHAR_SHOP_PHONE_BOOTH ? "PhoneBooth" : "SkinShop", i,
                     s_entries[ i ].numSelectors, shop->numFree, shop->hasIncluded ? "" : "(all) ", shop->numIncluded,
                     shop->numExcluded );
        }
    }
}

}

const SharShop* shar_ModShop( int kind, int level, SharShopLocatorMatch match, void* context )
{
    if( !s_loaded )
    {
        Load();
    }
    const SharShop* levelMatch = NULL;
    for( int i = 0; i < s_numEntries; i++ )
    {
        if( s_entries[ i ].kind != kind )
        {
            continue;
        }
        for( int j = 0; j < s_entries[ i ].numSelectors; j++ )
        {
            const Selector* s = &s_entries[ i ].selectors[ j ];
            if( s->level != level )
            {
                continue;
            }
            if( s->locator[ 0 ] == '\0' )
            {
                if( levelMatch == NULL )
                {
                    levelMatch = &s_entries[ i ].shop;
                }
            }
            else if( match != NULL && match( s->locator, context ) )
            {
                return &s_entries[ i ].shop;
            }
        }
    }
    return levelMatch;
}

int shar_ModShopHas( const SharShopItem* items, int count, const char* name )
{
    for( int i = 0; i < count; i++ )
    {
        if( strcasecmp( items[ i ].name, name ) == 0 )
        {
            return 1;
        }
    }
    return 0;
}

void shar_ModShopSetCurrent( int kind, void* locator )
{
    if( kind == SHAR_SHOP_PHONE_BOOTH || kind == SHAR_SHOP_SKIN )
    {
        s_current[ kind ] = locator;
    }
}

void* shar_ModShopCurrent( int kind )
{
    return ( kind == SHAR_SHOP_PHONE_BOOTH || kind == SHAR_SHOP_SKIN ) ? s_current[ kind ] : NULL;
}
