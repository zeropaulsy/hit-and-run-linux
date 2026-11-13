// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

//=============================================================================
// Linux port: Mod Launcher CustomTriggerActions (see modtriggeractions.h).
//=============================================================================
#ifdef RAD_LINUX

#include <raddebug.hpp>
#include <radtime.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <meta/modtriggeractions.h>
#include <meta/triggervolume.h>
#include <meta/triggerlocator.h>
#include <meta/triggervolumetracker.h>
#include <meta/eventlocator.h>
#include <meta/actioneventlocator.h>
#include <constants/maxplayers.h>
#include <p3d/utility.hpp>
#include <p3d/inventory.hpp>
#include <p3d/shader.hpp>
#include <p3d/texture.hpp>
#include <p3d/drawable.hpp>
#include <mission/gameplaymanager.h>
#include <mission/mission.h>
#include <mission/charactersheet/charactersheetmanager.h>
#include <mission/objectives/modobjectives.h>
#include <cheats/cheatinputsystem.h>
#include <worldsim/avatar.h>
#include <worldsim/avatarmanager.h>
#include <worldsim/hitnrunmanager.h>
#include <worldsim/redbrick/vehicle.h>
#include <worldsim/character/character.h>
#include <worldsim/traffic/trafficmanager.h>
#include <sound/soundmanager.h>
#include <presentation/gui/ingame/guimanageringame.h>
#include <presentation/gui/ingame/guiscreenhud.h>
#include <presentation/gui/utility/hudmap.h>
#include <shar_mods.h>

namespace
{
    const char* const FILE_NAME = "CustomTriggerActions.ini";
    const float DEGREES_TO_RADIANS = 3.14159265f / 180.0f;

    enum { NAME = 64, MAX_ACTIONS = 512, MAX_CONDITIONS = 256, MAX_TRIGGERS = 256, MAX_BINDINGS = 256,
           MAX_REFS = 32, MAX_NAMES = 32, MAX_INSIDE = 64, MAX_DEPTH = 16 };

    enum ActionType { ACTION_UNKNOWN, ACTION_TEXTURE, ACTION_HUD_MAP, ACTION_DESTROY_CAR, ACTION_HIT_AND_RUN,
                      ACTION_TRAFFIC_GROUP, ACTION_MUSIC, ACTION_TILT };

    enum ConditionType { CONDITION_UNKNOWN, CONDITION_CAR, CONDITION_PLAYER_CAR, CONDITION_CHARACTER,
                         CONDITION_ALL, CONDITION_ANY, CONDITION_IS_PLAYER, CONDITION_MISSION,
                         CONDITION_UNLOCKED, CONDITION_COMPLETED, CONDITION_SETTING };

    struct Action
    {
        char name[ NAME ];
        ActionType type;
        char shader[ NAME ];        // ChangeTexture
        char text[ NAME ];          // Texture, DrawableName, Event
        float value;                // SetHitAndRunMeter, ChangeTrafficGroup, TiltCamera (degrees)
        float duration;             // TiltCamera, seconds
        bool reset;
    };

    struct Condition
    {
        char name[ NAME ];
        ConditionType type;
        bool negate;
        char carName[ NAME ];
        int carIndex;               // -1: any
        tUID character;             // 0: any
        int numSubs;
        short subs[ MAX_REFS ];
        int level;                  // 1-7, 0 unset
        int mission;                // -1 unset
        int sundayDrive;            // -1 unset, 0 main part, 1 Sunday Drive part
        int bonusMission;           // 1-5, 0 unset
        float bestTime;             // seconds, < 0 unset
        char setting[ NAME ];
        char settingValue[ NAME ];
        char settingMod[ 128 ];
    };

    struct Trigger
    {
        char name[ NAME ];
        int locatorType;            // -1: any
        int locatorEvent;           // -1: any
        char actionType[ NAME ];    // "": any
        int numLocators, numVolumes, numConditions;
        tUID locators[ MAX_NAMES ];
        char locatorNames[ MAX_NAMES ][ NAME ];
        tUID volumes[ MAX_NAMES ];
        short conditions[ MAX_REFS ];
    };

    struct Binding                  // [CustomTriggerAction]
    {
        int numActions, numTriggers, numConditions, numEvents;
        short actions[ MAX_REFS ];
        short triggers[ MAX_REFS ];
        short conditions[ MAX_REFS ];
        int events[ MAX_REFS ];     // EventEnum
        bool preventDefault;
    };

    // what entered a trigger (an event has none: known = false)
    struct Entity
    {
        bool known;
        bool player;
        int playerID;
        Vehicle* vehicle;           // the car that entered, if a car did
        Character* character;       // the player's character
    };

