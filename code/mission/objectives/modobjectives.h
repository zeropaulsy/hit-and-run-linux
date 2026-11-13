// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

//=============================================================================
// Linux port: the objective types Lucas' Mod Launcher's AdditionalScriptFunctionality hack adds for mods' mission
// scripts (docs.donutteam.com, Hacks/AdditionalScriptFunctionality/MissionObjectives). Their script commands are in
// code/mission/modscript.cpp; the matching conditions in conditions/modconditions.h share ModCounter.
//=============================================================================
#ifndef MODOBJECTIVES_H
#define MODOBJECTIVES_H

#ifdef RAD_LINUX

#include <mission/objectives/missionobjective.h>
#include <p3d/entity.hpp>

class Character;
class Vehicle;

// Counts what an ASF objective or condition is about: events (kicked peds, destroyed cars, jumps, ...) or a state
// polled each frame (wasps destroyed, speed, Hit & Run meter)
struct ModCounter
{
    enum Kind
    {
        COLLECT_COINS, COLLECT_WRENCH, COMPLETED_WASPS, DESTROY_CARS, EVENT, HIT_AND_RUN, HIT_AND_RUN_CAUGHT,
        HIT_AND_RUN_LOST, HIT_AND_RUN_RAGE, HIT_PEDS, JUMP, REACH_SPEED, SPRING, NUM_KINDS
    };
    enum { MAX_MODELS = 16 };

    // The kind for an ASF type name ("hitpeds", ...), -1 for others
    static int KindOf( const char* name );

    ModCounter();
    void Start( EventListener* listener );      // count from 0, listen to the kind's events
    void Stop( EventListener* listener );
    void AddModel( const char* name );          // AddObjTargetModel / AddCondTargetModel
    // Counts an event the listener received; true when it was one of this counter's
    bool OnEvent( EventEnum id, void* pEventData );
    int Current() const;                        // the count, or the polled value for the polled kinds
    bool Reached() const { return Current() >= total; }

    Kind kind;
    int eventId;            // EVENT: the game's event (translated from the Mod Launcher's retail numbering)
    int total;
    int count;
    float speedKmh;         // REACH_SPEED
    int numModels;
    tUID models[ MAX_MODELS ];
    char sound[ 48 ];       // HIT_PEDS: SetObjSound / SetCondSound, played for each counted kick; "" = none
};

// Plays a sound resource (daSoundResourceData name) as a one-shot sound effect; nothing when it is not loaded
void ModPlaySound( const char* name );

// insidetrigger / outsidetrigger: a meter that fills while the player is inside (or outside) any trigger volume of
// a locator and decays otherwise; full = the objective is done / the condition fails
struct ModTrigger
{
    enum SoundEvent { SOUND_ENTER, SOUND_INSIDE, SOUND_OUTSIDE, SOUND_EXIT, NUM_SOUNDS };

    ModTrigger();
    void SetLocator( const char* name );
    // SetObjSound / SetCondSound: (sound, event) as the syntax says, or (event, sound) as some examples have it;
    // false for an unknown event or one that does not apply to this type
    bool SetSound( int argc, char** argv );
    void SetDecay( float rate, float delay ) { decayRate = rate; decayDelay = delay; }
    void Start();
    // true once the meter is full
    bool Update( unsigned int elapsedTime );

    bool inside;            // insidetrigger (else outsidetrigger)
    char locator[ 64 ];
    float threshold;        // seconds
    float decayRate;        // meter seconds lost per second away from the counting state
    float decayDelay;       // seconds before the decay starts
    int messageIndex;       // shown when the counting state begins; -1 = none
    char sounds[ NUM_SOUNDS ][ 48 ];
    float soundInterval;    // SOUND_INSIDE / SOUND_OUTSIDE: seconds between plays
    float soundDelay;       // ... and before the first one

    // state
    float meter;            // seconds
    float sinceChange;      // seconds since the last enter/exit
    float sinceRepeat;      // seconds since the last repeated sound
    bool repeatStarted;
    bool wasInside;
    bool first;
};

class ModCountObjective : public MissionObjective
{
public:
    ModCountObjective( ModCounter::Kind kind, int eventId );
    ModCounter& GetCounter() { return mCounter; }
    virtual void HandleEvent( EventEnum id, void* pEventData );
protected:
    virtual void OnInitialize();
    virtual void OnFinalize();
    virtual void OnUpdate( unsigned int elapsedTime );
private:
    void ShowCount();
    void Explode( Character* character );
    enum { MAX_EXPLODED = 32 };
    ModCounter mCounter;
    int mShownCount;
    bool mShowingCount;
public:
    // SetObjExplosion (hitpeds): a breakable effect where a kicked character was, which then leaves the world
    void SetExplosion( int index, float scale, float verticalScale )
    {
        mExplosion = index; mExplosionScale = scale; mExplosionVerticalScale = verticalScale;
    }
private:
    int mExplosion;         // BreakablesEnum::BreakableID; -1 = none
    float mExplosionScale, mExplosionVerticalScale;
    // kicked characters blown up: pedestrians leave the world at the next update (the kick that sent the event is
    // still being handled); other characters stay but count once
    int mNumExploded;
    Character* mExploded[ MAX_EXPLODED ];
    bool mExplodedRemoved[ MAX_EXPLODED ];
};

// "insidetrigger" / "outsidetrigger"
class ModTriggerObjective : public MissionObjective
{
public:
    ModTriggerObjective( bool inside ) { mTrigger.inside = inside; }
    ModTrigger& GetTrigger() { return mTrigger; }
protected:
    virtual void OnInitialize() { mTrigger.Start(); }
    virtual void OnUpdate( unsigned int elapsedTime );
private:
    ModTrigger mTrigger;
};

// "camera": plays an animated camera (multicontroller + camera), done when it ends
class ModCameraObjective : public MissionObjective
{
public:
    ModCameraObjective();
    void SetCameraName( const char* name );
    void SetMulticontrollerName( const char* name );
    void SetCanSkip( bool canSkip ) { mCanSkip = canSkip; }
    void SetNoLetterbox( bool noLetterbox ) { mNoLetterbox = noLetterbox; }
    void SetUseCameraPosition( bool use ) { mUseCameraPosition = use; }
    virtual void HandleEvent( EventEnum id, void* pEventData );
protected:
    virtual void OnInitialize();
    virtual void OnFinalize();
    virtual void OnUpdate( unsigned int elapsedTime );
private:
    char mCamera[ 64 ];
    char mMulticontroller[ 64 ];
    bool mCanSkip;
    bool mNoLetterbox;
    bool mUseCameraPosition;    // traffic and peds spawn around the camera while it plays
    bool mStarted;          // the animated camera became active
    unsigned int mWaitedMs;
};

// The objective for an ASF type name ("camera", "hitpeds", ...), NULL for other names. argv as AddObjective's.
MissionObjective* ModCreateObjective( int argc, char** argv );

// The game's event for a Mod Launcher event number (retail numbering: from 177 one higher than this source's, where
// EVENT_FE_CANCEL became _1 and _2)
int ModTranslateEvent( int retailEvent );

#endif // RAD_LINUX

#endif // MODOBJECTIVES_H
