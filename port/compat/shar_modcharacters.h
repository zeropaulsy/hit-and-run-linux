/* Mod Launcher CustomCharacterSupport: a mod's CustomCharacterSupport.ini (port/compat/shar_modcharacters.cpp)
 *
 *   [homer]                       a character model (skin) name, any case
 *   BlendModeSupport=1            its additive / subtractive shaders keep their blend mode (characterrenderable.cpp;
 *                                 the game forces every character shader to alpha)
 *   IsLisa=1 / IsMarge=1          sits 0.12 m higher in cars / hair joints 33-35 kept up in high-roof cars
 *                                 (defaults: "lisa" / "marge" or l_ / m_ prefixes, charactermanager.cpp)
 *   IdleAnimationWeight=5         once per <model>_idle<N> animation, in order: relative chances (action.cpp;
 *   IdleAnimationWeight=3         the game's are 5, 3, 2)
 *
 * Only read when a mod requires the hack. Read once, on first use; static storage only. */
#ifndef SHAR_COMPAT_SHAR_MODCHARACTERS_H
#define SHAR_COMPAT_SHAR_MODCHARACTERS_H

#ifdef __cplusplus
extern "C" {
#endif

#define SHAR_MODCHAR_MAX_IDLES 16

typedef struct SharModCharacter
{
    char name[ 64 ];
    int blendModeSupport;
    int isLisa;                 /* -1: the game's rule */
    int isMarge;                /* -1: the game's rule */
    int numIdleWeights;
    float idleWeights[ SHAR_MODCHAR_MAX_IDLES ];
} SharModCharacter;

/* The mod's settings for the character model, or NULL. */
const SharModCharacter* shar_ModCharacter( const char* modelName );

#ifdef __cplusplus
}
#endif

#endif