    // a trigger the player or an AI car is inside, whose entering ran actions
    struct Inside
    {
        TriggerLocator* locator;
        unsigned int playerID;
        bool preventDefault;
        bool tilt;
        unsigned int tiltGeneration;
    };

    struct Tilt
    {
        int count;                  // tilting triggers the player is inside
        float radians;
        float startRadians;         // when the last was left
        unsigned int releaseTime;
        unsigned int duration;      // ms
        bool reset;
        Mission* mission;
        unsigned int generation;    // a Reset clears the triggers' hold on it
    };

    Action s_Actions[ MAX_ACTIONS ];
    Condition s_Conditions[ MAX_CONDITIONS ];
    Trigger s_Triggers[ MAX_TRIGGERS ];
    Binding s_Bindings[ MAX_BINDINGS ];
    int s_NumActions = 0, s_NumConditions = 0, s_NumTriggers = 0, s_NumBindings = 0;
    unsigned char s_EventBound[ ( NUM_EVENTS + 7 ) / 8 ];
    Inside s_Inside[ MAX_INSIDE ];
    int s_NumInside = 0;
    Tilt s_Tilt[ MAX_PLAYERS ];
    char s_ModName[ 128 ] = "";
    bool s_Loaded = false;
    bool s_Active = false;

    int Log()
    {
        static int s_Log = -1;
        if( s_Log < 0 )
        {
            s_Log = getenv( "SHAR_MOD_LOG" ) != NULL;
        }
        return s_Log;
    }

    void Copy( char* out, size_t size, const char* s )
    {
        snprintf( out, size, "%s", s );
    }

    bool IsTrue( const char* s )
    {
        return strcasecmp( s, "true" ) == 0 || atoi( s ) != 0;
    }

    //=========================================================================
    // Loading: pass 1 makes every section's entry, pass 2 resolves the names they refer to
    //=========================================================================
    enum Section { SECTION_NONE, SECTION_ACTION, SECTION_CONDITION, SECTION_TRIGGER, SECTION_BINDING };

    struct Parser
    {
        int pass;
        Section section;
        int index;                  // the entry of the current section, -1 when there was no room
        int numActions, numConditions, numTriggers, numBindings;
    };

    int FindAction( const char* name )
    {
        for( int i = 0; i < s_NumActions; i++ )
        {
            if( strcasecmp( s_Actions[ i ].name, name ) == 0 ) return i;
        }
        return -1;
    }

    int FindCondition( const char* name )
    {
        for( int i = 0; i < s_NumConditions; i++ )
        {
            if( strcasecmp( s_Conditions[ i ].name, name ) == 0 ) return i;
        }
        return -1;
    }

    int FindTrigger( const char* name )
    {
        for( int i = 0; i < s_NumTriggers; i++ )
        {
            if( strcasecmp( s_Triggers[ i ].name, name ) == 0 ) return i;
        }
        return -1;
    }

    // adds a resolved reference; a missing name is reported once, in pass 2
    void AddRef( short* refs, int* count, int index, const char* kind, const char* name )
    {
        if( index < 0 )
        {
            fprintf( stderr, "mods: CustomTriggerActions: no %s named %s, ignored\n", kind, name );
        }
        else if( *count < MAX_REFS )
        {
            refs[ ( *count )++ ] = static_cast< short >( index );
        }
    }

    ActionType ParseActionType( const char* s )
    {
        if( strcasecmp( s, "ChangeTexture" ) == 0 ) return ACTION_TEXTURE;
        if( strcasecmp( s, "ChangeHUDMapDrawable" ) == 0 ) return ACTION_HUD_MAP;
        if( strcasecmp( s, "DestroyCar" ) == 0 ) return ACTION_DESTROY_CAR;
        if( strcasecmp( s, "SetHitAndRunMeter" ) == 0 ) return ACTION_HIT_AND_RUN;
        if( strcasecmp( s, "ChangeTrafficGroup" ) == 0 ) return ACTION_TRAFFIC_GROUP;
        if( strcasecmp( s, "TriggerMusicEvent" ) == 0 ) return ACTION_MUSIC;
        if( strcasecmp( s, "TiltCamera" ) == 0 ) return ACTION_TILT;
        fprintf( stderr, "mods: CustomTriggerActions: action type %s not supported, ignored\n", s );
        return ACTION_UNKNOWN;
    }

