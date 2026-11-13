// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Mod Launcher CustomInteriorSupport: a mod's CustomInteriorSupport.ini (port/compat/shar_modinteriors.cpp), per
 * interior
 *
 *   [Interior]
 *   Name=moe1                     the name InteriorEntrance locators use
 *   Level=2                       1-7, repeated for each level it is in
 *   ShapeType=Sphere              X, Y, Z, Radius; or Box: MinX, MinY, MinZ, MaxX, MaxY, MaxZ
 *   MirrorXX=-1.0 ... MirrorWW=1.0   the reflection matrix (rows X, Y, Z, W; W = translation)
 *   ResetAmbientCharacter=moe     repeated; ambient characters put back in place on entering
 *   AllowJumping=1, AllowKicking=1, AllowSprinting=1   (none by default, as in the game's interiors)
 *
 * Only read when a mod requires the hack. The list replaces the game's built-in interior positions
 * (InteriorManager::ClassifyPoint: mods list the game's own interiors again, where and in which levels they want
 * them); a listed Mirror / ResetAmbientCharacter replaces the built-in one (Moe's, Bart's room), else that stays.
 * Read once, on first use; static storage only. */
#ifndef SHAR_COMPAT_SHAR_MODINTERIORS_H
#define SHAR_COMPAT_SHAR_MODINTERIORS_H

#ifdef __cplusplus
extern "C" {
#endif

#define SHAR_MODINTERIOR_MAX_RESETS 8

typedef struct SharModInterior
{
    char name[ 64 ];
    int levels;                 /* bit n-1: level n (1-7) */
    int isBox;                  /* else a sphere */
    float centre[ 3 ], radius;  /* sphere */
    float min[ 3 ], max[ 3 ];   /* box */
    int hasMirror;
    float mirror[ 16 ];         /* row-major, as rmt::Matrix */
    int numResets;
    char resetCharacters[ SHAR_MODINTERIOR_MAX_RESETS ][ 64 ];
    int allowJumping, allowKicking, allowSprinting;
} SharModInterior;

/* The interiors of the first mod with a CustomInteriorSupport.ini, if a mod requires the hack. */
int shar_ModInteriorCount( void );
const SharModInterior* shar_ModInteriorAt( int i );

/* The interior named (case-insensitive), or NULL. */
const SharModInterior* shar_ModInterior( const char* name );

/* Whether a point lies in the interior's shape. */
int shar_ModInteriorContains( const SharModInterior* interior, float x, float y, float z );

#ifdef __cplusplus
}
#endif

#endif
