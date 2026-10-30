//=============================================================================
// Linux port: Mod Launcher AdditionalScriptFunctionality passengers: characters a mod seats on a joint of a car,
// by mission script (AddStageVehicleCharacter) or by the car's CON file (AddVehicleCharacter). Kept here so both
// Character and Vehicle can hold them; the seating is code/mission/modscript.cpp.
//=============================================================================
#ifndef MODSEAT_H
#define MODSEAT_H

#ifdef RAD_LINUX

#include <p3d/p3dtypes.hpp>

// SetStageVehicleCharacterAnimation/Scale/Visible/JumpOut and the CON SetVehicleCharacter* commands
struct ModSeatSettings
{
    ModSeatSettings() : animation( 0 ), bobbing( true ), scale( 0.0f ), visible( false ), jumpOut( false ),
                        jumpDirectionSet( false ), jumpDirection( 0.0f ) {}
    tUID animation;         // 0 = in_car_idle (the game's)
    bool bobbing;           // bounces and sways with the car (a passenger's default)
    float scale;            // 0 = the car's character scale
    bool visible;           // even in a car whose characters are hidden
    bool jumpOut;           // when the car is destroyed (else gone with it)
    bool jumpDirectionSet;
    float jumpDirection;    // radians, as the seat's facing; unset: the side of the car the seat is on
};

// a CON file's AddVehicleCharacter: in every car of that type while it is in the world
struct ModVehicleCharacter
{
    enum { MAX_SUPPRESSORS = 4 };
    char character[ 32 ];
    char joint[ 16 ];
    float rotation;         // degrees; 180 faces forwards
    ModSeatSettings settings;
    // AddVehicleCharacterSuppressionCharacter: not in the car while any of these (or itself) is a suppressed driver
    // (SuppressDriver); "none" = never
    bool neverSuppressed;
    int numSuppressors;
    char suppressors[ MAX_SUPPRESSORS ][ 32 ];
};

#endif // RAD_LINUX

#endif // MODSEAT_H
