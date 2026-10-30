//=============================================================================
// Linux port: Mod Launcher AdditionalScriptFunctionality (see modscript.h). Command names, argument counts and
// behaviour follow the hack's documentation (version 1.27).
//=============================================================================
#ifdef RAD_LINUX

#include <raddebug.hpp>
#include <radkey.hpp>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <mission/modscript.h>
#include <console/console.h>
#include <mission/missionscriptloader.h>
#include <mission/mission.h>
#include <mission/missionstage.h>
#include <mission/gameplaymanager.h>
#include <mission/objectives/modobjectives.h>
#include <mission/conditions/modconditions.h>
#include <mission/charactersheet/charactersheetmanager.h>
#include <mission/rewards/reward.h>
#include <mission/rewards/rewardsmanager.h>
#include <memory/srrmemory.h>
#include <meta/locator.h>
#include <meta/carstartlocator.h>
#include <meta/zoneeventlocator.h>
#include <render/RenderManager/RenderManager.h>
#include <render/RenderManager/RenderLayer.h>
#include <p3d/utility.hpp>
#include <worldsim/hitnrunmanager.h>
#include <worldsim/coins/coinmanager.h>
#include <worldsim/avatar.h>
#include <worldsim/avatarmanager.h>
#include <worldsim/vehiclecentral.h>
#include <worldsim/redbrick/vehicle.h>
#include <worldsim/redbrick/geometryvehicle.h>
#include <worldsim/character/character.h>
#include <worldsim/character/charactermanager.h>
#include <worldsim/character/charactercontroller.h>
#include <worldsim/ped/pedestrianmanager.h>
#include <worldsim/traffic/trafficmanager.h>
#include <worldsim/parkedcars/parkedcarmanager.h>
#include <gameflow/gameflow.h>
#include <ai/state.h>
#include <ai/statemanager.h>
#include <cheats/cheatinputsystem.h>
#include <events/eventmanager.h>
#include <camera/supercammanager.h>
#include <shar_mods.h>

ModStage::ModStage() :
    payout( 0 ),
    paidOut( false ),
    hitAndRun( -1.0f ),
    resetHitAndRun( false ),
    decay( -1.0f ),
    decayHitAndRun( -1.0f ),
    decayInterior( -1.0f ),
    carChangeHitAndRun( -1.0f ),
    fine( -1 ),
    numChaseCars( -1 ),
    pedGroup( -1 ),
    musicEvent( 0 ),
    allowCancel( -1 ),
    collectibleSoundSet( false ),
    trafficGroup( -1 ),
    dynaLoadDump( false ),
    airGravitySet( false ),
    kickForceSet( false ),
    slamForceSet( false ),
    airGravity( 0.0f ),
    kickForce( 0.0f ),
    slamForce( 0.0f ),
    numTriggers( 0 ),
    numVehicleCharacters( 0 ),
    numVehicleSettings( 0 ),
    checkpoint( false ),
    checkpointPedGroup( -1 ),
    checkpointTrafficGroup( -1 ),
    saved( false ),
    savedDecay( 0.0f ),
    savedDecayHitAndRun( 0.0f ),
    savedDecayInterior( 0.0f ),
    savedFine( 0 ),
    savedNumChaseCars( 0 ),
    savedAirGravity( 0.0f ),
    savedKickForce( 0.0f ),
    savedSlamForce( 0.0f )
{
    collectibleSound[ 0 ] = '\0';
    characterModel[ 0 ] = '\0';
    characterAnimSet[ 0 ] = '\0';
    dynaLoad[ 0 ] = '\0';
    dynaLoadInterior[ 0 ] = '\0';
    checkpointDynaLoad[ 0 ] = '\0';
    checkpointInterior[ 0 ] = '\0';
    checkpointCarLocator[ 0 ] = '\0';
    checkpointPlayerLocator[ 0 ] = '\0';
    for( int i = 0; i < MAX_TRIGGERS; i++ )
    {
        triggers[ i ][ 0 ] = '\0';
        triggerWasActive[ i ] = false;
    }
}

