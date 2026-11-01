//=============================================================================
// Linux port: Mod Launcher CustomRewardQuestSupport. With a mod requiring the hack:
// - BindReward keeps every binding (RewardsManager::BindModReward): several rewards per quest type, skins for any
//   quest but defaultcar, an extra argument: cards / streetrace / mission number, wager time (seconds, beaten
//   below it), coins / maxcoins amount, hitandrun / evaded / busted count, getin car / doorbell character name.
// - New quest types: wasps, gags, mission, wager, coins (relocks below the amount), hitandrun, evaded, busted,
//   getin, doorbell (the locator's DB_<name>), maxcoins. Levels are the rewards' (counted while playing there).
// - Rewards unlock while playing (Update), with a "quest complete" HUD message ([ShowQuestComplete] <Type>=0/1 in
//   CustomRewardQuestSupport.ini; default on but StreetRace / BonusMission), QUEST_COMPLETE_<NAME> /
//   QUEST_COMPLETE_<TYPE> / QUEST_COMPLETE strings. Locked quest rewards: phone booth CompleteQuest text (a mod's
//   ingame.p3d), skin shop Price / ToPurchase: COMPLETE_QUEST_<NAME> / _<TYPE> / COMPLETE_QUEST, TO_UNLOCK.
// - Progress the character sheet doesn't hold (hit & run counts, getin / doorbell / maxcoins done) is saved in the
//   save's sidecar (CharacterSheetManager::ModExtraWrite).
//=============================================================================
#ifndef MODREWARDQUESTS_H
#define MODREWARDQUESTS_H

#ifdef RAD_LINUX

#include <stdio.h>
#include <mission/rewards/reward.h>

class UnicodeString;

namespace ModRewardQuests
{
    bool Active();

    // a BindReward quest name the hack adds ("wasps" ...): its type, else Reward::eBlank
    Reward::eQuestType QuestType( const char* name );

    // every quest reward locked / unlocked from the character sheet and the saved progress (a save loaded)
    void Synch();
    // while playing level (0-based), each frame: quests done since unlock (with their messages), coins relock
    void Update( int level );

    // saved progress (CharacterSheetManager's sidecar)
    void Clear();
    bool HasSaveData();
    void Write( FILE* file );
    bool ReadLine( const char* line );

    // texts for a locked quest reward (phone booth, skin shop)
    bool IsQuestReward( Reward* reward );
    void CompleteQuestText( Reward* reward, UnicodeString& text );
    void ToUnlockText( UnicodeString& text );
}

#endif // RAD_LINUX

#endif // MODREWARDQUESTS_H
