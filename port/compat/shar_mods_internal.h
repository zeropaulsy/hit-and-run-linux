// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

/* Shared by shar_mods.cpp (mod list, file index, virtual file system) and shar_modlua.cpp (Lua host).
 * Not for game code: that uses shar_mods.h. malloc only, like the rest of the compat layer. */
#ifndef SHAR_COMPAT_SHAR_MODS_INTERNAL_H
#define SHAR_COMPAT_SHAR_MODS_INTERNAL_H

#include "shar_mods.h"
#include <stddef.h>
#include <stdint.h>

struct ModPackageEntry
{
    char* rel;              /* path inside the package, '/' separators, original case */
    char* key;              /* rel normalised (lower case) */
    uint64_t offset;
    uint64_t size;
};

struct ModHandler           /* CustomFiles.ini [PathHandlers] */
{
    char* pattern;          /* normalised game path, '*' and '?' wildcards */
    char* script;           /* mod-relative path of the Lua script */
};

enum ModSettingType { MOD_SETTING_NUMBER, MOD_SETTING_BOOLEAN, MOD_SETTING_TEXT, MOD_SETTING_CHOICE };

struct ModSetting           /* Meta.ini [Setting] */
{
    char* name;
    char* value;            /* the default, or the user's value from ModSettings.ini */
    ModSettingType type;
};

struct Mod
{
    int index;
    char name[ 128 ];       /* InternalName (or the folder/package name) */
    char title[ 256 ];
    char version[ 64 ];     /* Meta.ini Version */
    char* root;             /* the folder, or the .lmlm package */
    bool packaged;
    int fd;                 /* package: open for the whole session */
    uint64_t fileSize;
    ModPackageEntry* entries;
    int numEntries;
    ModHandler* handlers;
    int numHandlers;
    ModSetting* settings;
    int numSettings;
    char* language;         /* ModSettings.ini Language= (the Mod Launcher's own setting for CustomText); NULL */
    bool hasLua;            /* CustomFiles.lua or path handlers */
    void* lua;              /* lua_State, shar_modlua.cpp */
};

extern Mod* g_Mods;
extern int g_NumMods;

char* ModDup( const char* s );
void ModNormalise( const char* in, char* out, size_t size );   /* see shar_mods.cpp */
bool ModWildcardMatch( const char* pattern, const char* key );  /* both normalised; '*' crosses '/' */
bool ModHackLoaded( const char* hack );
bool ModSettingHackCovered( const char* name );
bool ModLogEnabled( void );

/* Virtual file system, as the Mod Launcher's: /Mods/<InternalName>/... (a mod's own files), /GameData/... (the
 * game's path space: mods' CustomFiles over the game dir's loose files; RCF contents are not included) and
 * /GameDir/... (the game dir only). A path without a mount point is a /GameData one. */
bool ModVfsRead( const char* vpath, char** data, size_t* size );    /* malloc'd, NUL-terminated */
bool ModVfsExists( const char* vpath, bool* isDir );
typedef bool ( *ModVfsEntryCallback )( const char* name, bool isDir, void* user );   /* false stops */
bool ModVfsList( const char* vpath, ModVfsEntryCallback cb, void* user, bool* completed );
/* Where a file's bytes are: 1 and *file (a real file, or a slice of a package); 2 and gamePath when the path is
 * a /GameData one found neither in mods nor loose (it may be inside an RCF archive); 0 when not found. */
int ModVfsLocate( const char* vpath, SharModFile* file, char* gamePath, size_t gamePathSize );

/* Lua host (shar_modlua.cpp) */
void ModLuaStart( Mod* mod );           /* new state for the mod, runs its CustomFiles.lua */
/* Runs a path handler for gamePath: 1 and *fd/*size when it produced output (a memfd), 2 and redirect (a virtual
 * path) when it redirected, 0 when it did neither or failed. */
int ModLuaRunHandler( Mod* mod, const char* script, const char* gamePath, int* fd, unsigned int* size,
                      char* redirect, size_t redirectSize );

#endif