namespace
{

// passengers added with AddStageVehicleCharacter: they stay until removed or the mission ends
struct VehicleCharacter
{
    char car[ 32 ];         // "" once resolved from "current"
    char character[ 32 ];
    char joint[ 16 ];
    float rotation;
    Vehicle* vehicle;       // the car once found
    Character* npc;         // while seated (the car is in the world)
};

const int MAX_PASSENGERS = 8;
VehicleCharacter s_Passengers[ MAX_PASSENGERS ];
int s_NumPassengers = 0;
bool s_Retry = false;       // the mission being set up is a retry (checkpoints apply)
bool s_Loading = false;     // a retry's script is running (IfCurrentCheckpoint)
const char* s_CollectibleSound = NULL;  // the current stage's SetCollectibleSoundEffect (its ModStage's string)
bool s_UseCameraPosition = false;       // a camera objective with SetObjUseCameraPosition is running
// SetStageCharacterModel: the player's own model while a stage's is on (put back when a stage without one starts,
// or the mission ends: not between two stages that both set it, which would reload it)
bool s_ModelSwapped = false;
char s_SavedModel[ 64 ];
char s_SavedAnimSet[ 64 ];

bool Log()
{
    static int s_Log = -1;
    if( s_Log < 0 )
    {
        s_Log = getenv( "SHAR_MOD_LOG" ) != NULL;
    }
    return s_Log != 0;
}

bool ParseBool( const char* value )
{
    return strcasecmp( value, "true" ) == 0 || atoi( value ) != 0;
}

void Copy( char* to, size_t size, const char* from )
{
    snprintf( to, size, "%s", from );
}

// each unsupported command's name once
void ReportUnsupported( const char* name )
{
    static radKey32 s_Reported[ 64 ];
    static int s_NumReported = 0;
    radKey32 key = radMakeCaseInsensitiveKey32( name );
    for( int i = 0; i < s_NumReported; i++ )
    {
        if( s_Reported[ i ] == key )
        {
            return;
        }
    }
    if( s_NumReported < 64 )
    {
        s_Reported[ s_NumReported++ ] = key;
    }
    fprintf( stderr, "mods: AdditionalScriptFunctionality: %s not supported yet, ignored\n", name );
}

Vehicle* FindVehicle( const char* name )
{
    Vehicle* v = GetGameplayManager()->GetMissionVehicleByName( name );
    return v != NULL ? v : GetVehicleCentral()->GetVehicleByName( name );
}

void Unseat( VehicleCharacter& p )
{
    if( p.npc != NULL )
    {
        if( Log() ) fprintf( stderr, "mods: passenger %s leaves %s\n", p.character, p.vehicle != NULL ? p.vehicle->GetName() : "?" );
        GetCharacterManager()->RemoveCharacter( p.npc );
        p.npc->Release();
        p.npc = NULL;
    }
}

// Seats a passenger whose car is in the world (and takes them out when it leaves)
void UpdatePassenger( int index )
{
    VehicleCharacter& p = s_Passengers[ index ];
    if( p.vehicle == NULL && p.car[ 0 ] != '\0' )
    {
        p.vehicle = FindVehicle( p.car );
    }
    bool inWorld = p.vehicle != NULL && GetVehicleCentral()->GetVehicleId( p.vehicle, false ) >= 0;
    if( !inWorld )
    {
        Unseat( p );
        return;
    }
    if( p.npc != NULL || p.vehicle->IsVehicleDestroyed() )
    {
        return;
    }
    tPose* pose = p.vehicle->mGeometryVehicle != NULL ? p.vehicle->mGeometryVehicle->GetP3DPose() : NULL;
    int joint = pose != NULL ? pose->FindJointIndex( p.joint ) : -1;
    if( joint < 0 )
    {
        if( Log() ) fprintf( stderr, "mods: passenger %s: %s has no joint %s\n", p.character, p.vehicle->GetName(), p.joint );
        p.vehicle = NULL;   // as the Mod Launcher: no such joint, no passenger
        p.car[ 0 ] = '\0';
        return;
    }
    char name[ 16 ];
    snprintf( name, sizeof( name ), "vc%d_%s", index, p.character );
    Character* c = GetCharacterManager()->AddCharacter( CharacterManager::NPC, name, p.character, "npd", "" );
    if( c == NULL )
    {
        return;
    }
    c->AddRef();
    static_cast< NPCController* >( c->GetController() )->TransitToState( NPCController::NONE );
    c->SetModSeat( pose->GetJoint( joint )->objectMatrix.Row( 3 ), p.rotation * rmt::PI / 180.0f );
    c->SetTargetVehicle( p.vehicle );
    c->AddToWorldScene();
    c->GetStateManager()->SetState< CharacterAi::InCar >();
    p.npc = c;
    if( Log() ) fprintf( stderr, "mods: passenger %s seated in %s at %s\n", p.character, p.vehicle->GetName(), p.joint );
}

void AddPassenger( const ModStageVehicleCharacter& add )
{
    for( int i = 0; i < s_NumPassengers; i++ )
    {
        if( strcasecmp( s_Passengers[ i ].character, add.character ) == 0 && s_Passengers[ i ].vehicle != NULL &&
            ( strcasecmp( add.car, "current" ) == 0 ? s_Passengers[ i ].vehicle == GetGameplayManager()->GetCurrentVehicle()
                                                    : strcasecmp( s_Passengers[ i ].car, add.car ) == 0 ) )
        {
            return;     // already there (a stage replayed on a retry)
        }
    }
    if( s_NumPassengers >= MAX_PASSENGERS )
    {
        return;
    }
    VehicleCharacter& p = s_Passengers[ s_NumPassengers++ ];
    memset( &p, 0, sizeof( p ) );
    Copy( p.character, sizeof( p.character ), add.character );
    Copy( p.joint, sizeof( p.joint ), add.joint );
    p.rotation = add.rotation;
    if( strcasecmp( add.car, "current" ) == 0 )
    {
        p.vehicle = GetGameplayManager()->GetCurrentVehicle();
    }
    else
    {
        Copy( p.car, sizeof( p.car ), add.car );
    }
    UpdatePassenger( s_NumPassengers - 1 );
}

// "current" means the car the player had when the passenger was added, so match on the character for it
void RemovePassenger( const ModStageVehicleCharacter& remove )
{
    bool current = strcasecmp( remove.car, "current" ) == 0;
    for( int i = 0; i < s_NumPassengers; i++ )
    {
        VehicleCharacter& p = s_Passengers[ i ];
        if( strcasecmp( p.character, remove.character ) == 0 &&
            ( current ? p.car[ 0 ] == '\0' : strcasecmp( p.car, remove.car ) == 0 ) )
        {
            Unseat( p );
            s_Passengers[ i ] = s_Passengers[ --s_NumPassengers ];
            return;
        }
    }
}

// cars whose settings the stages changed, held until the mission ends and gets them back
struct ChangedVehicle
{
    Vehicle* vehicle;
    bool current;           // named "current" (ResetStageVehicleAbductable( "current" ) finds it)
    int abductable;         // as before
    bool allowSlide;
};

const int MAX_CHANGED_VEHICLES = 16;
ChangedVehicle s_Changed[ MAX_CHANGED_VEHICLES ];
int s_NumChanged = 0;

ChangedVehicle* TrackVehicle( Vehicle* v, bool current )
{
    for( int i = 0; i < s_NumChanged; i++ )
    {
        if( s_Changed[ i ].vehicle == v )
        {
            s_Changed[ i ].current = s_Changed[ i ].current || current;
            return &s_Changed[ i ];
        }
    }
    if( s_NumChanged >= MAX_CHANGED_VEHICLES )
    {
        return NULL;
    }
    ChangedVehicle& c = s_Changed[ s_NumChanged++ ];
    c.vehicle = v;
    c.current = current;
    c.abductable = v->mModAbductable;
    c.allowSlide = v->mAllowSlide;
    v->AddRef();
    return &c;
}

void RestoreVehicles()
{
    for( int i = 0; i < s_NumChanged; i++ )
    {
        s_Changed[ i ].vehicle->mModAbductable = s_Changed[ i ].abductable;
        s_Changed[ i ].vehicle->mAllowSlide = s_Changed[ i ].allowSlide;
        s_Changed[ i ].vehicle->Release();
    }
    s_NumChanged = 0;
}

void ApplyVehicleSettings( MissionStage* stage )
{
    ModStage* mod = stage->GetModStage();
    for( int i = 0; mod != NULL && i < mod->numVehicleSettings; i++ )
    {
        const ModStageVehicleSetting& s = mod->vehicleSettings[ i ];
        bool current = strcasecmp( s.car, "current" ) == 0;
        Vehicle* v = NULL;
        if( s.kind == ModStageVehicleSetting::RESET_ABDUCTABLE && current )
        {
            // the car "current" was when it was set
            for( int j = s_NumChanged - 1; j >= 0 && v == NULL; j-- )
            {
                if( s_Changed[ j ].current ) v = s_Changed[ j ].vehicle;
            }
        }
        else
        {
            v = current ? GetGameplayManager()->GetCurrentVehicle() : FindVehicle( s.car );
        }
        if( v == NULL )
        {
            if( Log() ) fprintf( stderr, "mods: stage vehicle setting: no car %s\n", s.car );
            continue;
        }
        switch( s.kind )
        {
        case ModStageVehicleSetting::ABDUCTABLE:
            if( TrackVehicle( v, current ) != NULL ) v->mModAbductable = s.value ? 1 : 0;
            break;
        case ModStageVehicleSetting::RESET_ABDUCTABLE:
            v->mModAbductable = -1;
            break;
        case ModStageVehicleSetting::SEAT_SLIDE:
            if( TrackVehicle( v, current ) != NULL ) v->mAllowSlide = s.value;
            break;
        case ModStageVehicleSetting::NO_DESTROYED_JUMP_OUT:
            v->mModNoDestroyedJumpOut = true;   // as documented: for as long as the car exists
            break;
        }
        if( Log() ) fprintf( stderr, "mods: stage vehicle setting %d = %d on %s\n", s.kind, s.value, v->GetName() );
    }
}

Character* PlayerCharacter()
{
    Avatar* avatar = GetAvatarManager()->GetAvatarForPlayer( 0 );
    return avatar != NULL ? avatar->GetCharacter() : NULL;
}

void SwapPlayerModel( Character* c, const char* model, const char* animSet )
{
    const char* currentModel = GetCharacterManager()->GetModelName( c );
    const char* currentAnim = GetCharacterManager()->GetAnimName( c );
    if( currentModel != NULL && currentAnim != NULL && strcasecmp( currentModel, model ) == 0 &&
        strcasecmp( currentAnim, animSet ) == 0 )
    {
        return;
    }
    if( Log() ) fprintf( stderr, "mods: player model %s (%s)\n", model, animSet );
    GetCharacterManager()->SwapData( c, model, animSet );
}

void RestorePlayerModel()
{
    if( !s_ModelSwapped )
    {
        return;
    }
    s_ModelSwapped = false;
    // not while leaving the level or the game (the next level loads the skin anyway)
    ContextEnum context = GetGameFlow()->GetCurrentContext();
    ContextEnum next = GetGameFlow()->GetNextContext();
    Character* c = PlayerCharacter();
    if( c != NULL && ( context == CONTEXT_GAMEPLAY || context == CONTEXT_PAUSE ) &&
        ( next == CONTEXT_GAMEPLAY || next == CONTEXT_PAUSE ) )
    {
        SwapPlayerModel( c, s_SavedModel, s_SavedAnimSet );
    }
}

void ApplyPlayerModel( ModStage* mod )
{
    if( mod == NULL || mod->characterModel[ 0 ] == '\0' )
    {
        RestorePlayerModel();
        return;
    }
    Character* c = PlayerCharacter();
    if( c == NULL || GetCharacterManager()->GetModelName( c ) == NULL || GetCharacterManager()->GetAnimName( c ) == NULL )
    {
        return;
    }
    if( !s_ModelSwapped )
    {
        s_ModelSwapped = true;
        Copy( s_SavedModel, sizeof( s_SavedModel ), GetCharacterManager()->GetModelName( c ) );
        Copy( s_SavedAnimSet, sizeof( s_SavedAnimSet ), GetCharacterManager()->GetAnimName( c ) );
    }
    // the animation set defaults to the player's own (as the launcher since 1.19)
    SwapPlayerModel( c, mod->characterModel, mod->characterAnimSet[ 0 ] != '\0' ? mod->characterAnimSet : s_SavedAnimSet );
}

void ApplyPassengers( MissionStage* stage )
{
    ModStage* mod = stage->GetModStage();
    for( int i = 0; mod != NULL && i < mod->numVehicleCharacters; i++ )
    {
        if( mod->vehicleCharacters[ i ].remove )
        {
            RemovePassenger( mod->vehicleCharacters[ i ] );
        }
        else
        {
            AddPassenger( mod->vehicleCharacters[ i ] );
        }
    }
}

}

