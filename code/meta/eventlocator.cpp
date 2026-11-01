//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        
//
// Description: Implement EventLocator
//
// History:     04/04/2002 + Created -- Cary Brisebois
//
//=============================================================================

//========================================
// System Includes
//========================================
// Foundation Tech
#include <raddebug.hpp>

//========================================
// Project Includes
//========================================
#include <meta/eventlocator.h>
#include <meta/locatorevents.h>
#include <events/eventmanager.h>
#ifdef RAD_LINUX
#include <worldsim/avatar.h>
#include <worldsim/avatarmanager.h>
#include <shar_mods.h>
#endif

#include <debug/profiler.h>

//******************************************************************************
//
// Global Data, Local Data, Local Classes
//
//******************************************************************************

//******************************************************************************
//
// Public Member Functions
//
//******************************************************************************

//==============================================================================
// EventLocator::EventLocator
//==============================================================================
// Description: Constructor.
//
// Parameters:	None.
//
// Return:      N/A.
//
//==============================================================================
EventLocator::EventLocator()
{
    mMatrix.Identity();
}

//==============================================================================
// EventLocator::~EventLocator
//==============================================================================
// Description: Destructor.
//
// Parameters:	None.
//
// Return:      N/A.
//
//==============================================================================
EventLocator::~EventLocator()
{
}

//******************************************************************************
//
// Private Member Functions
//
//******************************************************************************

//=============================================================================
// EventLocator::OnTrigger
//=============================================================================
// Description: Comment
//
// Parameters:  ( unsigned int playerID )
//
// Return:      void 
//
//=============================================================================
void EventLocator::OnTrigger( unsigned int playerID )
{
#ifdef RAD_LINUX
    // Linux port: mods' BugFixes NoAirVentAudio: a car driving into an air vent (bounce pad) gets no vent sound or
    // dialogue (the bounce itself only happens on foot)
    static const bool s_noAirVentAudio = shar_ModBugFix( "Vehicles", "NoAirVentAudio", 1 ) != 0;
    if( s_noAirVentAudio && mEventType == LocatorEvent::BOUNCEPAD )
    {
        Avatar* avatar = GetAvatarManager()->GetAvatarForPlayer( playerID );
        if( avatar != NULL && avatar->IsInCar() )
        {
            return;
        }
    }
#endif
    //This will fire an event off that uses the data field
    GetEventManager()->TriggerEvent( (EventEnum)( EVENT_LOCATOR + mEventType ), (void*)this );
}