    ConditionType ParseConditionType( const char* s )
    {
        if( strcasecmp( s, "Car" ) == 0 ) return CONDITION_CAR;
        if( strcasecmp( s, "PlayerCar" ) == 0 ) return CONDITION_PLAYER_CAR;
        if( strcasecmp( s, "Character" ) == 0 ) return CONDITION_CHARACTER;
        if( strcasecmp( s, "Conditions" ) == 0 ) return CONDITION_ALL;
        if( strcasecmp( s, "Condition" ) == 0 ) return CONDITION_ANY;
        if( strcasecmp( s, "IsPlayer" ) == 0 ) return CONDITION_IS_PLAYER;
        if( strcasecmp( s, "Mission" ) == 0 ) return CONDITION_MISSION;
        if( strcasecmp( s, "UnlockedMission" ) == 0 ) return CONDITION_UNLOCKED;
        if( strcasecmp( s, "CompletedMission" ) == 0 ) return CONDITION_COMPLETED;
        if( strcasecmp( s, "Setting" ) == 0 ) return CONDITION_SETTING;
        fprintf( stderr, "mods: CustomTriggerActions: condition type %s not supported (never met)\n", s );
        return CONDITION_UNKNOWN;
    }

    void StartSection( Parser* p, const char* section )
    {
        p->index = -1;
        p->section = SECTION_NONE;
        if( strcasecmp( section, "Action" ) == 0 )
        {
            p->section = SECTION_ACTION;
            if( p->pass == 1 && s_NumActions < MAX_ACTIONS )
            {
                Action* a = &s_Actions[ s_NumActions++ ];
                memset( a, 0, sizeof( *a ) );
            }
            if( p->numActions < s_NumActions ) p->index = p->numActions;
            p->numActions++;
        }
        else if( strcasecmp( section, "Condition" ) == 0 )
        {
            p->section = SECTION_CONDITION;
            if( p->pass == 1 && s_NumConditions < MAX_CONDITIONS )
            {
                Condition* c = &s_Conditions[ s_NumConditions++ ];
                *c = Condition();
                c->carIndex = -1;
                c->mission = -1;
                c->sundayDrive = -1;
                c->bestTime = -1.0f;
            }
            if( p->numConditions < s_NumConditions ) p->index = p->numConditions;
            p->numConditions++;
        }
        else if( strcasecmp( section, "Trigger" ) == 0 )
        {
            p->section = SECTION_TRIGGER;
            if( p->pass == 1 && s_NumTriggers < MAX_TRIGGERS )
            {
                Trigger* t = &s_Triggers[ s_NumTriggers++ ];
                *t = Trigger();
                t->locatorType = -1;
                t->locatorEvent = -1;
            }
            if( p->numTriggers < s_NumTriggers ) p->index = p->numTriggers;
            p->numTriggers++;
        }
        else if( strcasecmp( section, "CustomTriggerAction" ) == 0 )
        {
            p->section = SECTION_BINDING;
            if( p->pass == 1 && s_NumBindings < MAX_BINDINGS )
            {
                Binding* b = &s_Bindings[ s_NumBindings++ ];
                memset( b, 0, sizeof( *b ) );
            }
            if( p->numBindings < s_NumBindings ) p->index = p->numBindings;
            p->numBindings++;
        }
        else if( p->pass == 1 )
        {
            fprintf( stderr, "mods: CustomTriggerActions: unknown section [%s], ignored\n", section );
        }
    }

    void ActionKey( Action* a, const char* key, const char* value )
    {
        if( strcasecmp( key, "Name" ) == 0 ) Copy( a->name, sizeof( a->name ), value );
        else if( strcasecmp( key, "Type" ) == 0 ) a->type = ParseActionType( value );
        else if( strcasecmp( key, "Shader" ) == 0 ) Copy( a->shader, sizeof( a->shader ), value );
        else if( strcasecmp( key, "Texture" ) == 0 || strcasecmp( key, "DrawableName" ) == 0 ||
                 strcasecmp( key, "Event" ) == 0 ) Copy( a->text, sizeof( a->text ), value );
        else if( strcasecmp( key, "Value" ) == 0 ) a->value = static_cast< float >( atof( value ) );
        else if( strcasecmp( key, "Duration" ) == 0 ) a->duration = static_cast< float >( atof( value ) );
        else if( strcasecmp( key, "Reset" ) == 0 ) a->reset = IsTrue( value );
    }

