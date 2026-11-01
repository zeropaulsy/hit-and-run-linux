//=============================================================================
// Linux port: Mod Launcher CustomStatsTotals. A mod with RequiredHack=CustomStatsTotals sets the number of levels
// and each level's totals in CustomStatsTotals.ini:
//   [Miscellaneous] Levels (1-7, default 7), RemoveUnusedMissionFromMissionSelect (default 1)
//   [LevelX]        StoryMissions (0-7, default 7; 0 = no mission select there), BonusMissions (0-1, 1),
//                   StreetRaces (0-3, 3), CollectorCards (0-7, 7), Movies (0-1, 1), IncludeMovieInTotal (default 1
//                   for level 3, else 0; 0 when Movies=0).
// Wasp and gag totals are the level scripts' (SetTotalWasps / SetTotalGags); clothing and vehicle totals are counted
// from the rewards the scripts registered (as the scrapbook galleries list them). Levels past Levels have no totals. A total of 0 is left out of the completion percentages.
// Without the hack Active() is false and the game uses its own numbers.
//=============================================================================
#ifndef MODSTATSTOTALS_H
#define MODSTATSTOTALS_H

#ifdef RAD_LINUX

namespace ModStatsTotals
{
    bool Active();

    int  Levels();                  // levels the mod has (1-7)
    bool RemoveUnusedMissions();    // hide mission-select slots past StoryMissions

    // level is 0-based (RenderEnums::LevelEnum); 0 for levels past Levels()
    int  StoryMissions( int level );
    int  BonusMissions( int level );
    int  StreetRaces( int level );
    int  CollectorCards( int level );
    int  Movies( int level );
    bool MovieInTotal( int level );
    int  Wasps( int level );
    int  Gags( int level );
    int  Skins( int level );        // clothing on sale (interior sellers)
    int  Cars( int level );         // earnable + purchasable cars, without the default car (the stats screens' total)
    int  DefaultCars( int level );  // the level's default car rewards that are cars (the scrapbook's vehicle total adds them)

    // completion percentages (0-100) over the totals above, totals of 0 left out
    float PercentLevelCompleted( int level );
    float PercentGameCompleted();
}

#endif // RAD_LINUX

#endif // MODSTATSTOTALS_H
