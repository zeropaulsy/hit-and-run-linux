// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Mod Launcher CustomTrafficSupport: a mod's CustomTrafficSupport.xml (port/compat/shar_modtraffic.cpp)
 *
 *   <CustomTrafficSupport>
 *     <DynamicTraffic Type="None|Models|Slots" />
 *     <PreallocatedCars Amount="5" />  <AllocatedCars Amount="5" />  <MaxTraffic Amount="5" />  <MaxTrafficOnFoot Amount="5" />
 *     <RoadTrafficLimitMultiplier Value="1" />  <RoadLaneMaxTrafficLimit Amount="2" />
 *     <RemoveInterval Value="200" />  <AddInterval Value="200" />  <PopulationDuration Value="3000" />   (ms)
 *     <FadeDistance Value="85" />  <CentreOffset Value="40" />  <AddDistance Value="65" />  <RemoveDistance Value="75" />
 *     <PopulateAddDistance Value="100" />
 *     <DecelerationSpeed Value="-10" />  <AccelerationSpeed Value="3.5" />  <DecelerationBrakeThreshold Value="1" />
 *     <Colours Inherit="false"> <Colour Red="" Green="" Blue="" /> ... </Colours>
 *     <Car Name="traffA"> <Colours ...> </Car>
 *     <Group Index="0" Multiplier="1"> <Car Name="traffA" Weight="2" /> </Group>
 *     <Level Index="0" | Number="1"> any of the above </Level>
 *   </CustomTrafficSupport>
 *
 * Only read when a mod requires the hack. A level's elements override the root's. Colours: the first list found
 * among the level's car, the root's car, the level, the root is used; with Inherit="true" the next one's colours are
 * added too, down to the game's own swatches. Read once, on first use; static storage only. */
#ifndef SHAR_COMPAT_SHAR_MODTRAFFIC_H
#define SHAR_COMPAT_SHAR_MODTRAFFIC_H

#ifdef __cplusplus
extern "C" {
#endif

#define SHAR_MODTRAFFIC_UNSET ( -12345.0f )     /* a setting not given: the game's own value */

enum { SHAR_TRAFFIC_DYNAMIC_NONE = 0, SHAR_TRAFFIC_DYNAMIC_MODELS = 1, SHAR_TRAFFIC_DYNAMIC_SLOTS = 2 };

typedef struct SharModTrafficSettings
{
    float dynamicType;          /* SHAR_TRAFFIC_DYNAMIC_* */
    float preallocatedCars, allocatedCars, maxTraffic, maxTrafficOnFoot;
    float roadTrafficLimitMultiplier, roadLaneMaxTrafficLimit;
    float removeInterval, addInterval, populationDuration;
    float fadeDistance, centreOffset, addDistance, removeDistance, populateAddDistance;
    float decelerationSpeed, accelerationSpeed, decelerationBrakeThreshold;
} SharModTrafficSettings;

/* Whether a mod requires the hack and provides the file. */
int shar_ModTrafficActive( void );

/* The settings for a level (0-6): the level's overrides on the root's; every field SHAR_MODTRAFFIC_UNSET if not given. */
void shar_ModTrafficSettings( int level, SharModTrafficSettings* out );

/* The largest AllocatedCars over the root and all levels, or 0. */
int shar_ModTrafficMaxAllocated( void );

/* The colour list for a car (name case-insensitive) in a level: up to max RGB triples into rgb; returns the count;
 * *withGame set when the game's own swatches are added too (the chain ended in Inherit="true" or no list at all). */
int shar_ModTrafficColours( int level, const char* car, unsigned char ( *rgb )[ 3 ], int max, int* withGame );

/* A traffic group's settings: Multiplier (scales the group's counts; 1 if not given) and a car's Weight (the
 * defaultWeight if not given). */
float shar_ModTrafficGroupMultiplier( int level, int group );
float shar_ModTrafficCarWeight( int level, int group, const char* car, float defaultWeight );

#ifdef __cplusplus
}
#endif

#endif