    void ConditionKey( Condition* c, const char* key, const char* value )
    {
        if( strcasecmp( key, "Name" ) == 0 ) Copy( c->name, sizeof( c->name ), value );
        else if( strcasecmp( key, "Type" ) == 0 ) c->type = ParseConditionType( value );
        else if( strcasecmp( key, "Not" ) == 0 ) c->negate = IsTrue( value );
        else if( strcasecmp( key, "CarName" ) == 0 ) Copy( c->carName, sizeof( c->carName ), value );
        else if( strcasecmp( key, "CarIndex" ) == 0 ) c->carIndex = atoi( value );
        else if( strcasecmp( key, "CharacterName" ) == 0 ) c->character = tEntity::MakeUID( value );
        else if( strcasecmp( key, "Level" ) == 0 ) c->level = atoi( value );
        else if( strcasecmp( key, "Mission" ) == 0 ) c->mission = atoi( value );
        else if( strcasecmp( key, "SundayDrive" ) == 0 ) c->sundayDrive = IsTrue( value ) ? 1 : 0;
        else if( strcasecmp( key, "BonusMission" ) == 0 )
        {
            int n = atoi( value );
            c->bonusMission = ( n > 20 ) ? n - 20 : n;     // 21-25 before launcher 1.22.2
        }
        else if( strcasecmp( key, "BestTime" ) == 0 ) c->bestTime = static_cast< float >( atof( value ) );
        else if( strcasecmp( key, "SettingName" ) == 0 ) Copy( c->setting, sizeof( c->setting ), value );
        else if( strcasecmp( key, "SettingValue" ) == 0 ) Copy( c->settingValue, sizeof( c->settingValue ), value );
        else if( strcasecmp( key, "ModName" ) == 0 ) Copy( c->settingMod, sizeof( c->settingMod ), value );
    }

    void TriggerKey( Trigger* t, const char* key, const char* value )
    {
        if( strcasecmp( key, "Name" ) == 0 ) Copy( t->name, sizeof( t->name ), value );
        else if( strcasecmp( key, "LocatorType" ) == 0 ) t->locatorType = atoi( value );
        else if( strcasecmp( key, "LocatorType0Event" ) == 0 ) t->locatorEvent = atoi( value );
        else if( strcasecmp( key, "LocatorType9Type" ) == 0 ) Copy( t->actionType, sizeof( t->actionType ), value );
        else if( strcasecmp( key, "LocatorName" ) == 0 && t->numLocators < MAX_NAMES )
        {
            Copy( t->locatorNames[ t->numLocators ], NAME, value );
            t->locators[ t->numLocators++ ] = tEntity::MakeUID( value );
        }
        else if( strcasecmp( key, "TriggerName" ) == 0 && t->numVolumes < MAX_NAMES )
        {
            t->volumes[ t->numVolumes++ ] = tEntity::MakeUID( value );
        }
    }

    void BindingKey( Binding* b, const char* key, const char* value )
    {
        if( strcasecmp( key, "PreventDefault" ) == 0 ) b->preventDefault = IsTrue( value );
        else if( strcasecmp( key, "Event" ) == 0 && b->numEvents < MAX_REFS )
        {
            int id = ModTranslateEvent( atoi( value ) );
            if( id >= 0 && id < NUM_EVENTS )
            {
                b->events[ b->numEvents++ ] = id;
                s_EventBound[ id >> 3 ] |= static_cast< unsigned char >( 1 << ( id & 7 ) );
            }
        }
    }

    void OnKey( const char* section, const char* key, const char* value, void* user )
    {
        Parser* p = static_cast< Parser* >( user );
        if( key == NULL )
        {
            StartSection( p, section );
            return;
        }
        if( p->index < 0 )
        {
            return;
        }
        if( p->pass == 1 )
        {
            switch( p->section )
            {
            case SECTION_ACTION: ActionKey( &s_Actions[ p->index ], key, value ); break;
            case SECTION_CONDITION: ConditionKey( &s_Conditions[ p->index ], key, value ); break;
            case SECTION_TRIGGER: TriggerKey( &s_Triggers[ p->index ], key, value ); break;
            case SECTION_BINDING: BindingKey( &s_Bindings[ p->index ], key, value ); break;
            default: break;
            }
            return;
        }
        // pass 2: references
        if( p->section == SECTION_CONDITION && strcasecmp( key, "Condition" ) == 0 )
        {
            Condition* c = &s_Conditions[ p->index ];
            AddRef( c->subs, &c->numSubs, FindCondition( value ), "condition", value );
        }
        else if( p->section == SECTION_TRIGGER && strcasecmp( key, "Condition" ) == 0 )
        {
            Trigger* t = &s_Triggers[ p->index ];
            AddRef( t->conditions, &t->numConditions, FindCondition( value ), "condition", value );
        }
        else if( p->section == SECTION_BINDING )
        {
            Binding* b = &s_Bindings[ p->index ];
            if( strcasecmp( key, "Action" ) == 0 ) AddRef( b->actions, &b->numActions, FindAction( value ), "action", value );
            else if( strcasecmp( key, "Trigger" ) == 0 ) AddRef( b->triggers, &b->numTriggers, FindTrigger( value ), "trigger", value );
            else if( strcasecmp( key, "Condition" ) == 0 ) AddRef( b->conditions, &b->numConditions, FindCondition( value ), "condition", value );
        }
    }

