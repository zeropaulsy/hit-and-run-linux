/* Mod Launcher CustomBonusMissionSupport: a mod's CustomBonusMissionSupport.ini (port/compat/shar_modbonus.cpp)
 *
 *   [Miscellaneous] StreetRaceIntroDialogueMissionIndex=11     dialogue mission of other race hosts' intros
 *   [StreetRaceIntroDialogueMissionIndex] / [...IndexL1-7]     model=9 or 10: the SR1 / SR2 host (dialoglist.cpp)
 *   [L1SR1] ... [L7M7] [L1BM1]  ResetPlayer=, ForcePlayerInCar= (defaults 1 for street races, else 0; mission.cpp,
 *                               missionmanager.cpp)
 *   [StartBitmaps] / [StartBitmapsL1-7]  host NPC name=bitmap at the mission briefing (presentation.cpp)
 *   [EndBitmaps] / [EndBitmapsL1-7]      mission name (bm1, sr2, gr1) or Default=bitmap on the Patty & Selma
 *                                        screen (guiscreenmissionsuccess.cpp)
 *
 * Level sections win over the global ones. Only read when a mod requires the hack. Read once, on first use;
 * static storage only. */
#ifndef SHAR_COMPAT_SHAR_MODBONUS_H
#define SHAR_COMPAT_SHAR_MODBONUS_H

#ifdef __cplusplus
extern "C" {
#endif

/* The dialogue mission (9 or 10) a mod binds the race host's model to in level levelIndex (0-6), else -1. */
int shar_ModBonusRaceIntroIndex( int levelIndex, const char* modelName );

/* The dialogue mission of intros with a host no binding names, else defaultIndex. */
int shar_ModBonusRaceIntroLastIndex( int defaultIndex );

/* Whether loading the mission (its script name, "sr1", "m3") puts the player at its start, and whether that puts
 * the player in the car: the mod's, else defaultValue. */
int shar_ModBonusResetPlayer( int levelIndex, const char* missionName, int defaultValue );
int shar_ModBonusForcePlayerInCar( int levelIndex, const char* missionName, int defaultValue );

/* The briefing bitmaps for missions by host NPC ("b_nelson"; release builds keep only name UIDs, so the caller
 * compares): entry i's bitmap and *hostName, NULL past the last. The level's entries come first; the first entry
 * for a host is the one that counts. */
const char* shar_ModBonusStartBitmapAt( int levelIndex, int i, const char** hostName );

/* The Patty & Selma screen's bitmap after the mission, or NULL for the game's. */
const char* shar_ModBonusEndBitmap( int levelIndex, const char* missionName );

#ifdef __cplusplus
}
#endif

#endif