//=============================================================================
// The commands
//=============================================================================
class ModScriptCommands
{
public:
    static MissionScriptLoader* Loader() { return MissionScriptLoader::spInstance; }

    // the stage being scripted, with its ModStage (made on the mission heap on first use)
    static ModStage* Stage( const char* command )
    {
        MissionStage* stage = Loader()->mpStage;
        if( stage == NULL )
        {
            fprintf( stderr, "mods: %s outside a stage, ignored\n", command );
            return NULL;
        }
        if( stage->GetModStage() == NULL )
        {
            HeapMgr()->PushHeap( Loader()->mMissionHeap );
            stage->SetModStage( new ModStage() );
            HeapMgr()->PopHeap( Loader()->mMissionHeap );
        }
        return stage->GetModStage();
    }

    template< typename T > static T* Objective( const char* command )
    {
        T* objective = dynamic_cast< T* >( Loader()->mpObjective );
        if( objective == NULL )
        {
            fprintf( stderr, "mods: %s does not apply to this objective, ignored\n", command );
        }
        return objective;
    }

    template< typename T > static T* Condition( const char* command )
    {
        T* condition = dynamic_cast< T* >( Loader()->mpCondition );
        if( condition == NULL )
        {
            fprintf( stderr, "mods: %s does not apply to this condition, ignored\n", command );
        }
        return condition;
    }

    static ModCounter* ObjectiveCounter( const char* command )
    {
        ModCountObjective* o = Objective< ModCountObjective >( command );
        return o != NULL ? &o->GetCounter() : NULL;
    }

    static ModCounter* ConditionCounter( const char* command )
    {
        ModCountCondition* c = Condition< ModCountCondition >( command );
        return c != NULL ? &c->GetCounter() : NULL;
    }

    static void Unsupported( int argc, char** argv ) { ReportUnsupported( argv[ 0 ] ); }
    static bool UnsupportedConditional( int argc, char** argv ) { ReportUnsupported( argv[ 0 ] ); return false; }

    // Objectives
    static void AddObjTargetModel( int argc, char** argv )
    {
        ModCounter* c = ObjectiveCounter( argv[ 0 ] );
        if( c != NULL ) c->AddModel( argv[ 1 ] );
    }
    static void SetObjTotal( int argc, char** argv )
    {
        ModCounter* c = ObjectiveCounter( argv[ 0 ] );
        if( c != NULL ) c->total = atoi( argv[ 1 ] );
    }
    static void SetObjSpeedKMH( int argc, char** argv )
    {
        ModCounter* c = ObjectiveCounter( argv[ 0 ] );
        if( c != NULL ) c->speedKmh = static_cast< float >( atof( argv[ 1 ] ) );
    }
    static void SetObjCameraName( int argc, char** argv )
    {
        ModCameraObjective* o = Objective< ModCameraObjective >( argv[ 0 ] );
        if( o != NULL ) o->SetCameraName( argv[ 1 ] );
    }
    static void SetObjMulticontName( int argc, char** argv )
    {
        ModCameraObjective* o = Objective< ModCameraObjective >( argv[ 0 ] );
        if( o != NULL ) o->SetMulticontrollerName( argv[ 1 ] );
    }
    static void SetObjCanSkip( int argc, char** argv )
    {
        ModCameraObjective* o = Objective< ModCameraObjective >( argv[ 0 ] );
        if( o != NULL ) o->SetCanSkip( ParseBool( argv[ 1 ] ) );
    }
    static void SetObjNoLetterbox( int argc, char** argv )
    {
        ModCameraObjective* o = Objective< ModCameraObjective >( argv[ 0 ] );
        if( o != NULL ) o->SetNoLetterbox( ParseBool( argv[ 1 ] ) );
    }
    static void SetObjUseCameraPosition( int argc, char** argv )
    {
        ModCameraObjective* o = Objective< ModCameraObjective >( argv[ 0 ] );
        if( o != NULL ) o->SetUseCameraPosition( ParseBool( argv[ 1 ] ) );
    }
    static void SetObjExplosion( int argc, char** argv )
    {
        ModCountObjective* o = Objective< ModCountObjective >( argv[ 0 ] );
        if( o == NULL || o->GetCounter().kind != ModCounter::HIT_PEDS )
        {
            if( o != NULL ) fprintf( stderr, "mods: %s does not apply to this objective, ignored\n", argv[ 0 ] );
            return;
        }
        float scale = static_cast< float >( atof( argv[ 2 ] ) );
        o->SetExplosion( atoi( argv[ 1 ] ), scale, argc > 3 ? static_cast< float >( atof( argv[ 3 ] ) ) : scale );
    }

    // SetObj/SetCondSound, SetObj/SetCondTrigger, ...: hitpeds counters and insidetrigger/outsidetrigger
    static ModTrigger* ObjectiveTrigger( const char* command )
    {
        ModTriggerObjective* o = Objective< ModTriggerObjective >( command );
        return o != NULL ? &o->GetTrigger() : NULL;
    }
    static ModTrigger* ConditionTrigger( const char* command )
    {
        ModTriggerCondition* c = Condition< ModTriggerCondition >( command );
        return c != NULL ? &c->GetTrigger() : NULL;
    }
    static void SetSound( int argc, char** argv, ModCounter* counter, ModTrigger* trigger )
    {
        if( counter != NULL && counter->kind == ModCounter::HIT_PEDS )
        {
            Copy( counter->sound, sizeof( counter->sound ), argv[ 1 ] );
        }
        else if( trigger != NULL )
        {
            if( !trigger->SetSound( argc, argv ) )
            {
                fprintf( stderr, "mods: %s: no event, or one that does not apply to this type, ignored\n", argv[ 0 ] );
            }
        }
        else
        {
            fprintf( stderr, "mods: %s does not apply here, ignored\n", argv[ 0 ] );
        }
    }
    static void SetObjSound( int argc, char** argv )
    {
        MissionObjective* o = Loader()->mpObjective;
        ModCountObjective* count = dynamic_cast< ModCountObjective* >( o );
        ModTriggerObjective* trigger = dynamic_cast< ModTriggerObjective* >( o );
        SetSound( argc, argv, count != NULL ? &count->GetCounter() : NULL, trigger != NULL ? &trigger->GetTrigger() : NULL );
    }
    static void SetCondSound( int argc, char** argv )
    {
        MissionCondition* c = Loader()->mpCondition;
        ModCountCondition* count = dynamic_cast< ModCountCondition* >( c );
        ModTriggerCondition* trigger = dynamic_cast< ModTriggerCondition* >( c );
        SetSound( argc, argv, count != NULL ? &count->GetCounter() : NULL, trigger != NULL ? &trigger->GetTrigger() : NULL );
    }
    static void SetTrigger( ModTrigger* t, int argc, char** argv ) { if( t != NULL ) t->SetLocator( argv[ 1 ] ); }
    static void SetThreshold( ModTrigger* t, int argc, char** argv )
    {
        if( t != NULL ) t->threshold = static_cast< float >( atof( argv[ 1 ] ) );
    }
    static void SetDecay( ModTrigger* t, int argc, char** argv )
    {
        if( t != NULL ) t->SetDecay( static_cast< float >( atof( argv[ 1 ] ) ), argc > 2 ? static_cast< float >( atof( argv[ 2 ] ) ) : 0.0f );
    }
    static void SetMessageIndex( ModTrigger* t, int argc, char** argv ) { if( t != NULL ) t->messageIndex = atoi( argv[ 1 ] ); }
    static void SetObjTrigger( int argc, char** argv ) { SetTrigger( ObjectiveTrigger( argv[ 0 ] ), argc, argv ); }
    static void SetObjThreshold( int argc, char** argv ) { SetThreshold( ObjectiveTrigger( argv[ 0 ] ), argc, argv ); }
    static void SetObjDecay( int argc, char** argv ) { SetDecay( ObjectiveTrigger( argv[ 0 ] ), argc, argv ); }
    static void SetObjMessageIndex( int argc, char** argv ) { SetMessageIndex( ObjectiveTrigger( argv[ 0 ] ), argc, argv ); }
    static void SetCondTrigger( int argc, char** argv ) { SetTrigger( ConditionTrigger( argv[ 0 ] ), argc, argv ); }
    static void SetCondThreshold( int argc, char** argv ) { SetThreshold( ConditionTrigger( argv[ 0 ] ), argc, argv ); }
    static void SetCondDecay( int argc, char** argv ) { SetDecay( ConditionTrigger( argv[ 0 ] ), argc, argv ); }
    static void SetCondMessageIndex( int argc, char** argv ) { SetMessageIndex( ConditionTrigger( argv[ 0 ] ), argc, argv ); }