    void Load()
    {
        s_Loaded = true;
        if( !shar_ModHackRequired( "CustomTriggerActions" ) )
        {
            return;
        }
        const char* modName = NULL;
        for( int pass = 1; pass <= 2; pass++ )
        {
            Parser p;
            memset( &p, 0, sizeof( p ) );
            p.pass = pass;
            p.index = -1;
            if( !shar_ModConfigIni( FILE_NAME, OnKey, &p, &modName ) )
            {
                fprintf( stderr, "mods: CustomTriggerActions: no %s\n", FILE_NAME );
                return;
            }
        }
        if( modName != NULL )
        {
            Copy( s_ModName, sizeof( s_ModName ), modName );
        }
        s_Active = s_NumBindings > 0;
        fprintf( stderr, "mods: CustomTriggerActions: %d actions, %d conditions, %d triggers, %d bindings\n",
                 s_NumActions, s_NumConditions, s_NumTriggers, s_NumBindings );
    }

    bool Active()
    {
        if( !s_Loaded )
        {
            Load();
        }
        return s_Active;
    }

    //=========================================================================
    // Conditions
    //=========================================================================
    bool CarMatches( const Condition& c, Vehicle* v )
    {
        if( v == NULL ) return false;
        if( c.carName[ 0 ] != '\0' && strcasecmp( v->GetName(), c.carName ) != 0 ) return false;
        if( c.carIndex >= 0 && static_cast< int >( v->GetID() ) != c.carIndex ) return false;
        return true;
    }

    // the character sheet's index of story mission number (Level 1 has m0)
    int MissionRecordIndex( int level, int mission )
    {
        return level == 1 ? mission : mission - 1;
    }

    bool InMission( const Condition& c )
    {
        GameplayManager* gm = GetGameplayManager();
        if( gm == NULL || c.level != static_cast< int >( gm->GetCurrentLevelIndex() ) + 1 ) return false;
        Mission* m = gm->GetCurrentMission();
        if( m == NULL ) return false;
        if( c.bonusMission > 0 )
        {
            return m->IsBonusMission() && gm->GetCurrentMissionNum() - GameplayManager::MAX_MISSIONS == c.bonusMission - 1;
        }
        if( m->IsBonusMission() ) return c.mission < 0 && c.sundayDrive < 0;
        if( c.mission >= 0 )
        {
            int number = gm->GetCurrentMissionIndex() + ( c.level == 1 ? 0 : 1 );
            if( number != c.mission ) return false;
            return m->IsSundayDrive() == ( c.sundayDrive == 1 );
        }
        return c.sundayDrive < 0 || m->IsSundayDrive() == ( c.sundayDrive == 1 );
    }

    bool MissionCompleted( const Condition& c )
    {
        if( c.level < 1 || c.level > 7 ) return false;
        RenderEnums::LevelEnum level = static_cast< RenderEnums::LevelEnum >( c.level - 1 );
        CharacterSheetManager* sheet = GetCharacterSheetManager();
        if( c.bonusMission > 0 )
        {
            int best = -1;
            bool completed = false;
            if( c.bonusMission <= 3 )
            {
                MissionRecord* r = sheet->QueryStreetRaceStatus( level, c.bonusMission - 1 );
                completed = r != NULL && r->mCompleted;
                best = r != NULL ? r->mBestTime : -1;
            }
            else if( c.bonusMission == 4 )
            {
                best = sheet->GetGambleRaceBestTime( level );
                completed = best >= 0;
            }
            else
            {
                completed = sheet->QueryBonusMissionCompleted( level );
            }
            if( completed && c.bestTime >= 0.0f )
            {
                completed = best >= 0 && static_cast< float >( best ) <= c.bestTime;
            }
            return completed;
        }
        if( c.mission < 0 ) return false;
        MissionRecord* r = sheet->QueryMissionStatus( level, MissionRecordIndex( c.level, c.mission ) );
        return r != NULL && r->mCompleted;
    }

    bool MissionUnlocked( const Condition& c )
    {
        if( c.level < 1 || c.level > 7 || c.mission < 0 ) return false;
        if( GetCheatInputSystem()->IsCheatEnabled( CHEAT_ID_UNLOCK_MISSIONS ) || MissionCompleted( c ) ) return true;
        // the mission select's rule
        CurrentMissionStruct highest = GetCharacterSheetManager()->QueryHighestMission();
        int level = c.level - 1;
        return level < static_cast< int >( highest.mLevel ) ||
               ( level == static_cast< int >( highest.mLevel ) &&
                 MissionRecordIndex( c.level, c.mission ) <= static_cast< int >( highest.mMissionNumber ) );
    }

