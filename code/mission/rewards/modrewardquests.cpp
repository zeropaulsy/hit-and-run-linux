//=============================================================================
// Linux port: Mod Launcher CustomRewardQuestSupport (see modrewardquests.h). Behaviour follows the hack's
// documentation (Mod Launcher 1.27.1).
//=============================================================================
#ifdef RAD_LINUX

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <mission/rewards/modrewardquests.h>
#include <mission/rewards/reward.h>
#include <mission/rewards/rewardsmanager.h>
#include <mission/charactersheet/charactersheetmanager.h>
#include <mission/charactersheet/modstatstotals.h>
#include <mission/gameplaymanager.h>
#include <events/eventlistener.h>
#include <events/eventmanager.h>
#include <memory/srrmemory.h>
#include <presentation/gui/guitextbible.h>
#include <presentation/gui/ingame/guimanageringame.h>
#include <presentation/gui/ingame/guiscreenhud.h>
#include <presentation/gui/ingame/hudevents/hudwaspdestroyed.h>
#include <render/Enums/RenderEnums.h>
#include <worldsim/avatar.h>
#include <worldsim/avatarmanager.h>
#include <worldsim/redbrick/vehicle.h>

#include <strings/unicodeString.h>

#include <shar_mods.h>

namespace
{

const char* const FILE_NAME = "CustomRewardQuestSupport.ini";

struct QuestInfo
{
    Reward::eQuestType type;
    const char* name;       // BindReward's, lower case; upper case in string IDs
    const char* configKey;  // [ShowQuestComplete], NULL: no message
    bool showByDefault;
};

const QuestInfo QUESTS[] =
{
    { Reward::eDefaultCar,   "defaultcar",   NULL,           false },
    { Reward::eDefaultSkin,  "defaultskin",  NULL,           false },
    { Reward::eCards,        "cards",        "Cards",        true },
    { Reward::eStreetRace,   "streetrace",   "StreetRace",   false },
    { Reward::eBonusMission, "bonusmission", "BonusMission", false },
    { Reward::eGoldCards,    "goldcards",    NULL,           false },
    { Reward::eWasps,        "wasps",        "Wasps",        true },
    { Reward::eGags,         "gags",         "Gags",         true },
    { Reward::eMission,      "mission",      "Mission",      true },
    { Reward::eWager,        "wager",        "Wager",        true },
    { Reward::eCoins,        "coins",        "Coins",        true },
    { Reward::eHitAndRun,    "hitandrun",    "HitAndRun",    true },
    { Reward::eEvaded,       "evaded",       "Evaded",       true },
    { Reward::eBusted,       "busted",       "Busted",       true },
    { Reward::eGetIn,        "getin",        "GetIn",        true },
    { Reward::eDoorbell,     "doorbell",     "Doorbell",     true },
    { Reward::eMaxCoins,     "maxcoins",     "MaxCoins",     true },
};
const int NUM_QUEST_INFOS = sizeof( QUESTS ) / sizeof( QUESTS[ 0 ] );

const QuestInfo* Info( Reward::eQuestType type )
{
    for( int i = 0; i < NUM_QUEST_INFOS; i++ )
    {
        if( QUESTS[ i ].type == type )
        {
            return &QUESTS[ i ];
        }
    }
    return NULL;
}

bool s_Loaded = false;
bool s_Active = false;
bool s_Show[ Reward::NUM_MOD_QUESTS ];

void Load()
{
    if( s_Loaded )
    {
        return;
    }
    s_Loaded = true;
    s_Active = shar_ModHackRequired( "CustomRewardQuestSupport" ) != 0;
    for( int i = 0; i < NUM_QUEST_INFOS; i++ )
    {
        const QuestInfo& q = QUESTS[ i ];
        s_Show[ q.type ] = q.configKey != NULL && s_Active &&
                           shar_ModConfigInt( FILE_NAME, "ShowQuestComplete", q.configKey, q.showByDefault ) != 0;
    }
}

// progress the character sheet doesn't hold
enum { COUNT_HIT_AND_RUN, COUNT_EVADED, COUNT_BUSTED, NUM_COUNTS };
int s_Counts[ RenderEnums::numLevels ][ NUM_COUNTS ];

// getin / doorbell / maxcoins quests done: by level, type, reward name and argument
struct DoneQuest
{
    int level;
    int type;
    char name[ 16 ];
    char arg[ 32 ];
};
const int MAX_DONE = 512;
DoneQuest s_Done[ MAX_DONE ];
int s_NumDone = 0;

bool IsDone( Reward* reward )
{
    for( int i = 0; i < s_NumDone; i++ )
    {
        const DoneQuest& d = s_Done[ i ];
        if( d.level == reward->GetLevel() && d.type == reward->GetQuestType() &&
            strcmp( d.name, reward->GetName() ) == 0 && strcmp( d.arg, reward->GetModQuestArg() ) == 0 )
        {
            return true;
        }
    }
    return false;
}

void AddDone( int level, int type, const char* name, const char* arg )
{
    if( s_NumDone >= MAX_DONE )
    {
        return;
    }
    DoneQuest& d = s_Done[ s_NumDone++ ];
    d.level = level;
    d.type = type;
    snprintf( d.name, sizeof( d.name ), "%s", name );
    snprintf( d.arg, sizeof( d.arg ), "%s", arg );
}

void SetDone( Reward* reward )
{
    if( !IsDone( reward ) )
    {
        AddDone( reward->GetLevel(), reward->GetQuestType(), reward->GetName(), reward->GetModQuestArg() );
    }
}

bool AllMissionsCompleted( RenderEnums::LevelEnum level )
{
    int found = 0;
    for( int m = 1; m < MAX_MISSIONS; m++ )
    {
        char name[ 8 ];
        snprintf( name, sizeof( name ), "m%d", m );
        MissionRecord* record = GetCharacterSheetManager()->QueryMissionStatus( level, name );
        if( record != NULL )
        {
            if( !record->mCompleted )
            {
                return false;
            }
            found++;
        }
    }
    return found > 0;
}

// is the reward's quest done? currentLevel: the level being played, -1 outside one
bool QuestDone( Reward* reward, int currentLevel )
{
    CharacterSheetManager* sheet = GetCharacterSheetManager();
    RenderEnums::LevelEnum level = static_cast<RenderEnums::LevelEnum>( reward->GetLevel() );
    int number = reward->GetModQuestNumber();
    bool hasArg = reward->HasModQuestArg();
    switch( reward->GetQuestType() )
    {
        case Reward::eDefaultCar:
        case Reward::eDefaultSkin:
        {
            return static_cast<int>( level ) <= static_cast<int>( sheet->QueryHighestMission().mLevel ) ||
                   static_cast<int>( level ) == currentLevel;
        }
        case Reward::eCards:
        {
            if( hasArg )
            {
                return number >= 1 && number <= MAX_CARDS && sheet->QueryCardCollected( level, number - 1 );
            }
            return sheet->QueryAllCardsCollected( level );
        }
        case Reward::eStreetRace:
        {
            if( hasArg )
            {
                char name[ 16 ];
                snprintf( name, sizeof( name ), "sr%d", number );
                MissionRecord* record = sheet->QueryStreetRaceStatus( level, name );
                return record != NULL && record->mCompleted;
            }
            return sheet->QueryAllStreetRacesCompleted( level );
        }
        case Reward::eBonusMission:
        {
            return sheet->QueryBonusMissionCompleted( level );
        }
        case Reward::eWasps:
        {
            int total = ModStatsTotals::Active() ? ModStatsTotals::Wasps( level ) : GetRewardsManager()->GetTotalWasps( level );
            return total > 0 && sheet->QueryNumWaspsDestroyed( level ) >= total;
        }
        case Reward::eGags:
        {
            int total = ModStatsTotals::Active() ? ModStatsTotals::Gags( level ) : GetRewardsManager()->GetTotalGags( level );
            return total > 0 && sheet->QueryNumGagsViewed( level ) >= total;
        }
        case Reward::eMission:
        {
            if( hasArg )
            {
                char name[ 16 ];
                snprintf( name, sizeof( name ), "m%d", number );
                MissionRecord* record = sheet->QueryMissionStatus( level, name );
                return record != NULL && record->mCompleted;
            }
            return AllMissionsCompleted( level );
        }
        case Reward::eWager:
        {
            // the best time is set only when the race is won within its par time
            int best = sheet->GetGambleRaceBestTime( level );
            return best >= 0 && ( !hasArg || best < number );
        }
        case Reward::eCoins:
        {
            return sheet->GetNumberOfTokens( level ) >= number;
        }
        case Reward::eHitAndRun:
        {
            return s_Counts[ level ][ COUNT_HIT_AND_RUN ] >= ( number > 0 ? number : 1 );
        }
        case Reward::eEvaded:
        {
            return s_Counts[ level ][ COUNT_EVADED ] >= ( number > 0 ? number : 1 );
        }
        case Reward::eBusted:
        {
            return s_Counts[ level ][ COUNT_BUSTED ] >= ( number > 0 ? number : 1 );
        }
        case Reward::eGetIn:
        case Reward::eDoorbell:
        case Reward::eMaxCoins:
        {
            return IsDone( reward );
        }
        default:
        {
            return false;
        }
    }
}

// a text bible string: PREFIX_<NAME>, PREFIX_<TYPE>, PREFIX, else fallback
void FindText( const char* prefix, Reward* reward, const char* fallback, UnicodeString& text )
{
    char ids[ 3 ][ 64 ];
    int numIds = 0;
    if( reward != NULL )
    {
        snprintf( ids[ numIds++ ], sizeof( ids[ 0 ] ), "%s_%s", prefix, reward->GetName() );
        const QuestInfo* info = Info( reward->GetQuestType() );
        if( info != NULL )
        {
            snprintf( ids[ numIds++ ], sizeof( ids[ 0 ] ), "%s_%s", prefix, info->name );
        }
    }
    snprintf( ids[ numIds++ ], sizeof( ids[ 0 ] ), "%s", prefix );
    for( int i = 0; i < numIds; i++ )
    {
        for( char* c = ids[ i ]; *c != '\0'; c++ )
        {
            *c = static_cast<char>( toupper( static_cast<unsigned char>( *c ) ) );
        }
        P3D_UNICODE* string = CGuiTextBible::IsTextBibleLoaded() ? GetTextBibleString( ids[ i ] ) : NULL;
        if( string != NULL )
        {
            text.ReadUnicode( static_cast<const UnicodeChar*>( string ) );
            return;
        }
    }
    text.ReadAscii( fallback );
}

// quest complete messages waiting for the HUD
const int MAX_MESSAGES = 16;
Reward* s_Messages[ MAX_MESSAGES ];
int s_NumMessages = 0;

void ShowMessages()
{
    CGuiScreenHud* hud = GetCurrentHud();
    if( s_NumMessages == 0 || hud == NULL )
    {
        return;
    }
    HudWaspDestroyed* handler =
        static_cast<HudWaspDestroyed*>( hud->GetEventHandler( CGuiScreenHud::HUD_EVENT_HANDLER_WASP_DESTROYED ) );
    // the game's wasp / gag messages give way; another quest message is waited for
    if( handler == NULL || handler->IsShowingModMessage() )
    {
        return;
    }
    UnicodeString text;
    FindText( "QUEST_COMPLETE", s_Messages[ 0 ], "QUEST COMPLETE!", text );
    handler->StartModMessage( text );
    s_NumMessages--;
    memmove( &s_Messages[ 0 ], &s_Messages[ 1 ], s_NumMessages * sizeof( s_Messages[ 0 ] ) );
}

class QuestEventListener : public EventListener
{
public:
    virtual void HandleEvent( EventEnum id, void* pEventData )
    {
        int level = GetGameplayManager() != NULL ? GetGameplayManager()->GetCurrentLevelIndex() : -1;
        if( level < 0 || level >= RenderEnums::numLevels )
        {
            return;
        }
        switch( id )
        {
            case EVENT_HIT_AND_RUN_START:
            {
                s_Counts[ level ][ COUNT_HIT_AND_RUN ]++;
                break;
            }
            case EVENT_HIT_AND_RUN_EVADED:
            {
                s_Counts[ level ][ COUNT_EVADED ]++;
                break;
            }
            case EVENT_HIT_AND_RUN_CAUGHT:
            {
                s_Counts[ level ][ COUNT_BUSTED ]++;
                break;
            }
            case EVENT_DING_DONG:
            {
                if( pEventData != NULL )
                {
                    MarkDone( level, Reward::eDoorbell, static_cast<const char*>( pEventData ) );
                }
                break;
            }
            default:
            {
                break;
            }
        }
    }

