// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

//=============================================================================
// Linux port: Mod Launcher AdditionalScriptFunctionality objective types (see modobjectives.h)
//=============================================================================
#ifdef RAD_LINUX

#include <raddebug.hpp>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <stdint.h>

#include <mission/objectives/modobjectives.h>
#include <mission/gameplaymanager.h>
#include <mission/charactersheet/charactersheetmanager.h>
#include <events/eventmanager.h>
#include <meta/locatorevents.h>
#include <camera/animatedcam.h>
#include <camera/supercammanager.h>
#include <presentation/gui/guisystem.h>
#include <presentation/gui/ingame/guimanageringame.h>
#include <presentation/gui/ingame/guiscreenhud.h>
#include <worldsim/avatar.h>
#include <worldsim/avatarmanager.h>
#include <worldsim/hitnrunmanager.h>
#include <worldsim/character/character.h>
#include <worldsim/character/charactermanager.h>
#include <worldsim/redbrick/vehicle.h>
#include <worldsim/ped/pedestrianmanager.h>
#include <meta/triggerlocator.h>
#include <meta/triggervolume.h>
#include <p3d/utility.hpp>
#include <render/breakables/breakablesmanager.h>
#include <sound/soundmanager.h>
#include <mission/modscript.h>

namespace
{

struct KindName
{
    const char* name;
    ModCounter::Kind kind;
};

const KindName KIND_NAMES[] =
{
    { "collectcoins", ModCounter::COLLECT_COINS },
    { "collectwrench", ModCounter::COLLECT_WRENCH },
    { "completedwasps", ModCounter::COMPLETED_WASPS },
    { "destroycars", ModCounter::DESTROY_CARS },
    { "event", ModCounter::EVENT },
    { "hitandrun", ModCounter::HIT_AND_RUN },
    { "hitandruncaught", ModCounter::HIT_AND_RUN_CAUGHT },
    { "hitandrunlost", ModCounter::HIT_AND_RUN_LOST },
    { "hitandrunrage", ModCounter::HIT_AND_RUN_RAGE },
    { "hitpeds", ModCounter::HIT_PEDS },
    { "jump", ModCounter::JUMP },
    { "reachspeed", ModCounter::REACH_SPEED },
    { "spring", ModCounter::SPRING },
};

// the events each kind counts (EVENT: its own)
EventEnum CountedEvent( const ModCounter& counter )
{
    switch( counter.kind )
    {
    case ModCounter::COLLECT_COINS: return EVENT_COLLECTED_COINS;
    case ModCounter::COLLECT_WRENCH: return EVENT_COLLECTED_WRENCH;
    case ModCounter::COMPLETED_WASPS: return EVENT_WASP_BLOWED_UP;
    case ModCounter::DESTROY_CARS: return EVENT_VEHICLE_DESTROYED_BY_USER;
    case ModCounter::EVENT: return static_cast< EventEnum >( counter.eventId );
    case ModCounter::HIT_AND_RUN: return EVENT_HIT_AND_RUN_START;
    case ModCounter::HIT_AND_RUN_CAUGHT: return EVENT_HIT_AND_RUN_CAUGHT;
    case ModCounter::HIT_AND_RUN_LOST: return EVENT_HIT_AND_RUN_EVADED;
    case ModCounter::HIT_PEDS: return EVENT_KICK_NPC;
    case ModCounter::JUMP: return EVENT_JUMP_TAKEOFF;
    case ModCounter::SPRING: return static_cast< EventEnum >( EVENT_LOCATOR + LocatorEvent::BOUNCEPAD );
    default: return NUM_EVENTS;     // polled
    }
}

float PlayerSpeedKmh()
{
    Avatar* avatar = GetAvatarManager()->GetAvatarForPlayer( 0 );
    Vehicle* vehicle = avatar != NULL ? avatar->GetVehicle() : NULL;
    return vehicle != NULL ? vehicle->GetSpeedKmh() : 0.0f;
}

}

int ModCounter::KindOf( const char* name )
{
    for( unsigned int i = 0; i < sizeof( KIND_NAMES ) / sizeof( KIND_NAMES[ 0 ] ); i++ )
    {
        if( strcasecmp( name, KIND_NAMES[ i ].name ) == 0 )
        {
            return KIND_NAMES[ i ].kind;
        }
    }
    return -1;
}

