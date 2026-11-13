// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Mod Launcher CustomRoadBehaviour: a mod's CustomRoadBehaviour.xml (port/compat/shar_modroads.cpp)
 *
 *   <CustomRoadBehaviour>
 *     <Level Index="0">                        (0-6; or Number="1", 1-7)
 *       <Behaviour Name="UTurnTolerance" Value="0.10" />        level-wide (not implemented: logged)
 *       <Road Name="Road006">
 *         <Behaviour Name="DisableTrafficGroundSnapping" Value="true" />
 *       </Road>
 *     </Level>
 *   </CustomRoadBehaviour>
 *
 * DisableTrafficGroundSnapping: traffic on that road keeps the road's height instead of being moved onto the
 * collision found below it (TrafficLocomotion; bridges above other ground). Only read when a mod requires the hack.
 * Read once, on first use; static storage only. */
#ifndef SHAR_COMPAT_SHAR_MODROADS_H
#define SHAR_COMPAT_SHAR_MODROADS_H

#ifdef __cplusplus
extern "C" {
#endif

/* Whether traffic on the road named (case-insensitive) in that level (index 0-6) is not snapped to the ground. */
int shar_ModRoadNoGroundSnap( int levelIndex, const char* roadName );

#ifdef __cplusplus
}
#endif

#endif
