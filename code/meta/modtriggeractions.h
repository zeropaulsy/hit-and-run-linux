// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

//=============================================================================
// Linux port: Mod Launcher CustomTriggerActions (docs.donutteam.com, Hacks/CustomTriggerActions, version 1.27).
// A mod with RequiredHack=CustomTriggerActions binds actions to trigger volumes and game events in
// CustomTriggerActions.ini (the first mod's):
//   [Action]     Name, Type: ChangeTexture (Shader, Texture), ChangeHUDMapDrawable (DrawableName), DestroyCar,
//                SetHitAndRunMeter (Value 0-100), ChangeTrafficGroup (Value), TriggerMusicEvent (Event: a name in
//                the current music RMS), TiltCamera (Value degrees, Duration seconds back to level after the
//                last tilting trigger is left, Reset: back to level on a mission change)
//   [Condition]  Name, Type, Not: Car / PlayerCar (CarName or CarIndex), Character (CharacterName), Conditions
//                (all of Condition=...), Condition (any of them), IsPlayer, Mission (Level; Mission, SundayDrive;
//                or BonusMission 1-5, also 21-25 as the launcher before 1.22.2), UnlockedMission (Level, Mission),
//                CompletedMission (Level; Mission, or BonusMission 1-5 with BestTime seconds), Setting
//                (SettingName, SettingValue, ModName: default the mod with the ini)
//   [Trigger]    Name, LocatorType (Locator data type; 0 with LocatorType0Event, 9 with LocatorType9Type = the
//                action locator's action name), LocatorName=..., TriggerName=... (any of each), Condition=...
//                (any): every criterion given has to match
//   [CustomTriggerAction]  Action=..., Trigger=... and/or Event=... (retail event numbers), Condition=... (any;
//                none: always), PreventDefault
// Actions run when the player or a registered AI car enters a matching trigger (not again until it leaves);
// PreventDefault then hides both the entering and the leaving from the locator. An event's actions run before its
// listeners hear it, and PreventDefault drops it; an event has no entity, so the entity conditions are false for it
// and DestroyCar does nothing. Unknown names and types are logged once and skipped.
// Read on first use; static storage only.
//=============================================================================
#ifndef MODTRIGGERACTIONS_H
#define MODTRIGGERACTIONS_H

#ifdef RAD_LINUX

#include <events/eventenum.h>

class TriggerVolume;
class TriggerLocator;

namespace ModTriggerActions
{
    // TriggerVolume::Trigger: true keeps the locator from seeing it (PreventDefault)
    bool OnTrigger( TriggerVolume* volume, TriggerLocator* locator, unsigned int playerID, bool entered );

    // EventManager::TriggerEvent: true drops the event (PreventDefault)
    bool OnEvent( EventEnum id, void* data );

    // the camera roll TiltCamera adds for the player, radians (SuperCam::SetCameraValues)
    float CameraTilt( int playerID );
}

#endif // RAD_LINUX

#endif // MODTRIGGERACTIONS_H
