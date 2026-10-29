/* Mods in the format of Lucas' Simpsons Hit & Run Mod Launcher (port/compat/shar_mods.cpp, shar_modlua.cpp).
 * SHAR_MODS lists mods, ':'-separated, each an extracted mod folder or a .lmlm package (read in place);
 * earlier mods win when two provide the same file (port/play.sh --mod sets it).
 * A main mod (Meta.ini Main=1) gets its own saved games in ModSaves/<InternalName>/ in the game dir.
 * Supported: CustomFiles (a mod's CustomFiles/ tree overrides game files, also those inside RCF archives, also
 * by the sound system's key strings; CustomFiles.ini redirections, occluded paths and Lua path handlers;
 * CustomFiles.lua), OggVorbisSupport (x.ogg serves x.rsd), CustomLimits (only the limits wired up through
 * shar_ModLimit); read by the game through shar_ModConfig: CustomShopSupport (shar_modshops.h), CustomCarSupport (shar_modcars.h),
 * CustomDialogueCharacterCodes (dialogline.cpp, dialoglist.cpp), CustomText (guitextbible.cpp; Language setting), 3DPhoneBoothPreviewSupport (guiscreenphonebooth.cpp),
 * CustomInteriorSupport (shar_modinteriors.h), CustomRoadBehaviour (shar_modroads.h), CustomAudioSupport (shar_modaudio.h),
 * CustomTriggerActions (code/meta/modtriggeractions.h), CustomBonusMissionSupport (shar_modbonus.h),
 * CustomSkinPreviewAnimations (guiscreenpurchaserewards.cpp), OverrideShaderParameters (shar_modshaders.h),
 * CustomCharacterSupport (shar_modcharacters.h); switches (port/notes/mods.md): NoCheats, NoWrenches,
 * NoHitAndRunMusic, NoBustedHitAndRunMeterReset, NoFastCarReset, NoAutomaticSavedGameLoad,
 * NoSavedGames, UnlockAllMissions, UnlockAllVehicles, UnlockAllOutfits, UnlockBonusGame,
 * NoBonusGame, NoMissionStartCameras, NoNeitherRoadArrowProcessing,
 * NoInactiveDynamicObjectCollisions, ReplayableBonusMissions;
 * DynamicTreeNodeEntityLimits (WorldScene).
 * Other hacks a mod's Meta.ini requires are reported at startup and ignored. Mod settings are
 * their Meta.ini defaults unless ModSettings.ini in the game dir sets them ([InternalName] Name=value).
 * SHAR_MOD_LOG=1 prints every file a mod provides as it is opened.
 * Loaded on first use; malloc only (called from the file system, see the compat-layer rule in CLAUDE.md). */
#ifndef SHAR_COMPAT_SHAR_MODS_H
#define SHAR_COMPAT_SHAR_MODS_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A file a mod provides: fd >= 0 is an open file (generated content) the receiver now owns, size bytes from 0;
 * otherwise bytes [offset, offset + size) of the file at path (the whole file for a mod folder's file, a slice
 * of the package for a .lmlm). */
typedef struct SharModFile
{
    const char* path;
    unsigned long long offset;
    unsigned int size;
    int packaged;
    int fd;
} SharModFile;

enum
{
    SHAR_MOD_MISSING = -1,      /* the file does not exist (occluded) */
    SHAR_MOD_NONE = 0,          /* no mod is involved: open the game's own file */
    SHAR_MOD_FILE = 1,          /* *file */
    SHAR_MOD_REDIRECT = 2       /* open the game's own file *redirect instead (it may be inside an RCF archive) */
};

/* What a read-only open of gamePath ("art\l1z1.p3d", any case, optional drive letter, or a sound key string)
 * gets. Runs Lua path handlers, so call it once per open. *redirect stays valid until the next call on this
 * thread. */
int shar_ModResolve(const char* gamePath, SharModFile* file, const char** redirect);

/* The radcore drive's half of an open whose shar_ModResolve the file request (requests.cpp) already made on this
 * thread: SHAR_MOD_FILE and *file when that was for gamePath, else resolves now. */
int shar_ModTakeResolved(const char* gamePath, SharModFile* file, const char** redirect);

/* The game-relative folder holding the main mod's saved games ("ModSaves/<InternalName>"), or NULL. */
const char* shar_ModSaveDir(void);

/* A CustomLimits.ini value ([section] key=value, case-insensitive). Several mods setting it: the largest wins.
 * Returns defaultValue when no mod sets it. */
int shar_ModLimit(const char* section, const char* key, int defaultValue);
/* The same as a float ([Heaps] sizes in MB). */
float shar_ModLimitFloat(const char* section, const char* key, float defaultValue);
/* Whether a mod gives the key with no value ("BoundsMinimumY="; the defaults above apply to it). */
int shar_ModLimitBlank(const char* section, const char* key);

/* A config file in a mod's root folder ("CustomShopSupport.xml"), from the first mod that has one, as malloc'd
 * NUL-terminated text the caller frees; NULL when no mod has it. */
char* shar_ModConfig(const char* name);

/* Walks the config file shar_ModConfig(file) would return, in file order, repeated sections and keys included:
 * cb(section, NULL, NULL, user) where a section starts, cb(section, key, value, user) for each key=value.
 * *modName (when not NULL) gets the InternalName of the mod it came from. 0 when no mod has the file. */
typedef void ( *SharModIniCallback )( const char* section, const char* key, const char* value, void* user );
int shar_ModConfigIni(const char* file, SharModIniCallback cb, void* user, const char** modName);

/* A mod's setting by InternalName (case-insensitive): its ModSettings.ini value, else the Meta.ini default, copied
 * to out; 1 if the mod has it. */
int shar_ModSetting(const char* modName, const char* setting, char* out, size_t size);

/* A value from a config file shar_ModConfig(file) would return ([section] key=value, case-insensitive): copied to out,
 * 1 if found; the int form returns defaultValue when it is missing or blank. */
int shar_ModConfigValue(const char* file, const char* section, const char* key, char* out, size_t size);
int shar_ModConfigInt(const char* file, const char* section, const char* key, int defaultValue);

/* The files directly in a mod's root subfolder ("CustomText"), from the first mod that has any: up to max texts
 * (malloc'd, NUL-terminated after sizes[i] bytes, which may include zeros, e.g. UTF-16) and file names (malloc'd),
 * all for the caller to free. Returns the count. */
int shar_ModConfigFolder(const char* folder, char** texts, size_t* sizes, char** names, int max);
/* The same from the nth mod (0: first, in load order) that has any; modName (may be NULL) gets that mod's name. */
int shar_ModConfigFolderNth(const char* folder, int nth, char** texts, size_t* sizes, char** names, int max,
                            const char** modName);

/* The Language setting (ModSettings.ini [InternalName] Language=..., the Mod Launcher's choice among a mod's
 * CustomText files by their Title) of the mod shar_ModConfigFolder(folder) reads from; NULL when not set. */
const char* shar_ModConfigFolderLanguage(const char* folder);

/* Whether a loaded mod's Meta.ini has RequiredHack=hack (case-insensitive): for behaviour the Mod Launcher enables
 * only for mods that ask for it. */
int shar_ModHackRequired(const char* hack);

#ifdef __cplusplus
}
#endif

#endif