    bool SettingMatches( const Condition& c )
    {
        char value[ 256 ];
        if( !shar_ModSetting( c.settingMod[ 0 ] != '\0' ? c.settingMod : s_ModName, c.setting, value, sizeof( value ) ) )
        {
            return false;
        }
        if( strcasecmp( value, c.settingValue ) == 0 ) return true;
        // tick boxes: "true"/"false"/"1"/"0" alike; numbers by value
        char* end1;
        char* end2;
        double a = strtod( value, &end1 );
        double b = strtod( c.settingValue, &end2 );
        if( *end1 == '\0' && *end2 == '\0' && value[ 0 ] != '\0' && c.settingValue[ 0 ] != '\0' ) return a == b;
        bool boolA = strcasecmp( value, "true" ) == 0 || ( *end1 == '\0' && a != 0.0 );
        bool boolB = strcasecmp( c.settingValue, "true" ) == 0 || ( *end2 == '\0' && b != 0.0 );
        bool isBoolA = boolA || strcasecmp( value, "false" ) == 0 || *end1 == '\0';
        bool isBoolB = boolB || strcasecmp( c.settingValue, "false" ) == 0 || *end2 == '\0';
        return isBoolA && isBoolB && boolA == boolB;
    }

    bool ConditionMet( int index, const Entity& e, int depth );

    bool Test( const Condition& c, const Entity& e, int depth )
    {
        switch( c.type )
        {
        case CONDITION_CAR: return e.known && CarMatches( c, e.vehicle );
        case CONDITION_PLAYER_CAR: return e.known && e.player && CarMatches( c, e.vehicle );
        case CONDITION_CHARACTER:
            return e.known && e.character != NULL && e.vehicle == NULL &&
                   ( c.character == 0 || e.character->GetUID() == c.character );
        case CONDITION_ALL:
            for( int i = 0; i < c.numSubs; i++ )
            {
                if( !ConditionMet( c.subs[ i ], e, depth + 1 ) ) return false;
            }
            return c.numSubs > 0;
        case CONDITION_ANY:
            for( int i = 0; i < c.numSubs; i++ )
            {
                if( ConditionMet( c.subs[ i ], e, depth + 1 ) ) return true;
            }
            return false;
        case CONDITION_IS_PLAYER: return e.known && e.player;
        case CONDITION_MISSION: return InMission( c );
        case CONDITION_UNLOCKED: return MissionUnlocked( c );
        case CONDITION_COMPLETED: return MissionCompleted( c );
        case CONDITION_SETTING: return SettingMatches( c );
        default: return false;
        }
    }

    bool ConditionMet( int index, const Entity& e, int depth )
    {
        if( depth > MAX_DEPTH )
        {
            static bool s_Warned = false;
            if( !s_Warned )
            {
                s_Warned = true;
                fprintf( stderr, "mods: CustomTriggerActions: conditions nest too deep (a loop?)\n" );
            }
            return false;
        }
        const Condition& c = s_Conditions[ index ];
        return Test( c, e, depth ) != c.negate;
    }

    bool AnyConditionMet( const short* conditions, int count, const Entity& e )
    {
        if( count == 0 ) return true;
        for( int i = 0; i < count; i++ )
        {
            if( ConditionMet( conditions[ i ], e, 0 ) ) return true;
        }
        return false;
    }

    bool TriggerMatches( const Trigger& t, TriggerVolume* volume, TriggerLocator* locator, const Entity& e )
    {
        if( t.locatorType >= 0 && static_cast< int >( locator->GetDataType() ) != t.locatorType ) return false;
        if( t.locatorEvent >= 0 &&
            ( locator->GetDataType() != LocatorType::EVENT ||
              static_cast< int >( static_cast< EventLocator* >( locator )->GetEventType() ) != t.locatorEvent ) )
        {
            return false;
        }
        if( t.actionType[ 0 ] != '\0' )
        {
            if( locator->GetDataType() != LocatorType::ACTION ) return false;
            const char* action = static_cast< ActionEventLocator* >( locator )->GetActionName();
            if( action == NULL || strcasecmp( action, t.actionType ) != 0 ) return false;
        }
        if( t.numLocators > 0 )
        {
            bool found = false;
            for( int i = 0; i < t.numLocators && !found; i++ )
            {
                found = locator->GetUID() == t.locators[ i ] ||
                        ( locator->GetDataType() == LocatorType::ACTION &&
                          static_cast< ActionEventLocator* >( locator )->IsNamed( t.locatorNames[ i ] ) );
            }
            if( !found ) return false;
        }
        if( t.numVolumes > 0 )
        {
            bool found = false;
            for( int i = 0; i < t.numVolumes && !found; i++ )
            {
                found = volume->GetUID() == t.volumes[ i ];
            }
            if( !found ) return false;
        }
        return AnyConditionMet( t.conditions, t.numConditions, e );
    }