    // case-sensitive, as the hack's documentation says
    static void MarkDone( int level, Reward::eQuestType type, const char* name )
    {
        RewardsManager* rm = GetRewardsManager();
        for( int i = 0; i < rm->GetNumRewards( level ); i++ )
        {
            Reward* reward = rm->GetRewardAt( level, i );
            if( reward != NULL && reward->GetQuestType() == type && strcmp( reward->GetModQuestArg(), name ) == 0 )
            {
                SetDone( reward );
            }
        }
    }
};

QuestEventListener* s_Listener = NULL;
Vehicle* s_Vehicle = NULL;     // the player's car at the last Update (only compared)

} // namespace

namespace ModRewardQuests
{

bool Active()
{
    Load();
    return s_Active;
}

Reward::eQuestType QuestType( const char* name )
{
    for( int i = 0; i < NUM_QUEST_INFOS; i++ )
    {
        if( QUESTS[ i ].type >= Reward::eModFirst && strcmp( QUESTS[ i ].name, name ) == 0 )
        {
            return QUESTS[ i ].type;
        }
    }
    return Reward::eBlank;
}

void Synch()
{
    if( !Active() )
    {
        return;
    }
    RewardsManager* rm = GetRewardsManager();
    for( int level = 0; level < RenderEnums::numLevels; level++ )
    {
        for( int i = 0; i < rm->GetNumRewards( level ); i++ )
        {
            Reward* reward = rm->GetRewardAt( level, i );
            if( reward == NULL || reward->GetRewardType() == Reward::eNULL )
            {
                continue;
            }
            bool done = QuestDone( reward, -1 );
            if( done && !reward->RewardStatus() )
            {
                reward->UnlockReward();
            }
            else if( !done && reward->RewardStatus() )
            {
                reward->LockReward();
            }
        }
    }
    s_NumMessages = 0;
}

void Update( int currentLevel )
{
    if( !Active() || currentLevel < 0 || currentLevel >= RenderEnums::numLevels )
    {
        return;
    }
    if( s_Listener == NULL )
    {
        s_Listener = new( GMA_PERSISTENT ) QuestEventListener;
        GetEventManager()->AddListener( s_Listener, EVENT_HIT_AND_RUN_START );
        GetEventManager()->AddListener( s_Listener, EVENT_HIT_AND_RUN_EVADED );
        GetEventManager()->AddListener( s_Listener, EVENT_HIT_AND_RUN_CAUGHT );
        GetEventManager()->AddListener( s_Listener, EVENT_DING_DONG );
    }

    // getin: the player's car, however they got in (door, script, phone booth)
    Avatar* avatar = GetAvatarManager()->GetAvatarForPlayer( 0 );
    Vehicle* vehicle = avatar != NULL && avatar->IsInCar() ? avatar->GetVehicle() : NULL;
    if( vehicle != s_Vehicle )
    {
        s_Vehicle = vehicle;
        if( vehicle != NULL && vehicle->mName != NULL )
        {
            QuestEventListener::MarkDone( currentLevel, Reward::eGetIn, vehicle->mName );
        }
    }

    CharacterSheetManager* sheet = GetCharacterSheetManager();
    RewardsManager* rm = GetRewardsManager();
    for( int level = 0; level < RenderEnums::numLevels; level++ )
    {
        for( int i = 0; i < rm->GetNumRewards( level ); i++ )
        {
            Reward* reward = rm->GetRewardAt( level, i );
            if( reward == NULL || reward->GetRewardType() == Reward::eNULL )
            {
                continue;
            }
            // reached in this level at some point
            if( reward->GetQuestType() == Reward::eMaxCoins && level == currentLevel &&
                sheet->GetNumberOfTokens( static_cast<RenderEnums::LevelEnum>( level ) ) >= reward->GetModQuestNumber() )
            {
                SetDone( reward );
            }
            bool done = QuestDone( reward, currentLevel );
            if( done && !reward->RewardStatus() )
            {
                reward->UnlockReward();
                if( getenv( "SHAR_MOD_LOG" ) != NULL )
                {
                    const QuestInfo* info = Info( reward->GetQuestType() );
                    fprintf( stderr, "mods: CustomRewardQuestSupport: L%d %s %s done: %s unlocked\n", level + 1,
                             info != NULL ? info->name : "?", reward->GetModQuestArg(), reward->GetName() );
                }
                if( s_Show[ reward->GetQuestType() ] && s_NumMessages < MAX_MESSAGES )
                {
                    s_Messages[ s_NumMessages++ ] = reward;
                }
            }
            else if( !done && reward->RewardStatus() && reward->GetQuestType() == Reward::eCoins )
            {
                reward->LockReward();
            }
        }
    }
    ShowMessages();
}

void Clear()
{
    memset( s_Counts, 0, sizeof( s_Counts ) );
    s_NumDone = 0;
    s_NumMessages = 0;
}

bool HasSaveData()
{
    if( !Active() )
    {
        return false;
    }
    for( int level = 0; level < RenderEnums::numLevels; level++ )
    {
        for( int c = 0; c < NUM_COUNTS; c++ )
        {
            if( s_Counts[ level ][ c ] != 0 )
            {
                return true;
            }
        }
    }
    return s_NumDone > 0;
}

void Write( FILE* file )
{
    if( !Active() )
    {
        return;
    }
    for( int level = 0; level < RenderEnums::numLevels; level++ )
    {
        const int* c = s_Counts[ level ];
        if( c[ COUNT_HIT_AND_RUN ] != 0 || c[ COUNT_EVADED ] != 0 || c[ COUNT_BUSTED ] != 0 )
        {
            fprintf( file, "questcounts %d %d %d %d\n", level, c[ COUNT_HIT_AND_RUN ], c[ COUNT_EVADED ],
                     c[ COUNT_BUSTED ] );
        }
    }
    for( int i = 0; i < s_NumDone; i++ )
    {
        const DoneQuest& d = s_Done[ i ];
        const QuestInfo* info = Info( static_cast<Reward::eQuestType>( d.type ) );
        // names and arguments hold no spaces (script tokens); an empty argument is "-"
        fprintf( file, "questdone %d %s %s %s\n", d.level, info != NULL ? info->name : "?", d.name,
                 d.arg[ 0 ] != '\0' ? d.arg : "-" );
    }
}

bool ReadLine( const char* line )
{
    int level, a, b, c;
    char type[ 32 ], name[ 32 ], arg[ 64 ];
    if( sscanf( line, "questcounts %d %d %d %d", &level, &a, &b, &c ) == 4 )
    {
        if( level >= 0 && level < RenderEnums::numLevels )
        {
            s_Counts[ level ][ COUNT_HIT_AND_RUN ] = a;
            s_Counts[ level ][ COUNT_EVADED ] = b;
            s_Counts[ level ][ COUNT_BUSTED ] = c;
        }
        return true;
    }
    if( sscanf( line, "questdone %d %31s %31s %63s", &level, type, name, arg ) == 4 )
    {
        for( int i = 0; i < NUM_QUEST_INFOS; i++ )
        {
            if( strcmp( QUESTS[ i ].name, type ) == 0 && level >= 0 && level < RenderEnums::numLevels )
            {
                AddDone( level, QUESTS[ i ].type, name, strcmp( arg, "-" ) == 0 ? "" : arg );
            }
        }
        return true;
    }
    return false;
}

bool IsQuestReward( Reward* reward )
{
    return Active() && reward != NULL && reward->GetQuestType() != Reward::eBlank;
}

void CompleteQuestText( Reward* reward, UnicodeString& text )
{
    FindText( "COMPLETE_QUEST", reward, "Complete quest", text );
}

void ToUnlockText( UnicodeString& text )
{
    FindText( "TO_UNLOCK", NULL, "to unlock", text );
}

} // namespace ModRewardQuests

#endif // RAD_LINUX
