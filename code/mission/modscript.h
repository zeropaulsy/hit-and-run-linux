//=============================================================================
// Linux port: Lucas' Mod Launcher's AdditionalScriptFunctionality hack (docs.donutteam.com,
// Hacks/AdditionalScriptFunctionality): the script commands it adds for mods' MFK/CON scripts, the per-stage
// settings they make (ModStage, kept by MissionStage), checkpoints and passengers in cars. The objective and
// condition types are in objectives/modobjectives.h and conditions/modconditions.h.
//
// The commands are always registered (the original's scripts never use their names); the original's
// SetHitAndRunDecayInterior, wired to KillAllChaseAI, is only corrected for mods that require the hack.
//=============================================================================
#ifndef MODSCRIPT_H
#define MODSCRIPT_H

#ifdef RAD_LINUX

#include <radmath/radmath.hpp>

class Console;
class Mission;
class MissionStage;

struct ModStageVehicleCharacter
{
    char car[ 32 ];             // "current": the player's car when the stage starts
    char character[ 32 ];
    char joint[ 16 ];
    float rotation;             // degrees; 180 faces forwards
    bool remove;                // RemoveStageVehicleCharacter
};

// SetStageVehicleCharacterAnimation/JumpOut/Scale/Visible: for a passenger added with AddStageVehicleCharacter
struct ModStagePassengerSetting
{
    enum Kind { ANIMATION, JUMP_OUT, SCALE, VISIBLE };
    char car[ 32 ];             // as the passenger was added ("current" too)
    char character[ 32 ];
    Kind kind;
    char animation[ 32 ];
    bool bobbing;
    bool valueSet;              // JUMP_OUT: a direction given
    float value;                // the scale, or the direction (degrees)
};

// SetStageVehicleAbductable, ResetStageVehicleAbductable, SetStageVehicleAllowSeatSlide,
// SetStageVehicleNoDestroyedJumpOut: applied on reaching the stage
struct ModStageVehicleSetting
{
    enum Kind { ABDUCTABLE, RESET_ABDUCTABLE, SEAT_SLIDE, NO_DESTROYED_JUMP_OUT };
    char car[ 32 ];             // "current": the player's car when the stage starts
    Kind kind;
    bool value;
};

struct ModStage
{
    enum { MAX_TRIGGERS = 8, MAX_VEHICLE_CHARACTERS = 8, MAX_VEHICLE_SETTINGS = 8, MAX_PASSENGER_SETTINGS = 16 };

    ModStage();

    int payout;                 // coins for completing the stage; 0 = none
    bool paidOut;               // (the completion check runs every frame until the next stage)
    float hitAndRun;            // the meter at the start; < 0 = unchanged
    bool resetHitAndRun;
    // Hit & Run settings for the stage, restored after it; < 0 = unchanged
    float decay, decayHitAndRun, decayInterior, carChangeHitAndRun;
    int fine, numChaseCars;
    int pedGroup;               // SetStagePedGroup; -1 = unchanged
    unsigned int musicEvent;    // SetStageMusicEvent (radKey32); 0 = none
    int allowCancel;            // SetStageAllowMissionCancel; -1 = unchanged
    bool collectibleSoundSet;   // SetCollectibleSoundEffect
    char collectibleSound[ 48 ];    // "" = none
    int trafficGroup;           // SetStageTrafficGroup; -1 = unchanged (kept after the stage)
    // Add/SetStageDynaLoadData: zones loaded on reaching the stage ("" = none), Set dumping all loaded ones first
    char dynaLoad[ 512 ];
    char dynaLoadInterior[ 32 ];
    bool dynaLoadDump;
    // SetStageCharacterModel: the player's model and animation set for the stage ("" = unchanged / the player's)
    char characterModel[ 32 ];
    char characterAnimSet[ 32 ];
    // SetStageAirGravity / KickForce / SlamForce (CharacterTune), restored after the stage
    bool airGravitySet, kickForceSet, slamForceSet;
    float airGravity, kickForce, slamForce;

    int numTriggers;            // DisableTrigger
    char triggers[ MAX_TRIGGERS ][ 32 ];
    int numVehicleCharacters;
    ModStageVehicleCharacter vehicleCharacters[ MAX_VEHICLE_CHARACTERS ];
    int numVehicleSettings;
    ModStageVehicleSetting vehicleSettings[ MAX_VEHICLE_SETTINGS ];
    int numPassengerSettings;
    ModStagePassengerSetting passengerSettings[ MAX_PASSENGER_SETTINGS ];

    // CHECKPOINT_HERE and what resuming from it uses (unset values: those of the checkpoint before)
    bool checkpoint;
    char checkpointDynaLoad[ 512 ];
    char checkpointInterior[ 32 ];
    char checkpointCarLocator[ 32 ];
    char checkpointPlayerLocator[ 32 ];
    int checkpointPedGroup;
    int checkpointTrafficGroup;

    // the Hit & Run manager's settings before the stage changed them
    bool saved;
    float savedDecay, savedDecayHitAndRun, savedDecayInterior;
    int savedFine, savedNumChaseCars;
    float savedAirGravity, savedKickForce, savedSlamForce;
    bool triggerWasActive[ MAX_TRIGGERS ];
};

namespace ModScript
{
    void Register( Console* console );

    // MissionStage: entering the stage (end of Reset), completing it, leaving it (Finalize), each frame
    void OnStageStart( MissionStage* stage );
    void OnStageComplete( MissionStage* stage );
    void OnStageFinalize( MissionStage* stage );
    void Update( unsigned int elapsedTime );
    // Mission::SetToStage replaying the stages before the one a retry resumes at (their passengers)
    void OnStageSkipped( MissionStage* stage );

    // MissionManager::RestartCurrentMission: the next Mission::Initialize is a retry (resumes at the checkpoint)
    void OnRetry();
    // Mission::Initialize (the mission's script has just run): apply or forget its checkpoint
    void OnMissionInitialize( Mission* mission );
    // Mission::InitDynaLoad (selecting or restarting it, not a sunday drive's mission following it): its traffic group
    void OnMissionReset( Mission* mission );
    // Mission::Reset: passengers leave, cars get back the settings the stages changed
    void RemoveVehicleCharacters();
    // Mission::Finalize: that, and the player's model back (SetStageCharacterModel)
    void OnMissionFinalize();

    // SetCollectibleSoundEffect: the sound for collecting a delivery/dump/race item, "" for none, NULL for the game's
    const char* CollectibleSound();

    // SetObjUseCameraPosition: a camera objective sets it while it runs; then the traffic and pedestrian managers
    // spawn around the camera (SpawnCenter replaces the player's position and returns true)
    void SetUseCameraPosition( bool use );
    bool SpawnCenter( rmt::Vector& position );
}

#endif // RAD_LINUX

#endif // MODSCRIPT_H