    // Conditions
    static void AddCondTargetModel( int argc, char** argv )
    {
        ModCounter* c = ConditionCounter( argv[ 0 ] );
        if( c != NULL ) c->AddModel( argv[ 1 ] );
    }
    static void SetCondTotal( int argc, char** argv )
    {
        ModCounter* c = ConditionCounter( argv[ 0 ] );
        if( c != NULL ) c->total = atoi( argv[ 1 ] );
    }
    static void SetCondDelay( int argc, char** argv )
    {
        ModCountCondition* c = Condition< ModCountCondition >( argv[ 0 ] );
        if( c != NULL ) c->SetDelay( static_cast< float >( atof( argv[ 1 ] ) ) );
    }
    static void SetCondSpeedRangeKMH( int argc, char** argv )
    {
        ModMaintainSpeedCondition* c = Condition< ModMaintainSpeedCondition >( argv[ 0 ] );
        if( c != NULL ) c->SetRange( static_cast< float >( atof( argv[ 1 ] ) ), static_cast< float >( atof( argv[ 2 ] ) ) );
    }
    static void SetCondDisplay( int argc, char** argv )
    {
        if( !ModSetConditionDisplay( Loader()->mpCondition, argv[ 1 ] ) )
        {
            fprintf( stderr, "mods: %s does not apply to this condition, ignored\n", argv[ 0 ] );
        }
    }

