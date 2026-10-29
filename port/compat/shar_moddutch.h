/* Mod Launcher CustomDutchAngleNPCCameraTilt: a mod's CustomDutchAngleNPCCameraTilt.xml (port/compat/shar_moddutch.cpp)
 *
 *   <CustomDutchAngleNPCCameraTilt TiltAngle="10">         degrees (default: the game's, ~10)
 *     <Level Number="1" DutchCameraTilt="true">            (or Index="0"); default on for level 7 only
 *       <Mission Number="1" DutchCameraTilt="false" TiltAngle="15" />   (or Index; story mission, any stage)
 *     </Level>
 *   </CustomDutchAngleNPCCameraTilt>
 *
 * The walker camera's tilt near pedestrians (walkercam.cpp). Only read when a mod requires the hack (the file is
 * optional). Read once, on first use; static storage only. */
#ifndef SHAR_COMPAT_SHAR_MODDUTCH_H
#define SHAR_COMPAT_SHAR_MODDUTCH_H

#ifdef __cplusplus
extern "C" {
#endif

/* -1: the hack is not required (the game's rule); else 1 when level (0-6) / mission (0-based story mission) tilts,
 * with *degrees its angle, 0 when it does not. */
int shar_ModDutchTilt( int level, int mission, float* degrees );

#ifdef __cplusplus
}
#endif

#endif