ModCounter::ModCounter() :
    kind( COLLECT_COINS ),
    eventId( NUM_EVENTS ),
    total( 1 ),
    count( 0 ),
    speedKmh( 0.0f ),
    numModels( 0 )
{
    sound[ 0 ] = '\0';
}

void ModCounter::Start( EventListener* listener )
{
    count = 0;
    EventEnum id = CountedEvent( *this );
    if( id >= 0 && id < NUM_EVENTS )
    {
        GetEventManager()->AddListener( listener, id );
    }
}

void ModCounter::Stop( EventListener* listener )
{
    EventEnum id = CountedEvent( *this );
    if( id >= 0 && id < NUM_EVENTS )
    {
        GetEventManager()->RemoveListener( listener, id );
    }
}

void ModCounter::AddModel( const char* name )
{
    if( numModels < MAX_MODELS )
    {
        models[ numModels++ ] = tEntity::MakeUID( name );
    }
}

bool ModCounter::OnEvent( EventEnum id, void* pEventData )
{
    if( id != CountedEvent( *this ) )
    {
        return false;
    }
    if( numModels > 0 && ( kind == HIT_PEDS || kind == DESTROY_CARS ) )
    {
        tUID uid = 0;
        if( kind == HIT_PEDS && pEventData != NULL )
        {
            uid = tEntity::MakeUID( GetCharacterManager()->GetModelName( static_cast< Character* >( pEventData ) ) );
        }
        else if( kind == DESTROY_CARS && pEventData != NULL )
        {
            uid = tEntity::MakeUID( static_cast< Vehicle* >( pEventData )->GetName() );
        }
        bool wanted = false;
        for( int i = 0; i < numModels; i++ )
        {
            wanted = wanted || models[ i ] == uid;
        }
        if( !wanted )
        {
            return false;
        }
    }
    count++;
    return true;
}

int ModCounter::Current() const
{
    switch( kind )
    {
    case COMPLETED_WASPS:
        return GetCharacterSheetManager()->QueryNumWaspsDestroyed( GetGameplayManager()->GetCurrentLevelIndex() );
    case REACH_SPEED:
        return PlayerSpeedKmh() >= speedKmh ? total : 0;
    case HIT_AND_RUN_RAGE:
        return GetHitnRunManager()->GetHitnRunValue() >= GetHitnRunManager()->GetWarningThreshold() ? total : 0;
    default:
        return count;
    }
}

void ModPlaySound( const char* name )
{
    if( name == NULL || name[ 0 ] == '\0' )
    {
        return;
    }
    if( getenv( "SHAR_MOD_LOG" ) != NULL )
    {
        fprintf( stderr, "mods: playing sound %s\n", name );
    }
    SoundManager::GetInstance()->PlayModSound( name );
}

//=============================================================================
// ModTrigger
//=============================================================================
ModTrigger::ModTrigger() :
    inside( true ),
    threshold( 0.0f ),
    decayRate( 0.0f ),
    decayDelay( 0.0f ),
    messageIndex( -1 ),
    soundInterval( 0.0f ),
    soundDelay( 0.0f )
{
    locator[ 0 ] = '\0';
    for( int i = 0; i < NUM_SOUNDS; i++ )
    {
        sounds[ i ][ 0 ] = '\0';
    }
    Start();
}

void ModTrigger::SetLocator( const char* name )
{
    snprintf( locator, sizeof( locator ), "%s", name );
}

bool ModTrigger::SetSound( int argc, char** argv )
{
    static const char* EVENTS[ NUM_SOUNDS ] = { "enter_trigger", "inside_trigger", "outside_trigger", "exit_trigger" };
    if( argc < 3 )
    {
        return false;
    }
    int event = -1;
    const char* sound = NULL;
    for( int i = 0; i < NUM_SOUNDS && event < 0; i++ )
    {
        if( strcasecmp( argv[ 2 ], EVENTS[ i ] ) == 0 ) { event = i; sound = argv[ 1 ]; }
        else if( strcasecmp( argv[ 1 ], EVENTS[ i ] ) == 0 ) { event = i; sound = argv[ 2 ]; }
    }
    // a repeated sound only for the state that counts
    if( event < 0 || ( event == SOUND_INSIDE && !inside ) || ( event == SOUND_OUTSIDE && inside ) )
    {
        return false;
    }
    snprintf( sounds[ event ], sizeof( sounds[ event ] ), "%s", sound );
    if( event == SOUND_INSIDE || event == SOUND_OUTSIDE )
    {
        soundInterval = argc > 3 ? static_cast< float >( atof( argv[ 3 ] ) ) : 0.0f;
        soundDelay = argc > 4 ? static_cast< float >( atof( argv[ 4 ] ) ) : 0.0f;
    }
    return true;
}