    // Stages
    static void DisableTrigger( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL && s->numTriggers < ModStage::MAX_TRIGGERS )
        {
            Copy( s->triggers[ s->numTriggers++ ], sizeof( s->triggers[ 0 ] ), argv[ 1 ] );
        }
    }
    static void SetStagePayout( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->payout = atoi( argv[ 1 ] );
    }
    static void SetStageHitAndRun( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->hitAndRun = rmt::Clamp( static_cast< float >( atof( argv[ 1 ] ) ), 0.0f, 100.0f );
    }
    static void ResetStageHitAndRun( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->resetHitAndRun = true;
    }
    static void SetStageHitAndRunDecay( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->decay = static_cast< float >( atof( argv[ 1 ] ) );
    }
    static void SetStageHitAndRunDecayHitAndRun( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->decayHitAndRun = static_cast< float >( atof( argv[ 1 ] ) );
    }
    static void SetStageHitAndRunDecayInterior( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->decayInterior = static_cast< float >( atof( argv[ 1 ] ) );
    }
    static void SetStageHitAndRunFine( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->fine = atoi( argv[ 1 ] );
    }
    static void SetStageNumChaseCars( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->numChaseCars = atoi( argv[ 1 ] );
    }
    static void SetStageCarChangeHitAndRunChange( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->carChangeHitAndRun = static_cast< float >( atof( argv[ 1 ] ) );
    }
    static void SetStagePedGroup( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        int group = atoi( argv[ 1 ] );
        if( s != NULL && group >= 0 && group < PedestrianManager::GetMaxModelGroups() ) s->pedGroup = group;
    }
    static void SetStageMusicEvent( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->musicEvent = ::radMakeCaseInsensitiveKey32( argv[ 1 ] );
    }
    static void SetStageAllowMissionCancel( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->allowCancel = ParseBool( argv[ 1 ] ) ? 1 : 0;
    }
    static void SetStageCharacterModel( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s == NULL ) return;
        Copy( s->characterModel, sizeof( s->characterModel ), argv[ 1 ] );
        Copy( s->characterAnimSet, sizeof( s->characterAnimSet ), argc > 2 ? argv[ 2 ] : "" );
    }
    static void SetStageAirGravity( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        float gravity = static_cast< float >( atof( argv[ 1 ] ) );
        if( s != NULL && gravity < 0.0f )    // the docs: 0 and up are ignored (they break the game)
        {
            s->airGravitySet = true;
            s->airGravity = gravity;
        }
    }
    static void SetStageKickForce( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) { s->kickForceSet = true; s->kickForce = static_cast< float >( atof( argv[ 1 ] ) ); }
    }
    static void SetStageSlamForce( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) { s->slamForceSet = true; s->slamForce = static_cast< float >( atof( argv[ 1 ] ) ); }
    }
    static void StageDynaLoadData( int argc, char** argv, bool dump )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s == NULL ) return;
        Copy( s->dynaLoad, sizeof( s->dynaLoad ), argv[ 1 ] );
        Copy( s->dynaLoadInterior, sizeof( s->dynaLoadInterior ), argc > 2 ? argv[ 2 ] : "" );
        s->dynaLoadDump = dump;
    }
    static void AddStageDynaLoadData( int argc, char** argv ) { StageDynaLoadData( argc, argv, false ); }
    static void SetStageDynaLoadData( int argc, char** argv ) { StageDynaLoadData( argc, argv, true ); }
    static void SetStageTrafficGroup( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->trafficGroup = atoi( argv[ 1 ] );
    }
    static void SetCollectibleSoundEffect( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s == NULL ) return;
        s->collectibleSoundSet = true;
        Copy( s->collectibleSound, sizeof( s->collectibleSound ), strcasecmp( argv[ 1 ], "none" ) == 0 ? "" : argv[ 1 ] );
    }

    // Missions (between SelectMission and CloseMission)
    static Mission* ScriptedMission( const char* command )
    {
        Mission* mission = Loader()->mpMission;
        if( mission == NULL ) fprintf( stderr, "mods: %s outside a mission, ignored\n", command );
        return mission;
    }
    static void SetPedsEnabled( int argc, char** argv )
    {
        Mission* m = ScriptedMission( argv[ 0 ] );
        if( m != NULL ) m->mModPedsEnabled = ParseBool( argv[ 1 ] ) ? 1 : 0;
    }
    static void UseTrafficGroup( int argc, char** argv )
    {
        Mission* m = ScriptedMission( argv[ 0 ] );
        if( m != NULL ) m->mModTrafficGroup = atoi( argv[ 1 ] );
    }
    static void SetParkedCarsEnabled( int argc, char** argv )
    {
        Mission* m = ScriptedMission( argv[ 0 ] );
        if( m != NULL ) m->mModParkedCarsEnabled = ParseBool( argv[ 1 ] ) ? 1 : 0;
    }
    // a parked car type without a traffic group entry, as AddTrafficModel adds one
    static void AddParkedCar( int argc, char** argv )
    {
        ContextEnum context = GetGameFlow()->GetCurrentContext();
        if( context == CONTEXT_GAMEPLAY || context == CONTEXT_LOADING_GAMEPLAY )
        {
            GetPCM().AddCarType( argv[ 1 ] );
        }
    }
    static void StageVehicleCharacter( int argc, char** argv, bool remove )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s == NULL || s->numVehicleCharacters >= ModStage::MAX_VEHICLE_CHARACTERS )
        {
            return;
        }
        ModStageVehicleCharacter& v = s->vehicleCharacters[ s->numVehicleCharacters++ ];
        Copy( v.car, sizeof( v.car ), argv[ 1 ] );
        Copy( v.character, sizeof( v.character ), argv[ 2 ] );
        Copy( v.joint, sizeof( v.joint ), argc > 3 ? argv[ 3 ] : "pl" );
        v.rotation = argc > 4 ? static_cast< float >( atof( argv[ 4 ] ) ) : 180.0f;
        v.remove = remove;
    }
    static void AddStageVehicleCharacter( int argc, char** argv ) { StageVehicleCharacter( argc, argv, false ); }
    static void StageVehicleSetting( const char* command, const char* car, ModStageVehicleSetting::Kind kind, bool value )
    {
        ModStage* s = Stage( command );
        if( s == NULL || s->numVehicleSettings >= ModStage::MAX_VEHICLE_SETTINGS )
        {
            return;
        }
        ModStageVehicleSetting& v = s->vehicleSettings[ s->numVehicleSettings++ ];
        Copy( v.car, sizeof( v.car ), car );
        v.kind = kind;
        v.value = value;
    }
    static void SetStageVehicleAbductable( int argc, char** argv )
    {
        StageVehicleSetting( argv[ 0 ], argv[ 1 ], ModStageVehicleSetting::ABDUCTABLE, ParseBool( argv[ 2 ] ) );
    }
    static void ResetStageVehicleAbductable( int argc, char** argv )
    {
        StageVehicleSetting( argv[ 0 ], argv[ 1 ], ModStageVehicleSetting::RESET_ABDUCTABLE, false );
    }
    static void SetStageVehicleAllowSeatSlide( int argc, char** argv )
    {
        StageVehicleSetting( argv[ 0 ], argv[ 1 ], ModStageVehicleSetting::SEAT_SLIDE, ParseBool( argv[ 2 ] ) );
    }
    static void SetStageVehicleNoDestroyedJumpOut( int argc, char** argv )
    {
        StageVehicleSetting( argv[ 0 ], argv[ 1 ], ModStageVehicleSetting::NO_DESTROYED_JUMP_OUT, true );
    }
    // a stage vehicle (made when the script adds it): this stage's, else an earlier one's; kept by the car, so it
    // holds for whichever AI drives it (ActivateVehicle adds one later)
    static void SetStageVehicleReset( int argc, char** argv )
    {
        Mission* mission = Loader()->mpMission;
        MissionStage* current = Loader()->mpStage;
        if( mission == NULL || current == NULL )
        {
            fprintf( stderr, "mods: %s outside a stage, ignored\n", argv[ 0 ] );
            return;
        }
        for( int i = -1; i < mission->GetNumStages(); i++ )
        {
            MissionStage* stage = i < 0 ? current : mission->GetStage( i );
            for( int j = 0; stage != NULL && j < stage->GetNumVehicles(); j++ )
            {
                Vehicle* v = stage->GetVehicle( j );
                if( v != NULL && strcasecmp( v->GetName(), argv[ 1 ] ) == 0 )
                {
                    v->mModReset = ParseBool( argv[ 2 ] ) ? 1 : 0;
                    if( Log() ) fprintf( stderr, "mods: %s resets onto the road: %d\n", v->GetName(), v->mModReset );
                    return;
                }
            }
        }
        fprintf( stderr, "mods: %s: no stage vehicle %s, ignored\n", argv[ 0 ], argv[ 1 ] );
    }
    static void RemoveStageVehicleCharacter( int argc, char** argv ) { StageVehicleCharacter( argc, argv, true ); }

    // Checkpoints
    static void CHECKPOINT_HERE( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->checkpoint = true;
    }
    static void SetCheckpointDynaLoadData( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s == NULL ) return;
        Copy( s->checkpointDynaLoad, sizeof( s->checkpointDynaLoad ), argv[ 1 ] );
        Copy( s->checkpointInterior, sizeof( s->checkpointInterior ), argc > 2 ? argv[ 2 ] : "" );
    }
    static void SetCheckpointResetPlayerInCar( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s == NULL ) return;
        Copy( s->checkpointCarLocator, sizeof( s->checkpointCarLocator ), argv[ 1 ] );
        s->checkpointPlayerLocator[ 0 ] = '\0';
    }
    static void SetCheckpointResetPlayerOutCar( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s == NULL ) return;
        Copy( s->checkpointPlayerLocator, sizeof( s->checkpointPlayerLocator ), argv[ 1 ] );
        Copy( s->checkpointCarLocator, sizeof( s->checkpointCarLocator ), argv[ 2 ] );
    }
    static void SetCheckpointPedGroup( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        int group = atoi( argv[ 1 ] );
        if( s != NULL && group >= 0 && group < PedestrianManager::GetMaxModelGroups() ) s->checkpointPedGroup = group;
    }
    static void SetCheckpointTrafficGroup( int argc, char** argv )
    {
        ModStage* s = Stage( argv[ 0 ] );
        if( s != NULL ) s->checkpointTrafficGroup = atoi( argv[ 1 ] );
    }
    // the number (from 0) of the checkpoint the stage being scripted has, -1 for none
    static int ScriptedCheckpoint()
    {
        Mission* mission = Loader()->mpMission;
        MissionStage* current = Loader()->mpStage;
        if( mission == NULL || current == NULL || current->GetModStage() == NULL || !current->GetModStage()->checkpoint )
        {
            return -1;
        }
        int number = -1;
        for( int i = 0; i < mission->GetNumStages(); i++ )
        {
            MissionStage* stage = mission->GetStage( i );
            if( stage != NULL && stage->GetModStage() != NULL && stage->GetModStage()->checkpoint )
            {
                number++;
            }
            if( stage == current )
            {
                break;
            }
        }
        return number;
    }
    static bool IfCurrentCheckpoint( int argc, char** argv )
    {
        int number = ScriptedCheckpoint();
        return s_Loading && number >= 0 && number == Loader()->mpMission->mModCheckpoint;
    }

    // Level and mission settings
    static void SetHitAndRunDecayHitAndRun( int argc, char** argv )
    {
        GetHitnRunManager()->SetDecayRateWhileSpawning( static_cast< float >( atof( argv[ 1 ] ) ) );
    }
    static void SetHitAndRunDecayInterior( int argc, char** argv )
    {
        GetHitnRunManager()->SetDecayRateInside( static_cast< float >( atof( argv[ 1 ] ) ) );
    }
    static void SetHitAndRunFine( int argc, char** argv )
    {
        GetHitnRunManager()->SetBustedCoins( atoi( argv[ 1 ] ) );
    }
    static void SetCarChangeHitAndRunChange( int argc, char** argv )
    {
        GetHitnRunManager()->SetChangeVehicleDelta( static_cast< float >( atof( argv[ 1 ] ) ) );
    }
    // Conditionals (evaluated as the script runs)
    static bool IfStartedInCar( int argc, char** argv )
    {
        Avatar* avatar = GetAvatarManager()->GetAvatarForPlayer( 0 );
        return avatar != NULL && avatar->IsInCar();
    }
    static bool IfCurrentLevel( int argc, char** argv )
    {
        int level = static_cast< int >( GetGameplayManager()->GetCurrentLevelIndex() ) + 1;
        for( int i = 1; i < argc; i++ )
        {
            if( atoi( argv[ i ] ) == level ) return true;
        }
        return false;
    }
    static bool IfCurrentCar( int argc, char** argv )
    {
        Vehicle* v = GetGameplayManager()->GetCurrentVehicle();
        for( int i = 1; v != NULL && i < argc; i++ )
        {
            if( strcasecmp( v->GetName(), argv[ i ] ) == 0 ) return true;
        }
        return false;
    }
    static bool IfCurrentSkin( int argc, char** argv )
    {
        const char* skin = GetCharacterSheetManager()->QueryCurrentSkin( GetGameplayManager()->GetCurrentLevelIndex() );
        for( int i = 1; skin != NULL && i < argc; i++ )
        {
            if( strcasecmp( skin, argv[ i ] ) == 0 ) return true;
        }
        return false;
    }
    static bool IfRewardUnlocked( int argc, char** argv )
    {
        Reward::eRewardType type = ( strcasecmp( argv[ 1 ], "skin" ) == 0 || atoi( argv[ 1 ] ) == 1 ) ? Reward::ALT_SKIN_OTHER
                                 : Reward::ALT_PLAYERCAR;
        tUID uid = tEntity::MakeUID( argv[ 2 ] );
        for( int level = RenderEnums::L1; level <= RenderEnums::L7; level++ )
        {
            Reward* reward = GetRewardsManager()->GetReward( level, uid );
            if( reward != NULL && ( reward->GetRewardType() == type ||
                                    ( type == Reward::ALT_SKIN_OTHER && reward->GetRewardType() == Reward::ALT_SKIN_GOOD ) ) )
            {
                return reward->RewardStatus();
            }
        }
        return false;
    }
    // "l1m1", "l2sr3", "l3bm1", "l4gr1"
    static bool MissionCompleted( const char* id )
    {
        if( tolower( id[ 0 ] ) != 'l' || id[ 1 ] < '1' || id[ 1 ] > '7' )
        {
            return false;
        }
        RenderEnums::LevelEnum level = static_cast< RenderEnums::LevelEnum >( id[ 1 ] - '1' );
        char name[ 16 ];
        Copy( name, sizeof( name ), id + 2 );
        for( char* c = name; *c != '\0'; c++ ) *c = static_cast< char >( tolower( *c ) );
        CharacterSheetManager* sheet = GetCharacterSheetManager();
        MissionRecord* record = NULL;
        if( strncmp( name, "sr", 2 ) == 0 ) record = sheet->QueryStreetRaceStatus( level, name );
        else if( strncmp( name, "bm", 2 ) == 0 ) record = sheet->QueryBonusMissionStatus( level, name );
        else if( strncmp( name, "gr", 2 ) == 0 ) return sheet->GetGambleRaceBestTime( level ) >= 0;
        else record = sheet->QueryMissionStatus( level, name );
        return record != NULL && record->mCompleted;
    }
    static bool IfAllMissionsCompleted( int argc, char** argv )
    {
        for( int i = 1; i < argc; i++ )
        {
            if( !MissionCompleted( argv[ i ] ) ) return false;
        }
        return true;
    }
    static bool IfAnyMissionCompleted( int argc, char** argv )
    {
        for( int i = 1; i < argc; i++ )
        {
            if( MissionCompleted( argv[ i ] ) ) return true;
        }
        return false;
    }
    static bool CheatEnabled( const char* name )
    {
        static const struct { const char* name; eCheatID id; } CHEATS[] =
        {
            { "UnlockAllOutfits", CHEAT_ID_UNLOCK_SKINS }, { "UnlockAllStoryMissions", CHEAT_ID_UNLOCK_MISSIONS },
            { "UnlockAllMovies", CHEAT_ID_UNLOCK_MOVIES }, { "UnlockAllRewardVehicles", CHEAT_ID_UNLOCK_VEHICLES },
            { "UnlockAllCameras", CHEAT_ID_UNLOCK_CAMERAS }, { "NoTopSpeed", CHEAT_ID_NO_TOP_SPEED },
            { "HighAcceleration", CHEAT_ID_HIGH_ACCELERATION }, { "CarJumpOnHorn", CHEAT_ID_CAR_JUMP_ON_HORN },
            { "FlamingCar", CHEAT_ID_FULL_DAMAGE_TO_CAR }, { "OneTapTrafficDeath", CHEAT_ID_ONE_TAP_TRAFFIC_DEATH },
            { "x5StageTime", CHEAT_ID_EXTRA_TIME }, { "KickSwapsCharacterModel", CHEAT_ID_KICK_TOGGLES_CHARACTER_MODEL },
            { "PlayCreditsDialogue", CHEAT_ID_PLAY_CREDITS_DIALOG }, { "ShowSpeedometer", CHEAT_ID_SHOW_SPEEDOMETER },
            { "RedBrick", CHEAT_ID_REDBRICK }, { "InvincibleCar", CHEAT_ID_INVINCIBLE_CAR },
            { "ShowTree", CHEAT_ID_SHOW_TREE }, { "Trippy", CHEAT_ID_TRIPPY },
        };
        for( unsigned int i = 0; i < sizeof( CHEATS ) / sizeof( CHEATS[ 0 ] ); i++ )
        {
            if( strcasecmp( name, CHEATS[ i ].name ) == 0 )
            {
                return GetCheatInputSystem()->IsCheatEnabled( CHEATS[ i ].id );
            }
        }
        return false;
    }
    static bool IfAllCheatsEnabled( int argc, char** argv )
    {
        for( int i = 1; i < argc; i++ )
        {
            if( !CheatEnabled( argv[ i ] ) ) return false;
        }
        return true;
    }
    static bool IfAnyCheatEnabled( int argc, char** argv )
    {
        for( int i = 1; i < argc; i++ )
        {
            if( CheatEnabled( argv[ i ] ) ) return true;
        }
        return false;
    }

    // Add/SetStageDynaLoadData on reaching the stage: Set dumps every loaded zone first and loads as a mission start
    // does (the game's debug teleport does the same); Add loads alongside, as street race props do
    static void StageDynaLoad( ModStage* mod )
    {
        Mission* mission = GetGameplayManager()->GetCurrentMission();
        if( mission == NULL ) return;
        ZoneEventLocator* zone = new( GetGameplayManager()->GetCurrentMissionHeap() ) ZoneEventLocator;
        tRefCounted::Assign( mission->mModStageDynaLoad, zone );
        zone->SetZoneSize( strlen( mod->dynaLoad ) + 1 );
        zone->SetZone( mod->dynaLoad );
        zone->SetPlayerEntered();
        if( mod->dynaLoadInterior[ 0 ] != '\0' ) zone->SetName( mod->dynaLoadInterior );
        if( Log() ) fprintf( stderr, "mods: stage dyna load %s\"%s\"\n", mod->dynaLoadDump ? "(dump all) " : "", mod->dynaLoad );
        if( mod->dynaLoadDump )
        {
            GetRenderManager()->mpLayer( RenderEnums::LevelSlot )->DumpAllDynaLoads( 1, GetRenderManager()->mEntityDeletionList );
            GetEventManager()->TriggerEvent( EVENT_FIRST_DYNAMIC_ZONE_START, zone );
        }
        else
        {
            GetEventManager()->TriggerEvent( static_cast< EventEnum >( EVENT_LOCATOR + LocatorEvent::DYNAMIC_ZONE ), zone );
        }
    }

    // Applying a stage's settings
    static void StageStart( MissionStage* stage )
    {
        ModStage* mod = stage->GetModStage();
        ApplyPlayerModel( mod );
        if( mod == NULL )
        {
            return;
        }
        mod->paidOut = false;
        HitnRunManager* hnr = GetHitnRunManager();
        bool first = !mod->saved;      // Reset can run more than once for a stage
        if( first )
        {
            mod->saved = true;
            mod->savedDecay = hnr->GetDecayRate();
            mod->savedDecayHitAndRun = hnr->GetDecayRateWhileSpawning();
            mod->savedDecayInterior = hnr->GetDecayRateInside();
            mod->savedFine = hnr->GetBustedCoins();
            mod->savedNumChaseCars = hnr->GetNumChaseCars();
            mod->savedAirGravity = CharacterTune::sfAirGravity;
            mod->savedKickForce = CharacterTune::sfKickingForce;
            mod->savedSlamForce = CharacterTune::sfSlamForce;
        }
        if( mod->resetHitAndRun )
        {
            bool disabled = hnr->IsHitnRunDisabled();     // ResetState enables it again
            hnr->ResetState();
            if( disabled ) hnr->DisableHitnRun();
        }
        if( mod->decay >= 0.0f ) hnr->SetDecayRate( mod->decay );
        if( mod->decayHitAndRun >= 0.0f ) hnr->SetDecayRateWhileSpawning( mod->decayHitAndRun );
        if( mod->decayInterior >= 0.0f ) hnr->SetDecayRateInside( mod->decayInterior );
        if( mod->fine >= 0 ) hnr->SetBustedCoins( mod->fine );
        if( mod->numChaseCars >= 0 ) hnr->SetNumChaseCars( mod->numChaseCars );
        if( mod->carChangeHitAndRun >= 0.0f ) hnr->SetChangeVehicleDelta( mod->carChangeHitAndRun );
        if( mod->hitAndRun >= 0.0f ) hnr->SetHitnRunValue( mod->hitAndRun );
        if( mod->pedGroup >= 0 ) PedestrianManager::GetInstance()->SwitchModelGroup( mod->pedGroup );
        if( mod->trafficGroup >= 0 ) TrafficManager::GetInstance()->SetModTrafficGroup( mod->trafficGroup );
        if( mod->allowCancel >= 0 ) stage->SetMissionAbortEnabled( mod->allowCancel != 0 );
        if( mod->airGravitySet ) CharacterTune::sfAirGravity = mod->airGravity;
        if( mod->kickForceSet ) CharacterTune::sfKickingForce = mod->kickForce;
        if( mod->slamForceSet ) CharacterTune::sfSlamForce = mod->slamForce;
        if( mod->collectibleSoundSet ) s_CollectibleSound = mod->collectibleSound;
        if( mod->musicEvent != 0 )
        {
            radKey32 key = mod->musicEvent;
            GetEventManager()->TriggerEvent( EVENT_CHANGE_MUSIC, &key );
        }
        for( int i = 0; i < mod->numTriggers; i++ )
        {
            Locator* loc = p3d::find< Locator >( mod->triggers[ i ] );
            if( first ) mod->triggerWasActive[ i ] = loc != NULL && loc->GetFlag( Locator::ACTIVE );
            if( loc != NULL ) loc->SetFlag( Locator::ACTIVE, false );
        }
        ApplyPassengers( stage );
        ApplyVehicleSettings( stage );
        if( first && mod->dynaLoad[ 0 ] != '\0' ) StageDynaLoad( mod );
        if( Log() )
        {
            fprintf( stderr, "mods: stage settings: payout %d, hit & run %.0f%s, decays %.1f/%.1f/%.1f, %d trigger(s) off%s\n",
                     mod->payout, mod->hitAndRun, mod->resetHitAndRun ? " (reset)" : "", mod->decay, mod->decayHitAndRun,
                     mod->decayInterior, mod->numTriggers, mod->checkpoint ? ", checkpoint" : "" );
        }

        // reaching a checkpoint: a retry resumes here
        Mission* mission = GetGameplayManager()->GetCurrentMission();
        if( mod->checkpoint && mission != NULL )
        {
            int number = -1;
            for( int i = 0; i < mission->GetNumStages(); i++ )
            {
                MissionStage* s = mission->GetStage( i );
                if( s != NULL && s->GetModStage() != NULL && s->GetModStage()->checkpoint ) number++;
                if( s == stage ) break;
            }
            mission->mModCheckpoint = number;
            if( Log() ) fprintf( stderr, "mods: %s reached checkpoint %d\n", mission->GetName(), number );
        }
    }

    static void StageFinalize( MissionStage* stage )
    {
        ModStage* mod = stage->GetModStage();
        if( mod == NULL )
        {
            return;
        }
        if( mod->saved )
        {
            HitnRunManager* hnr = GetHitnRunManager();
            hnr->SetDecayRate( mod->savedDecay );
            hnr->SetDecayRateWhileSpawning( mod->savedDecayHitAndRun );
            hnr->SetDecayRateInside( mod->savedDecayInterior );
            hnr->SetBustedCoins( mod->savedFine );
            hnr->SetNumChaseCars( mod->savedNumChaseCars );
            CharacterTune::sfAirGravity = mod->savedAirGravity;
            CharacterTune::sfKickingForce = mod->savedKickForce;
            CharacterTune::sfSlamForce = mod->savedSlamForce;
            mod->saved = false;
        }
        if( s_CollectibleSound == mod->collectibleSound )
        {
            s_CollectibleSound = NULL;
        }
        // the next stage disables its own again before anything can trigger
        for( int i = 0; i < mod->numTriggers; i++ )
        {
            Locator* loc = p3d::find< Locator >( mod->triggers[ i ] );
            if( loc != NULL && mod->triggerWasActive[ i ] ) loc->SetFlag( Locator::ACTIVE, true );
        }
    }

    // A retry: resume at the checkpoint reached, with what it (or one before it) set
    static void MissionInitialize( Mission* mission )
    {
        bool retry = s_Retry;
        s_Retry = false;
        s_Loading = false;
        mission->mModRestartTrafficGroup = -1;
        if( !retry )
        {
            mission->mModCheckpoint = -1;
            return;
        }
        if( mission->mModCheckpoint < 0 )
        {
            return;
        }
        int number = -1;
        const char* dynaLoad = NULL;
        const char* interior = NULL;
        const char* carLocator = NULL;
        const char* playerLocator = NULL;
        int pedGroup = -1;
        int trafficGroup = -1;
        for( int i = 0; i < mission->GetNumStages(); i++ )
        {
            ModStage* mod = mission->GetStage( i ) != NULL ? mission->GetStage( i )->GetModStage() : NULL;
            if( mod == NULL || !mod->checkpoint )
            {
                continue;
            }
            number++;
            if( mod->checkpointDynaLoad[ 0 ] != '\0' ) { dynaLoad = mod->checkpointDynaLoad; interior = mod->checkpointInterior; }
            if( mod->checkpointCarLocator[ 0 ] != '\0' ) { carLocator = mod->checkpointCarLocator; playerLocator = mod->checkpointPlayerLocator; }
            if( mod->checkpointPedGroup >= 0 ) pedGroup = mod->checkpointPedGroup;
            if( mod->checkpointTrafficGroup >= 0 ) trafficGroup = mod->checkpointTrafficGroup;
            if( number == mission->mModCheckpoint )
            {
                mission->SetResetToStage( i );
                if( dynaLoad != NULL ) mission->SetRestartDynaload( dynaLoad, interior[ 0 ] != '\0' ? interior : NULL );
                CarStartLocator* car = carLocator != NULL ? p3d::find< CarStartLocator >( carLocator ) : NULL;
                if( car != NULL )
                {
                    mission->SetVehicleRestart( car );
                    // in the car, or out of it at the player's locator
                    mission->SetPlayerRestart( playerLocator[ 0 ] != '\0' ? p3d::find< Locator >( playerLocator ) : NULL );
                }
                if( pedGroup >= 0 ) mission->SetInitPedGroup( pedGroup );
                mission->mModRestartTrafficGroup = trafficGroup;
                fprintf( stderr, "mods: %s resumes at checkpoint %d (stage %d)\n", mission->GetName(), number, i );
                return;
            }
        }
        mission->mModCheckpoint = -1;   // the script no longer has it
    }

    static void MissionReset( Mission* mission )
    {
        int group = mission->mModRestartTrafficGroup >= 0 ? mission->mModRestartTrafficGroup : mission->mModTrafficGroup;
        if( group >= 0 ) TrafficManager::GetInstance()->SetModTrafficGroup( group );
    }

    static void Register( Console* console )
    {
        struct Command { const char* name; Console::CONSOLE_FUNCTION fn; int minArgs, maxArgs; };
        static const Command COMMANDS[] =
        {
            // Objectives
            { "AddObjTargetModel", AddObjTargetModel, 1, 1 },
            { "SetObjTotal", SetObjTotal, 1, 1 },
            { "SetObjSpeedKMH", SetObjSpeedKMH, 1, 1 },
            { "SetObjCameraName", SetObjCameraName, 1, 1 },
            { "SetObjMulticontName", SetObjMulticontName, 1, 1 },
            { "SetObjCanSkip", SetObjCanSkip, 1, 1 },
            { "SetObjNoLetterbox", SetObjNoLetterbox, 1, 1 },
            { "SetObjUseCameraPosition", SetObjUseCameraPosition, 1, 1 },
            { "SetObjDecay", SetObjDecay, 1, 2 },
            { "SetObjExplosion", SetObjExplosion, 2, 3 },
            { "SetObjFailDistance", Unsupported, 1, 1 },
            { "SetObjMessageIndex", SetObjMessageIndex, 1, 1 },
            { "SetObjSound", SetObjSound, 1, 4 },
            { "SetObjThreshold", SetObjThreshold, 1, 1 },
            { "SetObjTrigger", SetObjTrigger, 1, 1 },
            // Conditions
            { "AddCondTargetModel", AddCondTargetModel, 1, 1 },
            { "SetCondTotal", SetCondTotal, 1, 1 },
            { "SetCondDelay", SetCondDelay, 1, 1 },
            { "SetCondSpeedRangeKMH", SetCondSpeedRangeKMH, 2, 2 },
            { "SetCondDisplay", SetCondDisplay, 1, 1 },
            { "SetCondDecay", SetCondDecay, 1, 2 },
            { "SetCondMessageIndex", SetCondMessageIndex, 1, 1 },
            { "SetCondSound", SetCondSound, 1, 4 },
            { "SetCondThreshold", SetCondThreshold, 1, 1 },
            { "SetCondTrigger", SetCondTrigger, 1, 1 },
            // Stages
            { "DisableTrigger", DisableTrigger, 1, 1 },
            { "SetStagePayout", SetStagePayout, 1, 1 },
            { "SetStageHitAndRun", SetStageHitAndRun, 1, 1 },
            { "ResetStageHitAndRun", ResetStageHitAndRun, 0, 0 },
            { "SetStageHitAndRunDecay", SetStageHitAndRunDecay, 1, 1 },
            { "SetStageHitAndRunDecayHitAndRun", SetStageHitAndRunDecayHitAndRun, 1, 1 },
            { "SetStageHitAndRunDecayInterior", SetStageHitAndRunDecayInterior, 1, 1 },
            { "SetStageHitAndRunFine", SetStageHitAndRunFine, 1, 1 },
            { "SetStageNumChaseCars", SetStageNumChaseCars, 1, 1 },
            { "SetStageCarChangeHitAndRunChange", SetStageCarChangeHitAndRunChange, 1, 1 },
            { "SetStagePedGroup", SetStagePedGroup, 1, 1 },
            { "SetStageMusicEvent", SetStageMusicEvent, 1, 1 },
            { "SetStageAllowMissionCancel", SetStageAllowMissionCancel, 1, 1 },
            { "AddStageVehicleCharacter", AddStageVehicleCharacter, 2, 4 },
            { "RemoveStageVehicleCharacter", RemoveStageVehicleCharacter, 2, 2 },
            { "SetNoHitAndRunMusicForStage", Unsupported, 0, 0 },
            { "SetCollectibleSoundEffect", SetCollectibleSoundEffect, 1, 1 },
            { "SetStageCharacterModel", SetStageCharacterModel, 1, 2 },
            { "SetStageAirGravity", SetStageAirGravity, 1, 1 },
            { "SetStageKickForce", SetStageKickForce, 1, 1 },
            { "SetStageSlamForce", SetStageSlamForce, 1, 1 },
            { "SetStageEnabledCheats", Unsupported, 1, 15 },
            { "SetStageTrafficGroup", SetStageTrafficGroup, 1, 1 },
            { "AddStageDynaLoadData", AddStageDynaLoadData, 1, 2 },
            { "SetStageDynaLoadData", SetStageDynaLoadData, 1, 2 },
            { "ResetStageVehicleAbductable", ResetStageVehicleAbductable, 1, 1 },
            { "SetStageVehicleAbductable", SetStageVehicleAbductable, 2, 2 },
            { "SetStageVehicleAllowSeatSlide", SetStageVehicleAllowSeatSlide, 2, 2 },
            { "SetStageVehicleCharacterAnimation", Unsupported, 3, 4 },
            { "SetStageVehicleCharacterJumpOut", Unsupported, 2, 3 },
            { "SetStageVehicleCharacterScale", Unsupported, 3, 3 },
            { "SetStageVehicleCharacterVisible", Unsupported, 2, 2 },
            { "SetStageVehicleNoDestroyedJumpOut", SetStageVehicleNoDestroyedJumpOut, 1, 1 },
            { "SetStageVehicleReset", SetStageVehicleReset, 2, 2 },
            // Checkpoints
            { "CHECKPOINT_HERE", CHECKPOINT_HERE, 0, 0 },
            { "SetCheckpointDynaLoadData", SetCheckpointDynaLoadData, 1, 2 },
            { "SetCheckpointResetPlayerInCar", SetCheckpointResetPlayerInCar, 1, 1 },
            { "SetCheckpointResetPlayerOutCar", SetCheckpointResetPlayerOutCar, 2, 2 },
            { "SetCheckpointPedGroup", SetCheckpointPedGroup, 1, 1 },
            { "SetCheckpointTrafficGroup", SetCheckpointTrafficGroup, 1, 1 },
            // Level and mission initialisation
            { "SetHitAndRunDecayHitAndRun", SetHitAndRunDecayHitAndRun, 1, 1 },
            { "SetHitAndRunFine", SetHitAndRunFine, 1, 1 },
            { "SetCarChangeHitAndRunChange", SetCarChangeHitAndRunChange, 1, 1 },
            { "UseTrafficGroup", UseTrafficGroup, 1, 1 },
            { "AddParkedCar", AddParkedCar, 1, 1 },
            { "SetHUDMapDrawable", Unsupported, 1, 1 },
            { "SetParkedCarsEnabled", SetParkedCarsEnabled, 1, 1 },
            { "SetPedsEnabled", SetPedsEnabled, 1, 1 },
            // CON commands
            { "SetConditionalParameter", Unsupported, 3, 5 },
            { "SetWheelieOffsetX", Unsupported, 1, 1 },
            { "AddVehicleCharacter", Unsupported, 1, 3 },
            { "AddVehicleCharacterSuppressionCharacter", Unsupported, 2, 2 },
            { "SetVehicleCharacterAnimation", Unsupported, 2, 3 },
            { "SetVehicleCharacterJumpOut", Unsupported, 1, 2 },
            { "SetVehicleCharacterScale", Unsupported, 2, 2 },
            { "SetVehicleCharacterVisible", Unsupported, 1, 1 },
        };
        struct Conditional { const char* name; Console::CONSOLE_BOOL_FUNCTION fn; int minArgs, maxArgs; };
        static const Conditional CONDITIONALS[] =
        {
            { "IfCurrentCheckpoint", IfCurrentCheckpoint, 0, 0 },
            { "IfStartedInCar", IfStartedInCar, 0, 0 },
            { "IfCurrentLevel", IfCurrentLevel, 1, 15 },
            { "IfCurrentCar", IfCurrentCar, 1, 15 },
            { "IfCurrentSkin", IfCurrentSkin, 1, 15 },
            { "IfRewardUnlocked", IfRewardUnlocked, 2, 2 },
            { "IfAllMissionsCompleted", IfAllMissionsCompleted, 1, 15 },
            { "IfAnyMissionCompleted", IfAnyMissionCompleted, 1, 15 },
            { "IfAllCheatsEnabled", IfAllCheatsEnabled, 1, 15 },
            { "IfAnyCheatEnabled", IfAnyCheatEnabled, 1, 15 },
        };
        for( unsigned int i = 0; i < sizeof( COMMANDS ) / sizeof( COMMANDS[ 0 ] ); i++ )
        {
            console->AddFunction( COMMANDS[ i ].name, COMMANDS[ i ].fn, "", COMMANDS[ i ].minArgs, COMMANDS[ i ].maxArgs );
        }
        for( unsigned int i = 0; i < sizeof( CONDITIONALS ) / sizeof( CONDITIONALS[ 0 ] ); i++ )
        {
            console->AddFunction( CONDITIONALS[ i ].name, CONDITIONALS[ i ].fn, "", CONDITIONALS[ i ].minArgs,
                                  CONDITIONALS[ i ].maxArgs );
        }
        // the original registered SetHitAndRunDecayInterior as KillAllChaseAI; the hack fixes it (a later
        // registration replaces the earlier one)
        if( shar_ModHackRequired( "AdditionalScriptFunctionality" ) )
        {
            console->AddFunction( "SetHitAndRunDecayInterior", SetHitAndRunDecayInterior, "", 1, 1 );
        }
    }
};