    //=========================================================================
    // Actions
    //=========================================================================
    template< class T > T* FindInventory( const char* name )
    {
        bool sectionOnly = p3d::inventory->GetCurrentSectionOnly();
        p3d::inventory->SetCurrentSectionOnly( false );
        T* found = p3d::find< T >( name );
        p3d::inventory->SetCurrentSectionOnly( sectionOnly );
        return found;
    }

    void Missing( const Action& a, const char* what, const char* name )
    {
        fprintf( stderr, "mods: CustomTriggerActions: %s: no %s %s\n", a.name, what, name );
    }

    // returns whether the action holds a camera tilt (until the trigger is left)
    bool Run( const Action& a, const Entity& e )
    {
        if( Log() ) fprintf( stderr, "mods: CustomTriggerActions: action %s\n", a.name );
        switch( a.type )
        {
        case ACTION_TEXTURE:
        {
            tShader* shader = FindInventory< tShader >( a.shader );
            tTexture* texture = FindInventory< tTexture >( a.text );
            if( shader == NULL ) Missing( a, "shader", a.shader );
            else if( texture == NULL ) Missing( a, "texture", a.text );
            else shader->SetTexture( PDDI_SP_BASETEX, texture );
            break;
        }
        case ACTION_HUD_MAP:
        {
            tDrawable* drawable = FindInventory< tDrawable >( a.text );
            CGuiScreenHud* hud = GetCurrentHud();
            if( drawable == NULL ) Missing( a, "drawable", a.text );
            else if( hud != NULL )
            {
                for( int i = 0; i < GetGameplayManager()->GetNumPlayers(); i++ )
                {
                    CHudMap* map = hud->GetHudMap( i );
                    if( map != NULL ) map->ModSetMapDrawable( drawable );
                }
            }
            break;
        }
        case ACTION_DESTROY_CAR:
            if( e.vehicle != NULL && !e.vehicle->IsVehicleDestroyed() )
            {
                e.vehicle->TriggerDamage( 1.0e9f, false );
            }
            break;
        case ACTION_HIT_AND_RUN:
            GetHitnRunManager()->SetHitnRunValue( rmt::Clamp( a.value, 0.0f, 100.0f ) );
            break;
        case ACTION_TRAFFIC_GROUP:
            TrafficManager::GetInstance()->SetModTrafficGroup( static_cast< int >( a.value ) );
            break;
        case ACTION_MUSIC:
            if( !GetSoundManager()->ModTriggerMusicEvent( a.text ) && Log() )
            {
                fprintf( stderr, "mods: CustomTriggerActions: %s: no music event %s\n", a.name, a.text );
            }
            break;
        case ACTION_TILT:
            if( e.known && e.player && e.playerID >= 0 && e.playerID < MAX_PLAYERS )
            {
                Tilt& t = s_Tilt[ e.playerID ];
                t.count++;
                t.radians = a.value * DEGREES_TO_RADIANS;
                t.duration = static_cast< unsigned int >( rmt::Max( a.duration, 0.0f ) * 1000.0f );
                t.reset = a.reset;
                t.mission = GetGameplayManager() != NULL ? GetGameplayManager()->GetCurrentMission() : NULL;
                return true;
            }
            break;
        default:
            break;
        }
        return false;
    }

    // runs the bindings that match; returns whether one prevents the default; *tilt: one held a tilt
    bool RunBindings( TriggerVolume* volume, TriggerLocator* locator, int eventId, const Entity& e, bool* matched,
                      bool* tilt )
    {
        bool prevent = false;
        for( int i = 0; i < s_NumBindings; i++ )
        {
            const Binding& b = s_Bindings[ i ];
            bool hit = false;
            if( locator != NULL )
            {
                for( int t = 0; t < b.numTriggers && !hit; t++ )
                {
                    hit = TriggerMatches( s_Triggers[ b.triggers[ t ] ], volume, locator, e );
                }
            }
            else
            {
                for( int ev = 0; ev < b.numEvents && !hit; ev++ )
                {
                    hit = b.events[ ev ] == eventId;
                }
            }
            if( !hit || !AnyConditionMet( b.conditions, b.numConditions, e ) )
            {
                continue;
            }
            *matched = true;
            for( int a = 0; a < b.numActions; a++ )
            {
                if( Run( s_Actions[ b.actions[ a ] ], e ) ) *tilt = true;
            }
            prevent = prevent || b.preventDefault;
        }
        return prevent;
    }

