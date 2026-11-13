// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Mod Launcher CustomAudioSupport: a mod's CustomAudioSupport.xml (port/compat/shar_modaudio.cpp)
 *
 *   <CustomAudioSupport MusicSamplingRate="24000" ForcedMusicRMSLevel="1">
 *     <Interior Name="KwikEMart" MusicEventIndex="20" />
 *     <Level Index="0" StartAmbienceEventIndex="9" BirdSound="null">    (0-6; or Number="1", 1-7)
 *       <Mission Index="0" StartAmbienceEventIndex="6" BirdSound="..." />  (0-7)
 *     </Level>
 *   </CustomAudioSupport>
 *
 * Mission StartAmbienceEventIndex: the AmbientEventList event (musicplayer.h) started with the mission (the
 * game's startingAmbiences); 76 (AEVENT_INTERIOR_HOUSE) means the level's. Level StartAmbienceEventIndex: that
 * level's LocatorEvent ambience (startingExteriorAmbiences, AMBIENT_SOUND_CITY = 7). Interior MusicEventIndex: a
 * MusicEventList event on entering the interior. BirdSound: the sound resource the bird event locator plays
 * (EVENT_PLAY_BIRD_SOUND), "null" for none; a mission's overrides its level's. MusicSamplingRate: of the radMusic
 * stream players. ForcedMusicRMSLevel: a level number (sound\music\L<n>_music.rms) or an RMS file path, loaded
 * once and never unloaded, for every level and the front end (the Mod Launcher's docs do not say which form).
 * Only read when a mod requires the hack. Read once, on first use; static storage only. */
#ifndef SHAR_COMPAT_SHAR_MODAUDIO_H
#define SHAR_COMPAT_SHAR_MODAUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

/* The ambience event a mission starts with (level index 0-6, mission index 0-7), else defaultEvent. */
int shar_ModAudioMissionAmbience( int levelIndex, int missionIndex, int defaultEvent );

/* The level's exterior ambience (a LocatorEvent) for missions whose ambience is 76, else defaultEvent. */
int shar_ModAudioLevelAmbience( int levelIndex, int defaultEvent );

/* Interiors with a music event: their count, and each one's name and event. */
int shar_ModAudioInteriorCount( void );
const char* shar_ModAudioInterior( int i, int* musicEvent );

/* The bird sound for the mission: the mod's, else defaultName; NULL for none. */
const char* shar_ModAudioBirdSound( int levelIndex, int missionIndex, const char* defaultName );

/* The radMusic sampling rate, else defaultRate. */
unsigned int shar_ModAudioMusicSamplingRate( unsigned int defaultRate );

/* The music script to load always, or NULL. */
const char* shar_ModAudioForcedMusicFile( void );

#ifdef __cplusplus
}
#endif

#endif