//=============================================================================
// Hooks
//=============================================================================
void ModScript::Register( Console* console )
{
    ModScriptCommands::Register( console );
}

void ModScript::OnStageStart( MissionStage* stage )
{
    ModScriptCommands::StageStart( stage );
}

void ModScript::OnStageComplete( MissionStage* stage )
{
    ModStage* mod = stage->GetModStage();
    if( mod != NULL && mod->payout != 0 && !mod->paidOut )
    {
        mod->paidOut = true;
        if( Log() ) fprintf( stderr, "mods: stage payout %d coins\n", mod->payout );
        GetCoinManager()->AdjustBankValue( mod->payout );
        GetEventManager()->TriggerEvent( EVENT_COLLECTED_COINS );
    }
}

void ModScript::OnStageFinalize( MissionStage* stage )
{
    ModScriptCommands::StageFinalize( stage );
}

void ModScript::OnStageSkipped( MissionStage* stage )
{
    ApplyPassengers( stage );
}

void ModScript::Update( unsigned int elapsedTime )
{
    for( int i = 0; i < s_NumPassengers; i++ )
    {
        UpdatePassenger( i );
    }
}

void ModScript::OnRetry()
{
    s_Retry = true;
    s_Loading = true;
}

void ModScript::OnMissionInitialize( Mission* mission )
{
    ModScriptCommands::MissionInitialize( mission );
}