    int FindInside( TriggerLocator* locator, unsigned int playerID )
    {
        for( int i = 0; i < s_NumInside; i++ )
        {
            if( s_Inside[ i ].locator == locator && s_Inside[ i ].playerID == playerID ) return i;
        }
        return -1;
    }

    void ReleaseTilt( int playerID, unsigned int generation )
    {
        if( playerID < 0 || playerID >= MAX_PLAYERS ) return;
        Tilt& t = s_Tilt[ playerID ];
        if( generation != t.generation || t.count <= 0 ) return;
        if( --t.count == 0 )
        {
            t.startRadians = t.radians;
            t.releaseTime = radTimeGetMilliseconds();
        }
    }
}

namespace ModTriggerActions
{

bool OnTrigger( TriggerVolume* volume, TriggerLocator* locator, unsigned int playerID, bool entered )
{
    if( !Active() || locator == NULL )
    {
        return false;
    }
    int inside = FindInside( locator, playerID );
    if( !entered )
    {
        if( inside < 0 ) return false;
        Inside left = s_Inside[ inside ];
        s_Inside[ inside ] = s_Inside[ --s_NumInside ];
        if( left.tilt ) ReleaseTilt( static_cast< int >( playerID ), left.tiltGeneration );
        return left.preventDefault;
    }
    if( inside >= 0 )
    {
        return s_Inside[ inside ].preventDefault;  // still inside (death volumes say so every frame)
    }

    Entity e;
    memset( &e, 0, sizeof( e ) );
    e.known = true;
    e.playerID = static_cast< int >( playerID );
    if( playerID < static_cast< unsigned int >( MAX_PLAYERS ) )
    {
        e.player = true;
        Avatar* avatar = GetAvatarManager()->GetAvatarForPlayer( playerID );
        if( avatar != NULL )
        {
            e.character = avatar->GetCharacter();
            e.vehicle = avatar->IsInCar() ? avatar->GetVehicle() : NULL;
        }
    }
    else
    {
        e.vehicle = GetTriggerVolumeTracker()->GetAI( static_cast< int >( playerID ) );
    }

    bool matched = false;
    bool tilt = false;
    bool prevent = RunBindings( volume, locator, -1, e, &matched, &tilt );
    if( matched && s_NumInside < MAX_INSIDE )
    {
        Inside& in = s_Inside[ s_NumInside++ ];
        in.locator = locator;
        in.playerID = playerID;
        in.preventDefault = prevent;
        in.tilt = tilt;
        in.tiltGeneration = e.player ? s_Tilt[ playerID ].generation : 0;
    }
    if( Log() && matched )
    {
        fprintf( stderr, "mods: CustomTriggerActions: player %u entered %s%s\n", playerID,
                 locator->GetName() != NULL ? locator->GetName() : "?", prevent ? " (default prevented)" : "" );
    }
    return prevent;
}

bool OnEvent( EventEnum id, void* data )
{
    if( !Active() || id < 0 || id >= NUM_EVENTS || ( s_EventBound[ id >> 3 ] & ( 1 << ( id & 7 ) ) ) == 0 )
    {
        return false;
    }
    Entity e;
    memset( &e, 0, sizeof( e ) );
    bool matched = false;
    bool tilt = false;
    return RunBindings( NULL, NULL, static_cast< int >( id ), e, &matched, &tilt );
}

float CameraTilt( int playerID )
{
    if( !s_Active || playerID < 0 || playerID >= MAX_PLAYERS )
    {
        return 0.0f;
    }
    Tilt& t = s_Tilt[ playerID ];
    if( t.reset && ( t.count > 0 || t.releaseTime != 0 ) &&
        GetGameplayManager() != NULL && GetGameplayManager()->GetCurrentMission() != t.mission )
    {
        // Reset: level again on a mission change; the triggers still held let go of nothing
        t.count = 0;
        t.releaseTime = 0;
        t.generation++;
    }
    if( t.count > 0 )
    {
        return t.radians;
    }
    if( t.releaseTime == 0 )
    {
        return 0.0f;
    }
    unsigned int elapsed = radTimeGetMilliseconds() - t.releaseTime;
    if( elapsed >= t.duration )
    {
        t.releaseTime = 0;
        return 0.0f;
    }
    return t.startRadians * ( 1.0f - static_cast< float >( elapsed ) / static_cast< float >( t.duration ) );
}

}

#endif // RAD_LINUX
