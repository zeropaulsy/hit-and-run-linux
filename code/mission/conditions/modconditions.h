// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

//=============================================================================
// Linux port: the condition types Lucas' Mod Launcher's AdditionalScriptFunctionality hack adds for mods' mission
// scripts (docs.donutteam.com, Hacks/AdditionalScriptFunctionality/MissionConditions). Script commands:
// code/mission/modscript.cpp.
//=============================================================================
#ifndef MODCONDITIONS_H
#define MODCONDITIONS_H

#ifdef RAD_LINUX

#include <mission/conditions/missioncondition.h>
#include <mission/objectives/modobjectives.h>

// Fails the stage once its counter reaches the total (kicked peds, destroyed cars, a Hit & Run, ...), after a delay
// (SetCondDelay) that lets e.g. the Hit & Run ticket go before the mission-failed screen
class ModCountCondition : public MissionCondition
{
public:
    ModCountCondition( ModCounter::Kind kind, int eventId );
    ModCounter& GetCounter() { return mCounter; }
    void SetDelay( float seconds ) { mDelayMs = static_cast< int >( seconds * 1000.0f ); }
    virtual void Update( unsigned int elapsedTime );
    virtual void HandleEvent( EventEnum id, void* pEventData );
protected:
    virtual void OnInitialize();
    virtual void OnFinalize();
private:
    ModCounter mCounter;
    int mDelayMs;
    int mFailingMs;         // -1: not failing yet
};

// "insidetrigger" / "outsidetrigger": fails once the player has been inside (outside) the locator's triggers for
// the threshold (SetCondThreshold, default 0: at once)
class ModTriggerCondition : public MissionCondition
{
public:
    ModTriggerCondition( bool inside ) { mTrigger.inside = inside; }
    ModTrigger& GetTrigger() { return mTrigger; }
    virtual void Update( unsigned int elapsedTime );
protected:
    virtual void OnInitialize() { mTrigger.Start(); }
private:
    ModTrigger mTrigger;
};

// "maintainspeed": fails when the player's speed leaves a range; shown on the proximity meter
class ModMaintainSpeedCondition : public MissionCondition
{
public:
    enum Display { DISPLAY_FILL, DISPLAY_MIDPOINT, DISPLAY_NONE };
    ModMaintainSpeedCondition();
    void SetRange( float lowKmh, float highKmh ) { mLowKmh = lowKmh; mHighKmh = highKmh; }
    void SetDisplay( Display display ) { mDisplay = display; }
    virtual void Update( unsigned int elapsedTime );
protected:
    virtual void OnInitialize();
    virtual void OnFinalize();
private:
    float mLowKmh;
    float mHighKmh;
    Display mDisplay;
};

// The condition for an ASF type name (and the game's own "hitandruncought", which it never implemented), NULL
// for other names. argv as AddCondition's.
MissionCondition* ModCreateCondition( int argc, char** argv );

// SetCondDisplay("none") etc. on any of them; false when the condition is not one of these
bool ModSetConditionDisplay( MissionCondition* condition, const char* display );

#endif // RAD_LINUX

#endif // MODCONDITIONS_H