void ModTrigger::Start()
{
    meter = 0.0f;
    sinceChange = 0.0f;
    sinceRepeat = 0.0f;
    repeatStarted = false;
    wasInside = false;
    first = true;
}

bool ModTrigger::Update( unsigned int elapsedTime )
{
    float dt = elapsedTime / 1000.0f;
    bool in = false;
    Avatar* avatar = GetAvatarManager()->GetAvatarForPlayer( 0 );
    // looked up each time: a locator from a zone can be unloaded
    TriggerLocator* loc = locator[ 0 ] != '\0' ? dynamic_cast< TriggerLocator* >( p3d::find< Locator >( locator ) ) : NULL;
    if( avatar != NULL && loc != NULL )
    {
        rmt::Vector position;
        avatar->GetPosition( position );
        for( unsigned int i = 0; i < loc->GetNumTriggers() && !in; i++ )
        {
            TriggerVolume* volume = loc->GetTriggerVolume( i );
            in = volume != NULL && volume->Contains( position );
        }
    }
    bool counting = in == inside;
    if( first || in != wasInside )
    {
        // (starting outside is no exit)
        if( !first || in )
        {
            ModPlaySound( sounds[ in ? SOUND_ENTER : SOUND_EXIT ] );
        }
        if( getenv( "SHAR_MOD_LOG" ) != NULL )
        {
            fprintf( stderr, "mods: %s %s: player %s\n", inside ? "insidetrigger" : "outsidetrigger", locator,
                     loc == NULL ? "outside (locator not loaded)" : in ? "inside" : "outside" );
        }
        if( counting && messageIndex >= 0 )
        {
            GetEventManager()->TriggerEvent( EVENT_SHOW_MISSION_OBJECTIVE,
                                             reinterpret_cast< void* >( static_cast< intptr_t >( messageIndex ) ) );
        }
        wasInside = in;
        first = false;
        sinceChange = 0.0f;
        sinceRepeat = 0.0f;
        repeatStarted = false;
    }
    else
    {
        sinceChange += dt;
    }

    if( counting )
    {
        meter += dt;
        const char* repeat = sounds[ inside ? SOUND_INSIDE : SOUND_OUTSIDE ];
        if( repeat[ 0 ] != '\0' )
        {
            if( !repeatStarted )
            {
                if( sinceChange >= soundDelay )
                {
                    ModPlaySound( repeat );
                    repeatStarted = true;
                    sinceRepeat = 0.0f;
                }
            }
            else
            {
                sinceRepeat += dt;
                if( soundInterval > 0.0f && sinceRepeat >= soundInterval )
                {
                    ModPlaySound( repeat );
                    sinceRepeat -= soundInterval;
                }
            }
        }
        return meter >= threshold;
    }
    if( sinceChange >= decayDelay )
    {
        meter -= decayRate * dt;
        if( meter < 0.0f )
        {
            meter = 0.0f;
        }
    }
    return false;
}

void ModTriggerObjective::OnUpdate( unsigned int elapsedTime )
{
    if( !GetFinished() && mTrigger.Update( elapsedTime ) )
    {
        if( getenv( "SHAR_MOD_LOG" ) != NULL )
        {
            fprintf( stderr, "mods: %s %s: objective done\n", mTrigger.inside ? "insidetrigger" : "outsidetrigger",
                     mTrigger.locator );
        }
        SetFinished( true );
    }
}

//=============================================================================
// ModCountObjective
//=============================================================================
ModCountObjective::ModCountObjective( ModCounter::Kind kind, int eventId ) :
    mShownCount( -1 ),
    mShowingCount( false ),
    mExplosion( -1 ),
    mExplosionScale( 1.0f ),
    mExplosionVerticalScale( 1.0f ),
    mNumExploded( 0 )
{
    mCounter.kind = kind;
    mCounter.eventId = eventId;
}