void ModScript::OnMissionReset( Mission* mission )
{
    ModScriptCommands::MissionReset( mission );
}

void ModScript::RemoveVehicleCharacters()
{
    for( int i = 0; i < s_NumPassengers; i++ )
    {
        Unseat( s_Passengers[ i ] );
    }
    s_NumPassengers = 0;
    RestoreVehicles();
    s_CollectibleSound = NULL;      // (the mission's stages hold the string)
    s_UseCameraPosition = false;
}

void ModScript::OnMissionFinalize()
{
    RemoveVehicleCharacters();
    RestorePlayerModel();       // (not on a Reset: the first stage sets it again)
}

const char* ModScript::CollectibleSound()
{
    return s_CollectibleSound;
}

void ModScript::SetUseCameraPosition( bool use )
{
    if( use != s_UseCameraPosition && Log() )
    {
        fprintf( stderr, "mods: traffic and peds spawn around the %s\n", use ? "camera" : "player" );
    }
    s_UseCameraPosition = use;
}

bool ModScript::SpawnCenter( rmt::Vector& position )
{
    if( !s_UseCameraPosition )
    {
        return false;
    }
    SuperCam* cam = GetSuperCamManager()->GetSCC( 0 )->GetActiveSuperCam();
    if( cam == NULL )
    {
        return false;
    }
    cam->GetPosition( &position );
    return true;
}

#endif // RAD_LINUX
