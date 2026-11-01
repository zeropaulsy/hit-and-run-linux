//=============================================================================
// Linux port: Mod Launcher CustomStatsTotals (see modstatstotals.h). Keys and defaults follow the hack's
// documentation (Mod Launcher 1.27.1).
//=============================================================================
#ifdef RAD_LINUX

#include <stdio.h>
#include <stdlib.h>

#include <mission/charactersheet/modstatstotals.h>
#include <mission/charactersheet/charactersheetmanager.h>
#include <mission/rewards/reward.h>
#include <mission/rewards/merchandise.h>
#include <mission/rewards/rewardsmanager.h>
#include <cards/cardgallery.h>
#include <render/Enums/RenderEnums.h>

#include <shar_mods.h>

namespace
{

const char* const FILE_NAME = "CustomStatsTotals.ini";

struct LevelTotals
{
    int storyMissions;
    int bonusMissions;
    int streetRaces;
    int cards;
    int movies;
    bool movieInTotal;
};

bool s_Loaded = false;
bool s_Active = false;
int s_Levels = RenderEnums::numLevels;
bool s_RemoveUnused = true;
LevelTotals s_Level[ RenderEnums::numLevels ];

int Clamp( int value, int low, int high )
{
    return value < low ? low : ( value > high ? high : value );
}

void Load()
{
    if( s_Loaded )
    {
        return;
    }
    s_Loaded = true;
    s_Active = shar_ModHackRequired( "CustomStatsTotals" ) != 0;
    if( !s_Active )
    {
        return;
    }

    s_Levels = Clamp( shar_ModConfigInt( FILE_NAME, "Miscellaneous", "Levels", RenderEnums::numLevels ),
                      1, RenderEnums::numLevels );
    s_RemoveUnused = shar_ModConfigInt( FILE_NAME, "Miscellaneous", "RemoveUnusedMissionFromMissionSelect", 1 ) != 0;

    for( int i = 0; i < RenderEnums::numLevels; i++ )
    {
        char section[ 16 ];
        snprintf( section, sizeof( section ), "Level%d", i + 1 );
        LevelTotals& t = s_Level[ i ];
        t.storyMissions = Clamp( shar_ModConfigInt( FILE_NAME, section, "StoryMissions", 7 ), 0, 7 );
        t.bonusMissions = Clamp( shar_ModConfigInt( FILE_NAME, section, "BonusMissions", 1 ), 0, 1 );
        t.streetRaces = Clamp( shar_ModConfigInt( FILE_NAME, section, "StreetRaces", 3 ), 0, 3 );
        t.cards = Clamp( shar_ModConfigInt( FILE_NAME, section, "CollectorCards", 7 ), 0, 7 );
        t.movies = Clamp( shar_ModConfigInt( FILE_NAME, section, "Movies", 1 ), 0, 1 );
        t.movieInTotal = t.movies > 0 &&
                         shar_ModConfigInt( FILE_NAME, section, "IncludeMovieInTotal", i == RenderEnums::L3 ? 1 : 0 ) != 0;
    }

    if( getenv( "SHAR_MOD_LOG" ) != NULL )
    {
        fprintf( stderr, "mods: CustomStatsTotals: %d level(s), unused missions %s\n", s_Levels,
                 s_RemoveUnused ? "removed from mission select" : "kept" );
        for( int i = 0; i < s_Levels; i++ )
        {
            const LevelTotals& t = s_Level[ i ];
            fprintf( stderr, "mods: CustomStatsTotals: level %d: missions %d, bonus %d, races %d, cards %d, movies %d%s\n",
                     i + 1, t.storyMissions, t.bonusMissions, t.streetRaces, t.cards, t.movies,
                     t.movieInTotal ? " (in game total)" : "" );
        }
    }
}

bool InMod( int level )
{
    Load();
    return s_Active && level >= 0 && level < s_Levels;
}

bool RewardsReady()
{
    return GetRewardsManager() != NULL && GetRewardsManager()->ScriptLoaded();
}

// How many of the level's token-store items are of that kind (as the scrapbook galleries list them)
int CountMerchandise( int level, bool cars )
{
    if( !RewardsReady() )
    {
        return 0;
    }
    TokenStoreInventory* inventory = GetRewardsManager()->GetTokenStoreInventoryList( level );
    if( inventory == NULL )
    {
        return 0;
    }
    int count = 0;
    for( int i = 0; i < inventory->mCounter; i++ )
    {
        Merchandise* item = inventory->mInventoryList[ i ];
        if( item == NULL )
        {
            continue;
        }
        Merchandise::eSellerType seller = item->GetSellerType();
        if( cars ? ( seller == Merchandise::SELLER_GIL || seller == Merchandise::SELLER_SIMPSON )
                 : seller == Merchandise::SELLER_INTERIOR )
        {
            count++;
        }
    }
    return count;
}

// not capped at 1, as the original (a save with more than the totals goes past 100%)
float Fraction( int done, int total )
{
    return static_cast<float>( done ) / static_cast<float>( total );
}

} // namespace