void ModCountObjective::OnInitialize()
{
    mCounter.Start( this );
    mShownCount = -1;
    mNumExploded = 0;
    // a count to show: as the game's delivery objectives do, only for more than one
    mShowingCount = mCounter.total > 1 && mCounter.kind != ModCounter::REACH_SPEED &&
                    mCounter.kind != ModCounter::HIT_AND_RUN_RAGE;
    if( mShowingCount )
    {
        GetGuiSystem()->HandleMessage( GUI_MSG_SHOW_HUD_OVERLAY, HUD_COLLECTIBLES );
    }
    ShowCount();
}

void ModCountObjective::OnFinalize()
{
    mCounter.Stop( this );
    mNumExploded = 0;
    if( mShowingCount )
    {
        GetGuiSystem()->HandleMessage( GUI_MSG_HIDE_HUD_OVERLAY, HUD_COLLECTIBLES );
        mShowingCount = false;
    }
}

void ModCountObjective::ShowCount()
{
    int current = mCounter.Current();
    if( current > mCounter.total )
    {
        current = mCounter.total;
    }
    if( mShowingCount && current != mShownCount && GetCurrentHud() != NULL )
    {
        GetCurrentHud()->SetCollectibles( current, mCounter.total );
        mShownCount = current;
    }
}

void ModCountObjective::OnUpdate( unsigned int elapsedTime )
{
    for( int i = 0; i < mNumExploded; i++ )
    {
        if( !mExplodedRemoved[ i ] && PedestrianManager::GetInstance()->IsPed( mExploded[ i ] ) )
        {
            PedestrianManager::GetInstance()->RemovePed( mExploded[ i ] );
            // the character goes back to the pedestrian pool and may come back as another ped
            mExploded[ i ] = mExploded[ mNumExploded - 1 ];
            mExplodedRemoved[ i ] = mExplodedRemoved[ mNumExploded - 1 ];
            mNumExploded--;
            i--;
            continue;
        }
        mExplodedRemoved[ i ] = true;
    }
    if( mCounter.kind == ModCounter::HIT_AND_RUN && GetHitnRunManager()->IsHitnRunActive() )
    {
        mCounter.count = mCounter.total;    // already in one (e.g. SetStageHitAndRun(100))
    }
    ShowCount();
    if( !GetFinished() && mCounter.Reached() )
    {
        SetFinished( true );
    }
}

void ModCountObjective::HandleEvent( EventEnum id, void* pEventData )
{
    if( mCounter.kind == ModCounter::HIT_PEDS && id == EVENT_KICK_NPC && pEventData != NULL && mExplosion >= 0 )
    {
        for( int i = 0; i < mNumExploded; i++ )
        {
            if( mExploded[ i ] == pEventData )
            {
                return;     // already blown up
            }
        }
    }
    if( mCounter.OnEvent( id, pEventData ) && mCounter.kind == ModCounter::HIT_PEDS )
    {
        ModPlaySound( mCounter.sound );
        if( mExplosion >= 0 && pEventData != NULL )
        {
            Explode( static_cast< Character* >( pEventData ) );
        }
    }
}

void ModCountObjective::Explode( Character* character )
{
    rmt::Vector position;
    character->GetPosition( position );
    rmt::Matrix transform;
    transform.Identity();
    transform.FillScale( mExplosionScale, mExplosionVerticalScale, mExplosionScale );
    transform.FillTranslate( position );
    if( mExplosion < BreakablesEnum::eNumBreakables )
    {
        GetBreakablesManager()->Play( static_cast< BreakablesEnum::BreakableID >( mExplosion ), transform );
    }
    if( getenv( "SHAR_MOD_LOG" ) != NULL )
    {
        fprintf( stderr, "mods: hitpeds: explosion %d (scale %.2f/%.2f) at %.1f,%.1f,%.1f\n", mExplosion,
                 mExplosionScale, mExplosionVerticalScale, position.x, position.y, position.z );
    }
    if( mNumExploded < MAX_EXPLODED )
    {
        mExplodedRemoved[ mNumExploded ] = false;
        mExploded[ mNumExploded++ ] = character;
    }
}

