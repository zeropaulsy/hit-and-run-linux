/* Mod Launcher CustomCarSupport: a mod's CustomCarSupport.ini (port/compat/shar_modcars.cpp), per car either
 *
 *   [Car]                 (Mod Launcher 1.26+)        or     [fry_v]            (older: the section is the name)
 *   Name=fry_v                                               Index=97
 *   Index=97                                                 NoHusk=1
 *   NoHusk=1 ...
 *
 * Read: Index (car camera data chunk id, 97-255 for new cars), NoHusk, NoSkidMarks, NoFrontSkidMarks,
 * ShadowVisible, Abductable, Recolourable, Invincible, NoBumperCam, RearWheelSparks (BackWheelSparks), PreviewScale,
 * Husk, IsHusk, Front/Rear/Wheel0-3SteeringVisualMultiplier, Front/Rear/Wheel0-3SkidMarkWidthMultiplier, FakeWheel=
 * with [FakeWheel] sections (Name, Joint, CopyRotation, CopySuspension, SteeringMultiplier, CopySkidMarks,
 * SkidMarkWidthMultiplier; repeated keys add wheels).
 * Cars the game does not know load and drive anyway (their vehicle ID is VehicleEnum::INVALID); the ini adds
 * their camera data and these switches. Read once, on first use; static storage only. */
#ifndef SHAR_COMPAT_SHAR_MODCARS_H
#define SHAR_COMPAT_SHAR_MODCARS_H

#ifdef __cplusplus
extern "C" {
#endif

#define SHAR_MODCAR_UNSET ( -12345.0f )   /* a float setting not given */
#define SHAR_MODCAR_MAX_FAKE_WHEELS 8

typedef struct SharModFakeWheel
{
    char name[ 32 ];
    char joint[ 32 ];           /* the joint driven */
    int copyRotation;           /* bit i: wheel i's spin is added */
    int copySuspension;         /* bit i: wheel i's suspension travel (the highest of them is used) */
    int copySkidMarks;          /* bit i: wheel i's skid marks, drawn under the fake wheel too */
    float steeringMultiplier;   /* of the car's steering angle, default 0 */
    float skidMarkWidthMultiplier;
} SharModFakeWheel;

/* int settings: -1 = not set (the game's own behaviour); floats: SHAR_MODCAR_UNSET. Wheels: 0 back right,
 * 1 back left, 2 front left, 3 front right. */
typedef struct SharModCar
{
    char name[ 32 ];
    int index;
    int noHusk, noSkidMarks, noFrontSkidMarks, shadowVisible, abductable, recolourable, invincible, noBumperCam;
    int rearWheelSparks, isHusk;
    char husk[ 32 ];            /* "" = huskA */
    float previewScale;
    float frontSteering, rearSteering, wheelSteering[ 4 ];      /* steering visual multipliers */
    float frontSkidWidth, rearSkidWidth, wheelSkidWidth[ 4 ];   /* skid mark width multipliers */
    int numFakeWheels;
    const SharModFakeWheel* fakeWheels[ SHAR_MODCAR_MAX_FAKE_WHEELS ];
} SharModCar;

/* All cars a mod sets anything for (for the husk pool: the Husk= cars it makes at level start). */
int shar_ModCarCount( void );
const SharModCar* shar_ModCarAt( int i );

/* A wheel's steering visual multiplier (per-wheel setting, else front/rear, else 1 front / 0 back) and skid mark
 * width multiplier (default 1). */
float shar_ModCarSteering( const SharModCar* car, int wheel );
float shar_ModCarSkidWidth( const SharModCar* car, int wheel );

/* The settings for the car named (case-insensitive); NULL when no mod sets any. */
const SharModCar* shar_ModCar( const char* name );

#ifdef __cplusplus
}
#endif

#endif