namespace ModStatsTotals
{

bool Active()
{
    Load();
    return s_Active;
}

int Levels()
{
    Load();
    return s_Active ? s_Levels : RenderEnums::numLevels;
}

bool RemoveUnusedMissions()
{
    Load();
    return s_Active && s_RemoveUnused;
}

int StoryMissions( int level ) { return InMod( level ) ? s_Level[ level ].storyMissions : 0; }
int BonusMissions( int level ) { return InMod( level ) ? s_Level[ level ].bonusMissions : 0; }
int StreetRaces( int level ) { return InMod( level ) ? s_Level[ level ].streetRaces : 0; }
int CollectorCards( int level ) { return InMod( level ) ? s_Level[ level ].cards : 0; }
int Movies( int level ) { return InMod( level ) ? s_Level[ level ].movies : 0; }
bool MovieInTotal( int level ) { return InMod( level ) && s_Level[ level ].movieInTotal; }

int Wasps( int level )
{
    if( !InMod( level ) )
    {
        return 0;
    }
    return GetRewardsManager() != NULL ? GetRewardsManager()->GetTotalWasps( level ) : 0;
}

int Gags( int level )
{
    if( !InMod( level ) )
    {
        return 0;
    }
    return GetRewardsManager() != NULL ? GetRewardsManager()->GetTotalGags( level ) : 0;
}

int Skins( int level )
{
    return InMod( level ) ? CountMerchandise( level, false ) : 0;
}

int Cars( int level )
{
    if( !InMod( level ) || !RewardsReady() )
    {
        return 0;
    }
    int count = CountMerchandise( level, true );
    // the earnable cars (the vehicle gallery's quest rewards, CustomRewardQuestSupport's included), only for the
    // races / bonus mission the level has
    for( int i = 0; i < GetRewardsManager()->GetNumRewards( level ); i++ )
    {
        Reward* reward = GetRewardsManager()->GetRewardAt( level, i );
        if( reward == NULL || reward->GetRewardType() != Reward::ALT_PLAYERCAR )
        {
            continue;
        }
        Reward::eQuestType q = reward->GetQuestType();
        if( q == Reward::eDefaultCar ||
            ( q == Reward::eStreetRace && StreetRaces( level ) == 0 ) ||
            ( q == Reward::eBonusMission && BonusMissions( level ) == 0 ) )
        {
            continue;
        }
        count++;
    }
    return count;
}

int DefaultCars( int level )
{
    if( !InMod( level ) || !RewardsReady() )
    {
        return 0;
    }
    int count = 0;
    for( int i = 0; i < GetRewardsManager()->GetNumRewards( level ); i++ )
    {
        Reward* reward = GetRewardsManager()->GetRewardAt( level, i );
        if( reward != NULL && reward->GetQuestType() == Reward::eDefaultCar &&
            reward->GetRewardType() == Reward::ALT_PLAYERCAR )
        {
            count++;
        }
    }
    return count;
}

float PercentLevelCompleted( int level )
{
    if( !InMod( level ) )
    {
        return 0.0f;
    }
    CharacterSheetManager* sheet = GetCharacterSheetManager();
    RenderEnums::LevelEnum l = static_cast<RenderEnums::LevelEnum>( level );

    // the original's eight parts (missions, bonus mission, street races, clothing, cars, cards, wasps, gags),
    // each a fraction of its total; parts with no total do not count
    const int done[] = { sheet->QueryNumMissionsCompleted( l ), sheet->QueryNumBonusMissionsCompleted( l ),
                         sheet->QueryNumStreetRacesCompleted( l ), sheet->QueryNumSkinsUnlocked( l ),
                         sheet->QueryNumCarUnlocked( l ), sheet->QueryNumCardsCollected( l ),
                         sheet->QueryNumWaspsDestroyed( l ), sheet->QueryNumGagsViewed( l ) };
    const int total[] = { StoryMissions( level ), BonusMissions( level ), StreetRaces( level ), Skins( level ),
                          Cars( level ), CollectorCards( level ), Wasps( level ), Gags( level ) };
    float sum = 0.0f;
    int parts = 0;
    for( unsigned i = 0; i < sizeof( total ) / sizeof( total[ 0 ] ); i++ )
    {
        if( total[ i ] > 0 )
        {
            sum += Fraction( done[ i ], total[ i ] );
            parts++;
        }
    }
    return parts > 0 ? sum / parts * 100.0f : 0.0f;
}

float PercentGameCompleted()
{
    // levels average to (100 - n)%, each of the n movies counted in the game total adds 1% (the original: the
    // levels 99%, level 3's movie 1%); levels with no totals at all do not count
    CharacterSheetManager* sheet = GetCharacterSheetManager();
    float levels = 0.0f;
    int numLevels = 0;
    int movies = 0;
    int moviesUnlocked = 0;
    for( int i = 0; i < Levels(); i++ )
    {
        if( StoryMissions( i ) + BonusMissions( i ) + StreetRaces( i ) + Skins( i ) + Cars( i ) +
            CollectorCards( i ) + Wasps( i ) + Gags( i ) > 0 )
        {
            levels += PercentLevelCompleted( i );
            numLevels++;
        }
        if( MovieInTotal( i ) )
        {
            movies++;
            if( sheet->QueryFMVUnlocked( static_cast<RenderEnums::LevelEnum>( i ) ) )
            {
                moviesUnlocked++;
            }
        }
    }
    if( numLevels == 0 )
    {
        return movies > 0 ? 100.0f * moviesUnlocked / movies : 0.0f;
    }
    return levels / numLevels * ( 1.0f - 0.01f * movies ) + static_cast<float>( moviesUnlocked );
}

} // namespace ModStatsTotals

#endif // RAD_LINUX