//=============================================================================
// ModCameraObjective
//=============================================================================
ModCameraObjective::ModCameraObjective() :
    mCanSkip( false ),
    mNoLetterbox( false ),
    mUseCameraPosition( false ),
    mStarted( false ),
    mWaitedMs( 0 )
{
    mCamera[ 0 ] = '\0';
    mMulticontroller[ 0 ] = '\0';
}

void ModCameraObjective::SetCameraName( const char* name )
{
    snprintf( mCamera, sizeof( mCamera ), "%s", name );
}

void ModCameraObjective::SetMulticontrollerName( const char* name )
{
    snprintf( mMulticontroller, sizeof( mMulticontroller ), "%s", name );
}

void ModCameraObjective::OnInitialize()
{
    mStarted = false;
    mWaitedMs = 0;
    GetEventManager()->AddListener( this, EVENT_ANIMATED_CAM_SHUTDOWN );
    AnimatedCam::AllowSkipping( mCanSkip );
    if( mNoLetterbox )
    {
        AnimatedCam::SupressNextLetterbox();
    }
    AnimatedCam::SetMulticontroller( mMulticontroller );
    AnimatedCam::SetCamera( mCamera );
    AnimatedCam::CheckPendingCameraSwitch();    // (the game only checks when gameplay starts)
    ModScript::SetUseCameraPosition( mUseCameraPosition );
}

void ModCameraObjective::OnFinalize()
{
    GetEventManager()->RemoveListener( this, EVENT_ANIMATED_CAM_SHUTDOWN );
    ModScript::SetUseCameraPosition( false );
}

void ModCameraObjective::OnUpdate( unsigned int elapsedTime )
{
    if( GetFinished() || mStarted )
    {
        return;
    }
    SuperCam* cam = GetSuperCamManager()->GetSCC( 0 )->GetActiveSuperCam();
    if( cam != NULL && cam->GetType() == SuperCam::ANIMATED_CAM )
    {
        mStarted = true;
        if( getenv( "SHAR_MOD_LOG" ) != NULL )
        {
            fprintf( stderr, "mods: camera objective: %s / %s playing\n", mMulticontroller, mCamera );
        }
        return;
    }
    // the camera never started (missing from the inventory, or the player was inside an interior): move on
    mWaitedMs += elapsedTime;
    if( mWaitedMs > 3000 )
    {
        fprintf( stderr, "mods: camera objective: %s / %s did not start\n", mMulticontroller, mCamera );
        SetFinished( true );
    }
}

void ModCameraObjective::HandleEvent( EventEnum id, void* pEventData )
{
    if( id == EVENT_ANIMATED_CAM_SHUTDOWN )
    {
        if( getenv( "SHAR_MOD_LOG" ) != NULL )
        {
            fprintf( stderr, "mods: camera objective: %s / %s done%s\n", mMulticontroller, mCamera,
                     mStarted ? "" : " (before it played)" );
        }
        SetFinished( true );
    }
}

//=============================================================================
MissionObjective* ModCreateObjective( int argc, char** argv )
{
    if( strcasecmp( argv[ 1 ], "camera" ) == 0 )
    {
        return new ModCameraObjective();
    }
    if( strcasecmp( argv[ 1 ], "insidetrigger" ) == 0 || strcasecmp( argv[ 1 ], "outsidetrigger" ) == 0 )
    {
        return new ModTriggerObjective( tolower( argv[ 1 ][ 0 ] ) == 'i' );
    }
    int kind = ModCounter::KindOf( argv[ 1 ] );
    if( kind < 0 )
    {
        return NULL;
    }
    int eventId = NUM_EVENTS;
    if( kind == ModCounter::EVENT && argc > 2 )
    {
        eventId = ModTranslateEvent( atoi( argv[ 2 ] ) );
    }
    return new ModCountObjective( static_cast< ModCounter::Kind >( kind ), eventId );
}

int ModTranslateEvent( int retailEvent )
{
    const int FE_CANCEL = static_cast< int >( EVENT_FE_CANCEL );   // retail 176 = EVENT_FE_CANCEL_1, 177 = _2
    return retailEvent <= FE_CANCEL ? retailEvent : retailEvent == FE_CANCEL + 1 ? FE_CANCEL : retailEvent - 1;
}

#endif // RAD_LINUX
