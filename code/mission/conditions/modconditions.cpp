// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

//=============================================================================
// Linux port: Mod Launcher AdditionalScriptFunctionality condition types (see modconditions.h)
//=============================================================================
#ifdef RAD_LINUX

#include <raddebug.hpp>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <mission/conditions/modconditions.h>
#include <events/eventmanager.h>
#include <presentation/gui/guisystem.h>
#include <presentation/gui/ingame/guimanageringame.h>
#include <presentation/gui/ingame/guiscreenhud.h>
#include <worldsim/avatar.h>
#include <worldsim/avatarmanager.h>
#include <worldsim/redbrick/vehicle.h>

//=============================================================================
// ModCountCondition
//=============================================================================
ModCountCondition::ModCountCondition( ModCounter::Kind kind, int eventId ) :
    mFailingMs( -1 )
{
    mCounter.kind = kind;
    mCounter.eventId = eventId;
    // the Mod Launcher's default delays (SetCondDelay's docs)
    switch( kind )
    {
    case ModCounter::HIT_AND_RUN:
    case ModCounter::HIT_AND_RUN_CAUGHT:
        mDelayMs = 2000;
        SetType( kind == ModCounter::HIT_AND_RUN_CAUGHT ? COND_HIT_AND_RUN_CAUGHT : COND_INVALID );
        break;
    case ModCounter::EVENT:
    case ModCounter::HIT_AND_RUN_LOST:
        mDelayMs = 0;
        break;
    default:
        mDelayMs = 1000;
        break;
    }
}

void ModCountCondition::OnInitialize()
{
    mFailingMs = -1;
    mCounter.Start( this );
}

void ModCountCondition::OnFinalize()
{
    mCounter.Stop( this );
}

void ModCountCondition::Update( unsigned int elapsedTime )
{
    if( IsViolated() )
    {
        return;
    }
    if( mFailingMs < 0 && mCounter.Reached() )
    {
        mFailingMs = 0;
    }
    if( mFailingMs >= 0 )
    {
        if( mFailingMs >= mDelayMs )
        {
            SetIsViolated( true );
        }
        mFailingMs += elapsedTime;
    }
}

void ModCountCondition::HandleEvent( EventEnum id, void* pEventData )
{
    if( mCounter.OnEvent( id, pEventData ) && mCounter.kind == ModCounter::HIT_PEDS )
    {
        ModPlaySound( mCounter.sound );
    }
}

//=============================================================================
// ModTriggerCondition
//=============================================================================
void ModTriggerCondition::Update( unsigned int elapsedTime )
{
    if( !IsViolated() && mTrigger.Update( elapsedTime ) )
    {
        if( getenv( "SHAR_MOD_LOG" ) != NULL )
        {
            fprintf( stderr, "mods: %s %s: condition failed\n", mTrigger.inside ? "insidetrigger" : "outsidetrigger",
                     mTrigger.locator );
        }
        SetIsViolated( true );
    }
}

//=============================================================================
// ModMaintainSpeedCondition
//=============================================================================
ModMaintainSpeedCondition::ModMaintainSpeedCondition() :
    mLowKmh( 0.0f ),
    mHighKmh( 1000.0f ),
    mDisplay( DISPLAY_FILL )
{
}

void ModMaintainSpeedCondition::OnInitialize()
{
    if( mDisplay != DISPLAY_NONE )
    {
        GetGuiSystem()->HandleMessage( GUI_MSG_SHOW_HUD_OVERLAY, HUD_PROXIMITY_METER, 1 /* green bar */ );
    }
}

void ModMaintainSpeedCondition::OnFinalize()
{
    if( mDisplay != DISPLAY_NONE )
    {
        GetGuiSystem()->HandleMessage( GUI_MSG_HIDE_HUD_OVERLAY, HUD_PROXIMITY_METER );
    }
}

void ModMaintainSpeedCondition::Update( unsigned int elapsedTime )
{
    if( IsViolated() )
    {
        return;
    }
    Avatar* avatar = GetAvatarManager()->GetAvatarForPlayer( 0 );
    Vehicle* vehicle = avatar != NULL ? avatar->GetVehicle() : NULL;
    float speed = vehicle != NULL ? vehicle->GetSpeedKmh() : 0.0f;
    if( speed < mLowKmh || speed > mHighKmh )
    {
        SetIsViolated( true );
    }
    if( mDisplay != DISPLAY_NONE && GetCurrentHud() != NULL && mHighKmh > mLowKmh )
    {
        float t = ( speed - mLowKmh ) / ( mHighKmh - mLowKmh );
        // fill: full in the middle of the range, empty at either limit; midpoint: empty at the lower limit, full at
        // the upper one
        float value = mDisplay == DISPLAY_FILL ? 1.0f - rmt::Fabs( t * 2.0f - 1.0f ) : t;
        GetCurrentHud()->SetProximityMeter( rmt::Clamp( value, 0.0f, 1.0f ) );
    }
}

//=============================================================================
MissionCondition* ModCreateCondition( int argc, char** argv )
{
    if( strcasecmp( argv[ 1 ], "maintainspeed" ) == 0 )
    {
        return new ModMaintainSpeedCondition();
    }
    if( strcasecmp( argv[ 1 ], "insidetrigger" ) == 0 || strcasecmp( argv[ 1 ], "outsidetrigger" ) == 0 )
    {
        return new ModTriggerCondition( tolower( argv[ 1 ][ 0 ] ) == 'i' );
    }
    int kind = strcmp( argv[ 1 ], "hitandruncought" ) == 0 ? ModCounter::HIT_AND_RUN_CAUGHT
                                                            : ModCounter::KindOf( argv[ 1 ] );
    if( kind < 0 || kind == ModCounter::COMPLETED_WASPS || kind == ModCounter::REACH_SPEED )
    {
        return NULL;
    }
    int eventId = NUM_EVENTS;
    if( kind == ModCounter::EVENT && argc > 2 )
    {
        eventId = ModTranslateEvent( atoi( argv[ 2 ] ) );
    }
    return new ModCountCondition( static_cast< ModCounter::Kind >( kind ), eventId );
}

bool ModSetConditionDisplay( MissionCondition* condition, const char* display )
{
    ModMaintainSpeedCondition* speed = dynamic_cast< ModMaintainSpeedCondition* >( condition );
    if( speed != NULL )
    {
        speed->SetDisplay( strcasecmp( display, "none" ) == 0 ? ModMaintainSpeedCondition::DISPLAY_NONE
                         : strcasecmp( display, "midpoint" ) == 0 ? ModMaintainSpeedCondition::DISPLAY_MIDPOINT
                         : ModMaintainSpeedCondition::DISPLAY_FILL );
        return true;
    }
    return dynamic_cast< ModCountCondition* >( condition ) != NULL ||    // they show nothing anyway
           dynamic_cast< ModTriggerCondition* >( condition ) != NULL;
}

#endif // RAD_LINUX
