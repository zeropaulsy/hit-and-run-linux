// Mod Launcher-format mods: mod list, file index, virtual file system, open resolution. See shar_mods.h.
// The Lua side (CustomFiles.lua, path handlers) is in shar_modlua.cpp.
#include "shar_mods_internal.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

Mod* g_Mods = NULL;
int g_NumMods = 0;

namespace {

struct ModFile
{
    char* key;              // normalised game path: lower case, '/' separators ("art/l1z1.p3d")
    char* path;             // real path on disk: the file, or the .lmlm package holding it
    uint64_t offset;        // where the data starts in path (0 for a loose file)
    unsigned int size;
    bool packaged;
    int order;              // mod index * large + discovery order, so earlier mods sort first among equal keys
};

// CustomFiles.ini rules: [PathRedirections] / [AdditionalFiles] src=dst (open dst, a game path, instead) and
// [Miscellaneous] OccludedPath=path (the file does not exist)
struct Rule
{
    char* key;              // normalised source path
    char* target;           // game path to open instead, NULL when occluded
    int order;
};

// Lookups by key string: the sound system opens files by the decimal text of radMakeCaseInsensitiveKey32 of their
// path (backslashes), which RCF archives resolve (cementer.cpp). Files and rules are indexed by that key too.
struct KeyEntry
{
    uint32_t key;
    int index;              // into s_Files or s_Rules
    int priority;           // 0: the file's own name, 1: an .ogg standing in for the .rsd of that name
};

struct Limit
{
    char* section;
    char* key;
    double value;
    bool blank;             // "Key=" with no value (TreeNodes bounds: keep the k-d tree's)
};

ModFile* s_Files = NULL;
int s_NumFiles = 0, s_CapFiles = 0;
Rule* s_Rules = NULL;
int s_NumRules = 0, s_CapRules = 0;
KeyEntry* s_FileKeys = NULL;
int s_NumFileKeys = 0;
KeyEntry* s_RuleKeys = NULL;
int s_NumRuleKeys = 0;
Limit* s_Limits = NULL;
int s_NumLimits = 0, s_CapLimits = 0;
bool s_Log = false;
char s_SaveDir[ 128 ] = "";     // game-relative folder of the main mod's saves, "" for none
char s_GameRoot[ PATH_MAX ] = "";
pthread_once_t s_Once = PTHREAD_ONCE_INIT;

// Hacks implemented here; reported by IsHackLoaded and not listed as ignored
const char* const SUPPORTED_HACKS[] = { "CustomFiles", "CustomLimits", "OggVorbisSupport", "LuaSupport", "CustomShopSupport",
                                        "CustomDialogueCharacterCodes", "CustomText", "3DPhoneBoothPreviewSupport",
                                        "CustomCarSupport", "IncreasedVideoResolutionSupport",
                                        "AdditionalScriptFunctionality", "CustomInteriorSupport", "DynamicTreeNodeEntityLimits", "CustomRoadBehaviour",
                                        "CustomLicenseScreenTime", "NoMainMenuCameraAnimations", "HUDMapIgnorePlayerHeight",
                                        "CustomTrafficSupport", "CustomStatsTotals", "CustomAudioSupport",
                                        "CustomAnimationSetSupport",  // no-op: see port/notes/mods.md
                                        "CustomTriggerActions", "CustomBonusMissionSupport", "CustomSkinPreviewAnimations",
                                        "OverrideShaderParameters", "CustomCharacterSupport",
                                        "NoCheats", "NoWrenches", "NoHitAndRunMusic",
                                        "NoBustedHitAndRunMeterReset", "NoFastCarReset",
                                        "NoAutomaticSavedGameLoad", "NoSavedGames",
                                        "UnlockAllMissions", "UnlockAllVehicles",
                                        "UnlockAllOutfits", "UnlockBonusGame",
                                        "NoBonusGame",
                                        "SkippableFMVs",  // no-op: see port/notes/mods.md
                                        "NoMissionStartCameras", "NoNeitherRoadArrowProcessing",
                                        "NoInactiveDynamicObjectCollisions", "ReplayableBonusMissions",
                                        "BugFixes", "CustomCollectorCardSupport", "CustomDutchAngleNPCCameraTilt",
                                        "CustomHeadlights", "CustomMissionSkipFailCounts",
                                        "CustomVideoResolutionSupport", "DrunkCamera", "IncreasedRewardLimits",
                                        "PerLevelCoinCount", "RefractionShaderSupport",
                                        "WalkerCameraDataSupport" };

void Init( void );

template< typename T > void Grow( T*& array, int count, int& capacity, int first )
{
    if ( count == capacity )
    {
        capacity = capacity ? capacity * 2 : first;
        array = (T*)realloc( array, capacity * sizeof( T ) );
    }
}

// An entry of dir matching name case-insensitively, as a full path (malloc'd), or NULL.
char* FindNoCase( const char* dir, const char* name )
{
    DIR* d = opendir( dir );
    if ( d == NULL )
    {
        return NULL;
    }
    char* found = NULL;
    struct dirent* e;
    while ( ( e = readdir( d ) ) != NULL )
    {
        if ( strcasecmp( e->d_name, name ) == 0 )
        {
            found = (char*)malloc( strlen( dir ) + strlen( e->d_name ) + 2 );
            sprintf( found, "%s/%s", dir, e->d_name );
            break;
        }
    }
    closedir( d );
    return found;
}

// root + rel (normalised, '/' separators), each component matched case-insensitively. False if one is missing.
bool ResolveNoCase( const char* root, const char* rel, char* out, size_t size )
{
    snprintf( out, size, "%s", root );
    const char* p = rel;
    while ( *p != '\0' )
    {
        const char* end = strchr( p, '/' );
        size_t n = end != NULL ? (size_t)( end - p ) : strlen( p );
        char name[ NAME_MAX + 1 ];
        if ( n > NAME_MAX )
        {
            return false;
        }
        memcpy( name, p, n );
        name[ n ] = '\0';
        char* found = FindNoCase( out, name );
        if ( found == NULL )
        {
            return false;
        }
        snprintf( out, size, "%s", found );
        free( found );
        p += n;
        if ( *p == '/' )
        {
            p++;
        }
    }
    return true;
}

void AddFile( const char* rel, const char* path, uint64_t offset, unsigned int size, bool packaged, int modIndex )
{
    Grow( s_Files, s_NumFiles, s_CapFiles, 256 );
    char key[ PATH_MAX ];
    ModNormalise( rel, key, sizeof( key ) );
    ModFile& f = s_Files[ s_NumFiles ];
    f.key = ModDup( key );
    f.path = ModDup( path );
    f.offset = offset;
    f.size = size;
    f.packaged = packaged;
    f.order = modIndex * 1000000 + s_NumFiles;
    s_NumFiles++;
}

// Index every file under dir; rel is its game path so far.
int ScanTree( const char* dir, const char* rel, int modIndex )
{
    DIR* d = opendir( dir );
    if ( d == NULL )
    {
        return 0;
    }
    int count = 0;
    struct dirent* e;
    while ( ( e = readdir( d ) ) != NULL )
    {
        if ( strcmp( e->d_name, "." ) == 0 || strcmp( e->d_name, ".." ) == 0 )
        {
            continue;
        }
        char path[ PATH_MAX ], childRel[ PATH_MAX ];
        snprintf( path, sizeof( path ), "%s/%s", dir, e->d_name );
        snprintf( childRel, sizeof( childRel ), "%s%s%s", rel, rel[ 0 ] ? "/" : "", e->d_name );
        struct stat st;
        if ( stat( path, &st ) != 0 )
        {
            continue;
        }
        if ( S_ISDIR( st.st_mode ) )
        {
            count += ScanTree( path, childRel, modIndex );
        }
        else if ( S_ISREG( st.st_mode ) )
        {
            AddFile( childRel, path, 0, (unsigned int)st.st_size, false, modIndex );
            count++;
        }
    }
    closedir( d );
    return count;
}

//=============================================================================
// .lmlm packages ("LSPA"): 512-byte sectors, one field per sector (format notes in mod/tools/lmlm_extract.py)
//=============================================================================

struct PackageReader
{
    Mod* mod;
    unsigned char sector[ 512 ];
    unsigned int pos;       // next sector to read
    int capEntries;
    const char* error;
};

const unsigned int SECTOR = 512;
const unsigned int FLAG_TIMESTAMP = 0x02;

uint16_t U16( const unsigned char* p ) { return (uint16_t)( p[ 0 ] | p[ 1 ] << 8 ); }
uint64_t U64( const unsigned char* p )
{
    uint64_t v = 0;
    for ( int i = 7; i >= 0; i-- )
    {
        v = v << 8 | p[ i ];
    }
    return v;
}

bool NextSector( PackageReader& r )
{
    if ( (uint64_t)( r.pos + 1 ) * SECTOR > r.mod->fileSize ||
         pread( r.mod->fd, r.sector, SECTOR, (off_t)r.pos * SECTOR ) != (ssize_t)SECTOR )
    {
        r.error = "directory runs past the end of the file";
        return false;
    }
    r.pos++;
    return true;
}

bool ParseDir( PackageReader& r, const char* rel, unsigned int count, unsigned int firstType, int depth )
{
    if ( depth > 64 )
    {
        r.error = "directories nested too deeply";
        return false;
    }
    for ( unsigned int i = 0; i < count; i++ )
    {
        unsigned int type = firstType;
        if ( i > 0 )
        {
            if ( !NextSector( r ) )
            {
                return false;
            }
            type = r.sector[ 0 ];
        }
        if ( !NextSector( r ) )
        {
            return false;
        }
        unsigned int flags = U16( r.sector );
        if ( flags & ~FLAG_TIMESTAMP )
        {
            r.error = "unknown entry flags";
            return false;
        }
        char name[ 256 ];                               // UTF-16LE name; mod paths are ASCII in practice
        unsigned int n = 0;
        for ( unsigned int at = 2; at + 1 < SECTOR && n + 1 < sizeof( name ); at += 2 )
        {
            uint16_t c = U16( r.sector + at );
            if ( c == 0 )
            {
                break;
            }
            name[ n++ ] = c < 0x80 ? (char)c : '_';
        }
        name[ n ] = '\0';
        if ( n == 0 || strcmp( name, "." ) == 0 || strcmp( name, ".." ) == 0 || strchr( name, '/' ) || strchr( name, '\\' ) )
        {
            r.error = "bad entry name";
            return false;
        }
        char childRel[ PATH_MAX ];
        snprintf( childRel, sizeof( childRel ), "%s%s%s", rel, rel[ 0 ] ? "/" : "", name );
        if ( !NextSector( r ) )
        {
            return false;
        }
        const unsigned char* info = r.sector + 2 + ( ( flags & FLAG_TIMESTAMP ) ? 10 : 0 );
        if ( type == 1 )
        {
            if ( !ParseDir( r, childRel, U16( info ), info[ 2 ], depth + 1 ) )
            {
                return false;
            }
        }
        else if ( type == 0 )
        {
            uint64_t size = U64( info ), offset = U64( info + 8 );
            if ( offset > r.mod->fileSize || size > r.mod->fileSize - offset || size > 0xFFFFFFFFu )
            {
                r.error = "file data past the end of the package";
                return false;
            }
            Grow( r.mod->entries, r.mod->numEntries, r.capEntries, 256 );
            ModPackageEntry& e = r.mod->entries[ r.mod->numEntries++ ];
            char key[ PATH_MAX ];
            ModNormalise( childRel, key, sizeof( key ) );
            e.rel = ModDup( childRel );
            e.key = ModDup( key );
            e.offset = offset;
            e.size = size;
        }
        else
        {
            r.error = "unknown entry type";
            return false;
        }
    }
    return true;
}

const char* OpenPackage( Mod* mod )
{
    mod->fd = open( mod->root, O_RDONLY | O_CLOEXEC );
    struct stat st;
    if ( mod->fd < 0 || fstat( mod->fd, &st ) != 0 )
    {
        return "cannot open";
    }
    mod->fileSize = (uint64_t)st.st_size;
    PackageReader r;
    memset( &r, 0, sizeof( r ) );
    r.mod = mod;
    if ( !NextSector( r ) || memcmp( r.sector, "LSPA", 4 ) != 0 )
    {
        return "not an LSPA package";
    }
    r.pos = 2;                                          // root directory record
    if ( !NextSector( r ) || !ParseDir( r, "", U16( r.sector + 2 ), r.sector[ 4 ], 0 ) )
    {
        return r.error;
    }
    return NULL;
}

const ModPackageEntry* FindEntry( const Mod* mod, const char* key )
{
    for ( int i = 0; i < mod->numEntries; i++ )
    {
        if ( strcmp( mod->entries[ i ].key, key ) == 0 )
        {
            return &mod->entries[ i ];
        }
    }
    return NULL;
}

//=============================================================================
// INI files
//=============================================================================

// Minimal INI reader: [section], key=value, ';' / '#' comments, UTF-8 BOM, CRLF.
typedef void ( *IniCallback )( const char* section, const char* key, const char* value, void* user );

char* Trim( char* s )
{
    while ( isspace( (unsigned char)*s ) )
    {
        s++;
    }
    char* end = s + strlen( s );
    while ( end > s && isspace( (unsigned char)end[ -1 ] ) )
    {
        *--end = '\0';
    }
    return s;
}

// Parses text (modified in place).
void ParseIni( char* text, IniCallback cb, void* user )
{
    if ( (unsigned char)text[ 0 ] == 0xEF && (unsigned char)text[ 1 ] == 0xBB && (unsigned char)text[ 2 ] == 0xBF )
    {
        text += 3;
    }
    char section[ 128 ] = "";
    for ( char* save = NULL, *line = strtok_r( text, "\r\n", &save ); line != NULL; line = strtok_r( NULL, "\r\n", &save ) )
    {
        char* s = Trim( line );
        if ( s[ 0 ] == '\0' || s[ 0 ] == ';' || s[ 0 ] == '#' )
        {
            continue;
        }
        if ( s[ 0 ] == '[' )
        {
            char* end = strchr( s, ']' );
            if ( end != NULL )
            {
                *end = '\0';
                snprintf( section, sizeof( section ), "%s", Trim( s + 1 ) );
                cb( section, NULL, NULL, user );         // section start
            }
            continue;
        }
        char* eq = strchr( s, '=' );
        if ( eq == NULL )
        {
            continue;
        }
        *eq = '\0';
        cb( section, Trim( s ), Trim( eq + 1 ), user );
    }
}

char* ReadWholeFile( const char* path, size_t* sizeOut )
{
    FILE* f = fopen( path, "rb" );
    if ( f == NULL )
    {
        return NULL;
    }
    fseek( f, 0, SEEK_END );
    long size = ftell( f );
    fseek( f, 0, SEEK_SET );
    char* data = (char*)malloc( size + 1 );
    size_t got = fread( data, 1, size, f );
    data[ got ] = '\0';
    fclose( f );
    if ( sizeOut != NULL )
    {
        *sizeOut = got;
    }
    return data;
}

// A mod's own file (rel normalised), NUL-terminated (malloc'd), or NULL
char* ReadModFile( const Mod* mod, const char* rel, size_t* sizeOut )
{
    if ( mod->packaged )
    {
        const ModPackageEntry* e = FindEntry( mod, rel );
        if ( e == NULL )
        {
            return NULL;
        }
        char* data = (char*)malloc( e->size + 1 );
        if ( pread( mod->fd, data, e->size, (off_t)e->offset ) != (ssize_t)e->size )
        {
            free( data );
            return NULL;
        }
        data[ e->size ] = '\0';
        if ( sizeOut != NULL )
        {
            *sizeOut = e->size;
        }
        return data;
    }
    char path[ PATH_MAX ];
    return ResolveNoCase( mod->root, rel, path, sizeof( path ) ) ? ReadWholeFile( path, sizeOut ) : NULL;
}

bool ReadModIni( const Mod* mod, const char* name, IniCallback cb, void* user )
{
    char key[ PATH_MAX ];
    ModNormalise( name, key, sizeof( key ) );
    char* text = ReadModFile( mod, key, NULL );
    if ( text == NULL )
    {
        return false;
    }
    ParseIni( text, cb, user );
    free( text );
    return true;
}

//=============================================================================
// Loading
//=============================================================================

struct MetaInfo
{
    Mod* mod;
    bool main;
    bool inSetting;
    int capSettings;
    char unsupported[ 2048 ];
    char requiredMods[ 1024 ];
};

// every mod's RequiredHack= values, ", "-separated (shar_ModHackRequired)
char s_RequiredHacks[ 4096 ];

void Append( char* list, size_t size, const char* item )
{
    size_t len = strlen( list );
    snprintf( list + len, size - len, "%s%s", len ? ", " : "", item );
}

bool IsSupportedHack( const char* hack )
{
    for ( unsigned i = 0; i < sizeof( SUPPORTED_HACKS ) / sizeof( SUPPORTED_HACKS[ 0 ] ); i++ )
    {
        if ( strcasecmp( hack, SUPPORTED_HACKS[ i ] ) == 0 )
        {
            return true;
        }
    }
    return false;
}

void OnMeta( const char* section, const char* key, const char* value, void* user )
{
    MetaInfo* meta = (MetaInfo*)user;
    Mod* mod = meta->mod;
    if ( key == NULL )
    {
        // [Setting] sections repeat: each starts a new setting, filled in by its Name / Type / Default keys
        meta->inSetting = strcasecmp( section, "Setting" ) == 0;
        if ( meta->inSetting )
        {
            Grow( mod->settings, mod->numSettings, meta->capSettings, 8 );
            ModSetting& s = mod->settings[ mod->numSettings++ ];
            s.name = ModDup( "" );
            s.value = ModDup( "0" );
            s.type = MOD_SETTING_NUMBER;
        }
        return;
    }
    if ( meta->inSetting )
    {
        ModSetting& s = mod->settings[ mod->numSettings - 1 ];
        if ( strcasecmp( key, "Name" ) == 0 )
        {
            free( s.name );
            s.name = ModDup( value );
        }
        else if ( strcasecmp( key, "Default" ) == 0 )
        {
            free( s.value );
            s.value = ModDup( value );
        }
        else if ( strcasecmp( key, "Type" ) == 0 )
        {
            s.type = strcasecmp( value, "Boolean" ) == 0 ? MOD_SETTING_BOOLEAN
                   : ( strcasecmp( value, "Text" ) == 0 || strcasecmp( value, "String" ) == 0 ) ? MOD_SETTING_TEXT
                   : MOD_SETTING_NUMBER;
        }
        return;
    }
    if ( strcasecmp( section, "Miscellaneous" ) != 0 )
    {
        return;
    }
    if ( strcasecmp( key, "Title" ) == 0 )
    {
        snprintf( mod->title, sizeof( mod->title ), "%s", value );
    }
    else if ( strcasecmp( key, "InternalName" ) == 0 )
    {
        snprintf( mod->name, sizeof( mod->name ), "%s", value );
    }
    else if ( strcasecmp( key, "Main" ) == 0 )
    {
        meta->main = atoi( value ) != 0;
    }
    else if ( strcasecmp( key, "RequiredHack" ) == 0 )
    {
        if ( !IsSupportedHack( value ) )
        {
            Append( meta->unsupported, sizeof( meta->unsupported ), value );
        }
        Append( s_RequiredHacks, sizeof( s_RequiredHacks ), value );
    }
    else if ( strcasecmp( key, "RequiredMod" ) == 0 )
    {
        Append( meta->requiredMods, sizeof( meta->requiredMods ), value );
    }
}

void OnLimit( const char* section, const char* key, const char* value, void* )
{
    if ( key == NULL )
    {
        return;
    }
    char* end;
    bool blank = value[ 0 ] == '\0';
    double v = blank ? 0.0 : strtod( value, &end );
    if ( !blank && end == value )
    {
        return;
    }
    // several mods: the largest wins (the smallest for a minimum, [TreeNodes] BoundsMinimumY); a value beats a blank
    bool lowest = strcasestr( key, "Minimum" ) != NULL;
    for ( int i = 0; i < s_NumLimits; i++ )
    {
        Limit& l = s_Limits[ i ];
        if ( strcasecmp( l.section, section ) == 0 && strcasecmp( l.key, key ) == 0 )
        {
            if ( !blank && ( l.blank || ( lowest ? v < l.value : v > l.value ) ) )
            {
                l.value = v;
                l.blank = false;
            }
            return;
        }
    }
    Grow( s_Limits, s_NumLimits, s_CapLimits, 16 );
    Limit& l = s_Limits[ s_NumLimits++ ];
    l.section = ModDup( section );
    l.key = ModDup( key );
    l.value = v;
    l.blank = blank;
}

void AddRule( const char* path, const char* target, int modIndex )
{
    Grow( s_Rules, s_NumRules, s_CapRules, 16 );
    char key[ PATH_MAX ];
    ModNormalise( path, key, sizeof( key ) );
    Rule& r = s_Rules[ s_NumRules ];
    r.key = ModDup( key );
    r.target = target != NULL ? ModDup( target ) : NULL;
    r.order = modIndex * 1000000 + s_NumRules;
    s_NumRules++;
}

struct CustomFilesInfo
{
    Mod* mod;
    int capHandlers;
};

void OnCustomFiles( const char* section, const char* key, const char* value, void* user )
{
    CustomFilesInfo* info = (CustomFilesInfo*)user;
    if ( key == NULL )
    {
        return;
    }
    if ( strcasecmp( section, "PathRedirections" ) == 0 || strcasecmp( section, "AdditionalFiles" ) == 0 )
    {
        AddRule( key, value, info->mod->index );
    }
    else if ( strcasecmp( section, "Miscellaneous" ) == 0 && strcasecmp( key, "OccludedPath" ) == 0 )
    {
        AddRule( value, NULL, info->mod->index );
    }
    else if ( strcasecmp( section, "PathHandlers" ) == 0 )
    {
        Mod* mod = info->mod;
        Grow( mod->handlers, mod->numHandlers, info->capHandlers, 16 );
        char pattern[ PATH_MAX ], script[ PATH_MAX ];
        ModNormalise( key, pattern, sizeof( pattern ) );
        ModNormalise( value, script, sizeof( script ) );
        mod->handlers[ mod->numHandlers ].pattern = ModDup( pattern );
        mod->handlers[ mod->numHandlers ].script = ModDup( script );
        mod->numHandlers++;
    }
}

// ModSettings.ini in the game dir: [InternalName] Name=value overrides a mod's setting defaults
void OnUserSettings( const char* section, const char* key, const char* value, void* user )
{
    Mod* mod = (Mod*)user;
    if ( key == NULL || strcasecmp( section, mod->name ) != 0 )
    {
        return;
    }
    for ( int i = 0; i < mod->numSettings; i++ )
    {
        if ( strcasecmp( mod->settings[ i ].name, key ) == 0 )
        {
            free( mod->settings[ i ].value );
            mod->settings[ i ].value = ModDup( value );
            return;
        }
    }
    if ( strcasecmp( key, "Language" ) == 0 )
    {
        // not a Meta.ini setting: the Mod Launcher adds it for a mod with several CustomText files
        free( mod->language );
        mod->language = ModDup( value );
        return;
    }
    fprintf( stderr, "mods: ModSettings.ini: %s has no setting %s\n", mod->name, key );
}

// A main mod (Meta.ini Main=1) keeps its own saved games, as with the Mod Launcher: ModSaves/<InternalName>/ in the
// game dir. Only the first main mod counts.
void SetMainMod( const char* name )
{
    if ( s_SaveDir[ 0 ] != '\0' )
    {
        fprintf( stderr, "mods:   another main mod is loaded already; this one shares its saves\n" );
        return;
    }
    char safe[ 64 ];
    unsigned int n = 0;
    for ( const char* c = name; *c != '\0' && n + 1 < sizeof( safe ); c++ )
    {
        safe[ n++ ] = ( isalnum( (unsigned char)*c ) || *c == '-' || *c == '_' ) ? *c : '_';
    }
    safe[ n ] = '\0';
    if ( n == 0 )
    {
        return;
    }
    mkdir( "ModSaves", 0755 );
    snprintf( s_SaveDir, sizeof( s_SaveDir ), "ModSaves/%s", safe );
    mkdir( s_SaveDir, 0755 );
    fprintf( stderr, "mods:   main mod: saved games in %s/\n", s_SaveDir );
}

void LoadMod( const char* root )
{
    struct stat st;
    if ( stat( root, &st ) != 0 )
    {
        fprintf( stderr, "mods: %s not found\n", root );
        return;
    }
    g_Mods = (Mod*)realloc( g_Mods, ( g_NumMods + 1 ) * sizeof( Mod ) );
    Mod* mod = &g_Mods[ g_NumMods ];
    memset( mod, 0, sizeof( Mod ) );
    mod->index = g_NumMods;
    mod->root = ModDup( root );
    mod->fd = -1;
    mod->packaged = !S_ISDIR( st.st_mode );

    // Name until Meta.ini says otherwise: the folder or package name
    const char* base = strrchr( root, '/' ) != NULL ? strrchr( root, '/' ) + 1 : root;
    snprintf( mod->name, sizeof( mod->name ), "%s", base );
    char* dot = mod->packaged ? strrchr( mod->name, '.' ) : NULL;
    if ( dot != NULL )
    {
        *dot = '\0';
    }

    int files = 0;
    if ( mod->packaged )
    {
        const char* error = OpenPackage( mod );
        if ( error != NULL )
        {
            fprintf( stderr, "mods: %s: %s (expected a mod folder or a .lmlm package)\n", root, error );
            if ( mod->fd >= 0 )
            {
                close( mod->fd );
            }
            free( mod->root );
            return;
        }
        static const char PREFIX[] = "customfiles/";
        for ( int i = 0; i < mod->numEntries; i++ )
        {
            const ModPackageEntry& e = mod->entries[ i ];
            if ( strncmp( e.key, PREFIX, sizeof( PREFIX ) - 1 ) == 0 )
            {
                AddFile( e.rel + sizeof( PREFIX ) - 1, root, e.offset, (unsigned int)e.size, true, mod->index );
                files++;
            }
        }
    }
    else
    {
        char* customFiles = FindNoCase( root, "CustomFiles" );
        if ( customFiles != NULL )
        {
            files = ScanTree( customFiles, "", mod->index );
            free( customFiles );
        }
    }
    g_NumMods++;

    MetaInfo meta;
    memset( &meta, 0, sizeof( meta ) );
    meta.mod = mod;
    if ( !ReadModIni( mod, "Meta.ini", OnMeta, &meta ) )
    {
        fprintf( stderr, "mods: %s has no Meta.ini; loading its CustomFiles anyway\n", root );
    }
    ReadModIni( mod, "CustomLimits.ini", OnLimit, NULL );
    CustomFilesInfo cfInfo = { mod, 0 };
    ReadModIni( mod, "CustomFiles.ini", OnCustomFiles, &cfInfo );
    char* userSettings = ReadWholeFile( "ModSettings.ini", NULL );
    if ( userSettings != NULL )
    {
        ParseIni( userSettings, OnUserSettings, mod );
        free( userSettings );
    }

    fprintf( stderr, "mods: loaded \"%s\" (%s): %d files\n", mod->title[ 0 ] ? mod->title : mod->name, root, files );
    if ( meta.unsupported[ 0 ] )
    {
        fprintf( stderr, "mods:   not supported yet, ignored: %s\n", meta.unsupported );
    }
    if ( meta.requiredMods[ 0 ] )
    {
        fprintf( stderr, "mods:   also needs these mods: %s\n", meta.requiredMods );
    }
    if ( meta.main )
    {
        SetMainMod( mod->name );
    }
    bool hasScript;
    if ( mod->packaged )
    {
        hasScript = FindEntry( mod, "customfiles.lua" ) != NULL;
    }
    else
    {
        char* script = FindNoCase( root, "CustomFiles.lua" );
        hasScript = script != NULL;
        free( script );
    }
    mod->hasLua = hasScript || mod->numHandlers > 0;
}

int CompareRules( const void* a, const void* b )
{
    const Rule* ra = (const Rule*)a;
    const Rule* rb = (const Rule*)b;
    int c = strcmp( ra->key, rb->key );
    return c != 0 ? c : ( ra->order < rb->order ? -1 : ra->order > rb->order );
}

int CompareFiles( const void* a, const void* b )
{
    const ModFile* fa = (const ModFile*)a;
    const ModFile* fb = (const ModFile*)b;
    int c = strcmp( fa->key, fb->key );
    return c != 0 ? c : ( fa->order < fb->order ? -1 : fa->order > fb->order );
}

// radMakeCaseInsensitiveKey32 (radkey.hpp) of a normalised path with '\\' separators
uint32_t KeyOf( const char* key )
{
    uint32_t value = 0;
    for ( const char* p = key; *p != '\0'; p++ )
    {
        char ch = *p == '/' ? '\\' : *p;
        uint32_t c = (uint32_t)(int)( ch < 'a' ? ch + 'a' - 'A' : ch );
        value = ( value << 5 ) - value + c;
    }
    return value;
}

int CompareKeyEntries( const void* a, const void* b )
{
    const KeyEntry* ka = (const KeyEntry*)a;
    const KeyEntry* kb = (const KeyEntry*)b;
    if ( ka->key != kb->key )
    {
        return ka->key < kb->key ? -1 : 1;
    }
    if ( ka->priority != kb->priority )
    {
        return ka->priority - kb->priority;
    }
    return ka->index - kb->index;
}

// Sorted, first entry per key kept
int BuildKeyTable( KeyEntry* table, int count )
{
    qsort( table, count, sizeof( KeyEntry ), CompareKeyEntries );
    int out = 0;
    for ( int i = 0; i < count; i++ )
    {
        if ( out == 0 || table[ out - 1 ].key != table[ i ].key )
        {
            table[ out++ ] = table[ i ];
        }
    }
    return out;
}

void Init( void )
{
    if ( getcwd( s_GameRoot, sizeof( s_GameRoot ) ) == NULL )
    {
        strcpy( s_GameRoot, "." );
    }
    const char* log = getenv( "SHAR_MOD_LOG" );
    s_Log = log != NULL && log[ 0 ] != '\0' && log[ 0 ] != '0';
    const char* env = getenv( "SHAR_MODS" );
    if ( env == NULL || env[ 0 ] == '\0' )
    {
        return;
    }
    char* list = ModDup( env );
    for ( char* save = NULL, *tok = strtok_r( list, ":", &save ); tok != NULL; tok = strtok_r( NULL, ":", &save ) )
    {
        LoadMod( tok );
    }
    free( list );

    // Sort by key, keeping only the first mod's copy of each file / rule
    qsort( s_Files, s_NumFiles, sizeof( ModFile ), CompareFiles );
    int out = 0;
    for ( int i = 0; i < s_NumFiles; i++ )
    {
        if ( out > 0 && strcmp( s_Files[ out - 1 ].key, s_Files[ i ].key ) == 0 )
        {
            free( s_Files[ i ].key );
            free( s_Files[ i ].path );
            continue;
        }
        s_Files[ out++ ] = s_Files[ i ];
    }
    s_NumFiles = out;

    qsort( s_Rules, s_NumRules, sizeof( Rule ), CompareRules );
    out = 0;
    for ( int i = 0; i < s_NumRules; i++ )
    {
        if ( out > 0 && strcmp( s_Rules[ out - 1 ].key, s_Rules[ i ].key ) == 0 )
        {
            continue;
        }
        s_Rules[ out++ ] = s_Rules[ i ];
    }
    s_NumRules = out;

    // Key tables (files sorted by key first, so earlier mods already won; index order breaks key collisions)
    s_FileKeys = (KeyEntry*)malloc( ( 2 * s_NumFiles + 1 ) * sizeof( KeyEntry ) );
    s_NumFileKeys = 0;
    for ( int i = 0; i < s_NumFiles; i++ )
    {
        const char* key = s_Files[ i ].key;
        KeyEntry e = { KeyOf( key ), i, 0 };
        s_FileKeys[ s_NumFileKeys++ ] = e;
        size_t len = strlen( key );
        if ( len > 4 && strcmp( key + len - 4, ".ogg" ) == 0 && len < PATH_MAX )
        {
            char rsd[ PATH_MAX ];
            memcpy( rsd, key, len - 4 );
            strcpy( rsd + len - 4, ".rsd" );
            KeyEntry o = { KeyOf( rsd ), i, 1 };
            s_FileKeys[ s_NumFileKeys++ ] = o;
        }
    }
    s_NumFileKeys = BuildKeyTable( s_FileKeys, s_NumFileKeys );
    s_RuleKeys = (KeyEntry*)malloc( ( s_NumRules + 1 ) * sizeof( KeyEntry ) );
    for ( int i = 0; i < s_NumRules; i++ )
    {
        KeyEntry e = { KeyOf( s_Rules[ i ].key ), i, 0 };
        s_RuleKeys[ i ] = e;
    }
    s_NumRuleKeys = BuildKeyTable( s_RuleKeys, s_NumRules );

    for ( int i = 0; i < s_NumLimits; i++ )
    {
        if ( s_Limits[ i ].blank )
        {
            fprintf( stderr, "mods: limit [%s] %s= (blank)\n", s_Limits[ i ].section, s_Limits[ i ].key );
        }
        else
        {
            fprintf( stderr, "mods: limit [%s] %s=%g\n", s_Limits[ i ].section, s_Limits[ i ].key, s_Limits[ i ].value );
        }
    }

    // Lua last: CustomFiles.lua may read any mod's files
    for ( int i = 0; i < g_NumMods; i++ )
    {
        if ( g_Mods[ i ].hasLua )
        {
            ModLuaStart( &g_Mods[ i ] );
        }
    }
}

//=============================================================================
// Lookups
//=============================================================================

int CompareKey( const void* key, const void* elem )
{
    return strcmp( (const char*)key, ( (const ModFile*)elem )->key );
}

int CompareRuleKey( const void* key, const void* elem )
{
    return strcmp( (const char*)key, ( (const Rule*)elem )->key );
}

// A key string ("-1913354909"): decimal digits, optionally negative. Its value in *key.
bool ParseKeyString( const char* s, uint32_t* key )
{
    const char* p = s[ 0 ] == '-' ? s + 1 : s;
    if ( *p == '\0' || strlen( p ) > 10 )
    {
        return false;
    }
    for ( const char* c = p; *c != '\0'; c++ )
    {
        if ( !isdigit( (unsigned char)*c ) )
        {
            return false;
        }
    }
    *key = (uint32_t)atoi( s );         // StringKeyToKey32 (cementer.cpp)
    return true;
}

int FindKey( const KeyEntry* table, int count, uint32_t key )
{
    int low = 0, high = count - 1;
    while ( low <= high )
    {
        int mid = ( low + high ) / 2;
        if ( table[ mid ].key == key )
        {
            return table[ mid ].index;
        }
        if ( table[ mid ].key < key )
        {
            low = mid + 1;
        }
        else
        {
            high = mid - 1;
        }
    }
    return -1;
}

const Rule* FindRule( const char* gamePath, const char* key )
{
    if ( s_NumRules == 0 )
    {
        return NULL;
    }
    uint32_t k;
    if ( ParseKeyString( gamePath, &k ) )
    {
        int i = FindKey( s_RuleKeys, s_NumRuleKeys, k );
        return i >= 0 ? &s_Rules[ i ] : NULL;
    }
    return (const Rule*)bsearch( key, s_Rules, s_NumRules, sizeof( Rule ), CompareRuleKey );
}

// A mod's CustomFiles copy of a game path (key: normalised gamePath)
const ModFile* FindFile( const char* gamePath, const char* key )
{
    if ( s_NumFiles == 0 )
    {
        return NULL;
    }
    uint32_t k;
    if ( ParseKeyString( gamePath, &k ) )
    {
        int i = FindKey( s_FileKeys, s_NumFileKeys, k );
        return i >= 0 ? &s_Files[ i ] : NULL;
    }
    const ModFile* f = (const ModFile*)bsearch( key, s_Files, s_NumFiles, sizeof( ModFile ), CompareKey );
    size_t len = strlen( key );
    if ( f == NULL && len > 4 && len < PATH_MAX && strcmp( key + len - 4, ".rsd" ) == 0 )
    {
        // OggVorbisSupport: a mod's .ogg stands in for the .rsd (decoded in radsound's rsdfiledatasource.cpp)
        char ogg[ PATH_MAX ];
        memcpy( ogg, key, len - 4 );
        strcpy( ogg + len - 4, ".ogg" );
        f = (const ModFile*)bsearch( ogg, s_Files, s_NumFiles, sizeof( ModFile ), CompareKey );
    }
    return f;
}

void FileOf( const ModFile* f, SharModFile* file )
{
    file->path = f->path;
    file->offset = f->offset;
    file->size = f->size;
    file->packaged = f->packaged;
    file->fd = -1;
}

//=============================================================================
// Virtual file system
//=============================================================================

enum Mount { MOUNT_GAMEDATA, MOUNT_GAMEDIR, MOUNT_MOD };

// Splits a virtual path: mount, the mod for /Mods/<name>, and the rest (normalised) in rel
bool ParseVPath( const char* vpath, Mount* mount, Mod** mod, char* rel, size_t size )
{
    char key[ PATH_MAX ];
    ModNormalise( vpath, key, sizeof( key ) );
    const char* rest = key;
    *mod = NULL;
    if ( strncmp( key, "gamedata", 8 ) == 0 && ( key[ 8 ] == '/' || key[ 8 ] == '\0' ) )
    {
        *mount = MOUNT_GAMEDATA;
        rest = key + 8;
    }
    else if ( strncmp( key, "gamedir", 7 ) == 0 && ( key[ 7 ] == '/' || key[ 7 ] == '\0' ) )
    {
        *mount = MOUNT_GAMEDIR;
        rest = key + 7;
    }
    else if ( strncmp( key, "mods/", 5 ) == 0 )
    {
        *mount = MOUNT_MOD;
        const char* name = key + 5;
        const char* end = strchr( name, '/' );
        size_t n = end != NULL ? (size_t)( end - name ) : strlen( name );
        for ( int i = 0; i < g_NumMods; i++ )
        {
            if ( strlen( g_Mods[ i ].name ) == n && strncasecmp( g_Mods[ i ].name, name, n ) == 0 )
            {
                *mod = &g_Mods[ i ];
            }
        }
        if ( *mod == NULL )
        {
            return false;
        }
        rest = name + n;
    }
    else
    {
        *mount = MOUNT_GAMEDATA;
    }
    if ( *rest == '/' )
    {
        rest++;
    }
    snprintf( rel, size, "%s", rest );
    return true;
}

} // namespace

char* ModDup( const char* s )
{
    size_t n = strlen( s ) + 1;
    char* d = (char*)malloc( n );
    memcpy( d, s, n );
    return d;
}

// Game path -> key: drop a drive letter, '\' -> '/', lower case, resolve "." / "..", no leading or doubled '/'.
void ModNormalise( const char* in, char* out, size_t size )
{
    if ( in[ 0 ] != '\0' && in[ 1 ] == ':' )
    {
        in += 2;
    }
    size_t len = 0;
    out[ 0 ] = '\0';
    while ( *in != '\0' )
    {
        while ( *in == '/' || *in == '\\' )
        {
            in++;
        }
        const char* start = in;
        while ( *in != '\0' && *in != '/' && *in != '\\' )
        {
            in++;
        }
        size_t n = (size_t)( in - start );
        if ( n == 0 || ( n == 1 && start[ 0 ] == '.' ) )
        {
            continue;
        }
        if ( n == 2 && start[ 0 ] == '.' && start[ 1 ] == '.' )
        {
            while ( len > 0 && out[ len - 1 ] != '/' )
            {
                len--;
            }
            if ( len > 0 )
            {
                len--;
            }
            out[ len ] = '\0';
            continue;
        }
        if ( len + ( len ? 1 : 0 ) + n + 1 > size )
        {
            break;
        }
        if ( len )
        {
            out[ len++ ] = '/';
        }
        for ( size_t i = 0; i < n; i++ )
        {
            out[ len++ ] = (char)tolower( (unsigned char)start[ i ] );
        }
        out[ len ] = '\0';
    }
}

bool ModWildcardMatch( const char* pattern, const char* key )
{
    // Iterative glob: '*' any run (also across '/'), '?' any one character
    const char* star = NULL;
    const char* resume = NULL;
    while ( *key != '\0' )
    {
        if ( *pattern == '*' )
        {
            star = pattern++;
            resume = key;
        }
        else if ( *pattern == '?' || *pattern == *key )
        {
            pattern++;
            key++;
        }
        else if ( star != NULL )
        {
            pattern = star + 1;
            key = ++resume;
        }
        else
        {
            return false;
        }
    }
    while ( *pattern == '*' )
    {
        pattern++;
    }
    return *pattern == '\0';
}

bool ModHackLoaded( const char* hack )
{
    return IsSupportedHack( hack );
}

bool ModLogEnabled( void )
{
    return s_Log;
}

int ModVfsLocate( const char* vpath, SharModFile* file, char* gamePath, size_t gamePathSize )
{
    Mount mount;
    Mod* mod;
    char rel[ PATH_MAX ];
    if ( !ParseVPath( vpath, &mount, &mod, rel, sizeof( rel ) ) )
    {
        return 0;
    }
    file->fd = -1;
    file->offset = 0;
    file->packaged = 0;
    static __thread char s_RealPath[ PATH_MAX ];
    if ( mount == MOUNT_MOD )
    {
        if ( mod->packaged )
        {
            const ModPackageEntry* e = FindEntry( mod, rel );
            if ( e == NULL )
            {
                return 0;
            }
            file->path = mod->root;
            file->offset = e->offset;
            file->size = (unsigned int)e->size;
            file->packaged = 1;
            return 1;
        }
        if ( !ResolveNoCase( mod->root, rel, s_RealPath, sizeof( s_RealPath ) ) )
        {
            return 0;
        }
    }
    else
    {
        if ( mount == MOUNT_GAMEDATA )
        {
            const ModFile* f = FindFile( rel, rel );
            if ( f != NULL )
            {
                FileOf( f, file );
                return 1;
            }
        }
        if ( !ResolveNoCase( s_GameRoot, rel, s_RealPath, sizeof( s_RealPath ) ) )
        {
            if ( mount == MOUNT_GAMEDATA )
            {
                snprintf( gamePath, gamePathSize, "%s", rel );
                return 2;
            }
            return 0;
        }
    }
    struct stat st;
    if ( stat( s_RealPath, &st ) != 0 || !S_ISREG( st.st_mode ) )
    {
        return 0;
    }
    file->path = s_RealPath;
    file->size = (unsigned int)st.st_size;
    return 1;
}

bool ModVfsRead( const char* vpath, char** data, size_t* size )
{
    SharModFile file;
    char gamePath[ PATH_MAX ];
    if ( ModVfsLocate( vpath, &file, gamePath, sizeof( gamePath ) ) != 1 )
    {
        return false;
    }
    int fd = open( file.path, O_RDONLY | O_CLOEXEC );
    if ( fd < 0 )
    {
        return false;
    }
    *data = (char*)malloc( file.size + 1 );
    size_t done = 0;
    while ( done < file.size )
    {
        ssize_t n = pread( fd, *data + done, file.size - done, (off_t)( file.offset + done ) );
        if ( n <= 0 )
        {
            break;
        }
        done += (size_t)n;
    }
    close( fd );
    ( *data )[ done ] = '\0';
    *size = done;
    return done == file.size;
}

namespace {

struct NameList
{
    char** names;
    bool* dirs;
    int count, cap;
};

void AddName( NameList& l, const char* name, size_t n, bool dir )
{
    for ( int i = 0; i < l.count; i++ )
    {
        if ( strlen( l.names[ i ] ) == n && strncasecmp( l.names[ i ], name, n ) == 0 )
        {
            return;
        }
    }
    if ( l.count == l.cap )
    {
        l.cap = l.cap ? l.cap * 2 : 32;
        l.names = (char**)realloc( l.names, l.cap * sizeof( char* ) );
        l.dirs = (bool*)realloc( l.dirs, l.cap * sizeof( bool ) );
    }
    l.names[ l.count ] = (char*)malloc( n + 1 );
    memcpy( l.names[ l.count ], name, n );
    l.names[ l.count ][ n ] = '\0';
    l.dirs[ l.count ] = dir;
    l.count++;
}

// Children of dir (normalised; "" is the root) among paths: each path's next component
void AddChildren( NameList& l, const char* dir, const char* path, const char* original )
{
    size_t n = strlen( dir );
    if ( n > 0 && ( strncmp( path, dir, n ) != 0 || path[ n ] != '/' ) )
    {
        return;
    }
    size_t at = n > 0 ? n + 1 : 0;
    const char* child = original + at;
    const char* slash = strchr( child, '/' );
    AddName( l, child, slash != NULL ? (size_t)( slash - child ) : strlen( child ), slash != NULL );
}

void AddDirectory( NameList& l, const char* realDir )
{
    DIR* d = opendir( realDir );
    if ( d == NULL )
    {
        return;
    }
    struct dirent* e;
    while ( ( e = readdir( d ) ) != NULL )
    {
        if ( strcmp( e->d_name, "." ) == 0 || strcmp( e->d_name, ".." ) == 0 )
        {
            continue;
        }
        char path[ PATH_MAX ];
        snprintf( path, sizeof( path ), "%s/%s", realDir, e->d_name );
        struct stat st;
        AddName( l, e->d_name, strlen( e->d_name ), stat( path, &st ) == 0 && S_ISDIR( st.st_mode ) );
    }
    closedir( d );
}

bool ListVPath( const char* vpath, NameList& l )
{
    Mount mount;
    Mod* mod;
    char rel[ PATH_MAX ];
    if ( !ParseVPath( vpath, &mount, &mod, rel, sizeof( rel ) ) )
    {
        return false;
    }
    char real[ PATH_MAX ];
    if ( mount == MOUNT_MOD )
    {
        if ( mod->packaged )
        {
            for ( int i = 0; i < mod->numEntries; i++ )
            {
                AddChildren( l, rel, mod->entries[ i ].key, mod->entries[ i ].rel );
            }
        }
        else if ( ResolveNoCase( mod->root, rel, real, sizeof( real ) ) )
        {
            AddDirectory( l, real );
        }
        return true;
    }
    if ( mount == MOUNT_GAMEDATA )
    {
        for ( int i = 0; i < s_NumFiles; i++ )
        {
            AddChildren( l, rel, s_Files[ i ].key, s_Files[ i ].key );
        }
    }
    if ( ResolveNoCase( s_GameRoot, rel, real, sizeof( real ) ) )
    {
        AddDirectory( l, real );
    }
    return true;
}

void FreeNames( NameList& l )
{
    for ( int i = 0; i < l.count; i++ )
    {
        free( l.names[ i ] );
    }
    free( l.names );
    free( l.dirs );
}

} // namespace

bool ModVfsList( const char* vpath, ModVfsEntryCallback cb, void* user, bool* completed )
{
    NameList l;
    memset( &l, 0, sizeof( l ) );
    if ( !ListVPath( vpath, l ) )
    {
        return false;
    }
    *completed = true;
    // Copy out first: the callback may list other directories (recursion in Lua)
    for ( int i = 0; i < l.count; i++ )
    {
        if ( !cb( l.names[ i ], l.dirs[ i ], user ) )
        {
            *completed = false;
            break;
        }
    }
    FreeNames( l );
    return true;
}

bool ModVfsExists( const char* vpath, bool* isDir )
{
    SharModFile file;
    char gamePath[ PATH_MAX ];
    if ( ModVfsLocate( vpath, &file, gamePath, sizeof( gamePath ) ) == 1 )
    {
        *isDir = false;
        return true;
    }
    // A directory: anything listed under it
    NameList l;
    memset( &l, 0, sizeof( l ) );
    bool found = ListVPath( vpath, l ) && l.count > 0;
    FreeNames( l );
    *isDir = found;
    return found;
}

//=============================================================================
// Opens
//=============================================================================

namespace {

// The last resolution on this thread, for the drive (shar_ModTakeResolved)
__thread char t_StashPath[ PATH_MAX ];
__thread SharModFile t_StashFile;
__thread bool t_Stashed = false;
__thread char t_Redirect[ PATH_MAX ];

void ClearStash( void )
{
    if ( t_Stashed && t_StashFile.fd >= 0 )
    {
        close( t_StashFile.fd );
    }
    t_Stashed = false;
}

void LogFile( const char* gamePath, const SharModFile* file, const char* how )
{
    if ( !s_Log )
    {
        return;
    }
    if ( file->fd >= 0 )
    {
        fprintf( stderr, "mods: %s -> %s (%u bytes)\n", gamePath, how, file->size );
    }
    else if ( file->packaged )
    {
        fprintf( stderr, "mods: %s -> %s%s @%llu (%u bytes)\n", gamePath, how, file->path, file->offset, file->size );
    }
    else
    {
        fprintf( stderr, "mods: %s -> %s%s\n", gamePath, how, file->path );
    }
}

int Resolve( const char* gamePath, SharModFile* file, const char** redirect, int depth )
{
    char key[ PATH_MAX ];
    ModNormalise( gamePath, key, sizeof( key ) );
    uint32_t k;
    bool keyString = ParseKeyString( gamePath, &k );

    // Lua path handlers (by name only)
    for ( int m = 0; m < g_NumMods && !keyString; m++ )
    {
        Mod* mod = &g_Mods[ m ];
        for ( int h = 0; h < mod->numHandlers; h++ )
        {
            if ( !ModWildcardMatch( mod->handlers[ h ].pattern, key ) )
            {
                continue;
            }
            int fd;
            unsigned int size;
            char target[ PATH_MAX ];
            int r = ModLuaRunHandler( mod, mod->handlers[ h ].script, gamePath, &fd, &size, target, sizeof( target ) );
            if ( r == 1 )
            {
                file->path = NULL;
                file->offset = 0;
                file->size = size;
                file->packaged = 0;
                file->fd = fd;
                LogFile( gamePath, file, "Lua output, " );
                return SHAR_MOD_FILE;
            }
            if ( r == 2 )
            {
                int found = ModVfsLocate( target, file, t_Redirect, sizeof( t_Redirect ) );
                if ( found == 1 )
                {
                    LogFile( gamePath, file, "Lua redirect, " );
                    return SHAR_MOD_FILE;
                }
                if ( found == 2 )
                {
                    if ( s_Log )
                    {
                        fprintf( stderr, "mods: %s -> Lua redirect to game file %s\n", gamePath, t_Redirect );
                    }
                    *redirect = t_Redirect;
                    return SHAR_MOD_REDIRECT;
                }
                fprintf( stderr, "mods: %s: Lua redirect to %s, which does not exist\n", gamePath, target );
            }
            break;          // first matching handler of a mod only
        }
    }

    // CustomFiles.ini rules
    const Rule* rule = FindRule( gamePath, key );
    if ( rule != NULL )
    {
        if ( rule->target == NULL )
        {
            if ( s_Log )
            {
                fprintf( stderr, "mods: %s occluded\n", gamePath );
            }
            return SHAR_MOD_MISSING;
        }
        if ( depth < 8 )
        {
            if ( s_Log )
            {
                fprintf( stderr, "mods: %s redirected to %s\n", gamePath, rule->target );
            }
            int r = Resolve( rule->target, file, redirect, depth + 1 );
            if ( r != SHAR_MOD_NONE )
            {
                return r;
            }
            snprintf( t_Redirect, sizeof( t_Redirect ), "%s", rule->target );
            *redirect = t_Redirect;
            return SHAR_MOD_REDIRECT;
        }
    }

    // CustomFiles
    const ModFile* f = FindFile( gamePath, key );
    if ( f != NULL )
    {
        FileOf( f, file );
        LogFile( gamePath, file, "" );
        return SHAR_MOD_FILE;
    }
    return SHAR_MOD_NONE;
}

} // namespace

extern "C" int shar_ModResolve( const char* gamePath, SharModFile* file, const char** redirect )
{
    pthread_once( &s_Once, Init );
    ClearStash( );
    if ( g_NumMods == 0 )
    {
        return SHAR_MOD_NONE;
    }
    int r = Resolve( gamePath, file, redirect, 0 );
    if ( r == SHAR_MOD_FILE )
    {
        snprintf( t_StashPath, sizeof( t_StashPath ), "%s", gamePath );
        t_StashFile = *file;
        t_Stashed = true;
    }
    return r;
}

extern "C" int shar_ModTakeResolved( const char* gamePath, SharModFile* file, const char** redirect )
{
    if ( t_Stashed && strcmp( t_StashPath, gamePath ) == 0 )
    {
        *file = t_StashFile;
        t_Stashed = false;          // the caller owns any fd now
        return SHAR_MOD_FILE;
    }
    int r = shar_ModResolve( gamePath, file, redirect );
    t_Stashed = false;
    return r;
}

extern "C" const char* shar_ModSaveDir( void )
{
    pthread_once( &s_Once, Init );
    return s_SaveDir[ 0 ] != '\0' ? s_SaveDir : NULL;
}

static const Limit* FindLimit( const char* section, const char* key )
{
    pthread_once( &s_Once, Init );
    for ( int i = 0; i < s_NumLimits; i++ )
    {
        if ( strcasecmp( s_Limits[ i ].section, section ) == 0 && strcasecmp( s_Limits[ i ].key, key ) == 0 )
        {
            return &s_Limits[ i ];
        }
    }
    return NULL;
}

extern "C" int shar_ModLimit( const char* section, const char* key, int defaultValue )
{
    const Limit* l = FindLimit( section, key );
    return l != NULL && !l->blank ? (int)l->value : defaultValue;
}

extern "C" float shar_ModLimitFloat( const char* section, const char* key, float defaultValue )
{
    const Limit* l = FindLimit( section, key );
    return l != NULL && !l->blank ? (float)l->value : defaultValue;
}

extern "C" int shar_ModLimitBlank( const char* section, const char* key )
{
    const Limit* l = FindLimit( section, key );
    return l != NULL && l->blank;
}

// The first mod's config file name, as shar_ModConfig; *from gets that mod.
static char* ConfigText( const char* name, const Mod** from )
{
    pthread_once( &s_Once, Init );
    char key[ PATH_MAX ];
    ModNormalise( name, key, sizeof( key ) );
    for ( int i = 0; i < g_NumMods; i++ )
    {
        char* text = ReadModFile( &g_Mods[ i ], key, NULL );
        if ( text != NULL )
        {
            if ( s_Log )
            {
                fprintf( stderr, "mods: %s: %s\n", g_Mods[ i ].name, name );
            }
            *from = &g_Mods[ i ];
            return text;
        }
    }
    return NULL;
}

extern "C" char* shar_ModConfig( const char* name )
{
    const Mod* from;
    return ConfigText( name, &from );
}

extern "C" int shar_ModConfigIni( const char* file, SharModIniCallback cb, void* user, const char** modName )
{
    const Mod* from = NULL;
    char* text = ConfigText( file, &from );
    if ( text == NULL )
    {
        return 0;
    }
    if ( modName != NULL )
    {
        *modName = from->name;
    }
    ParseIni( text, cb, user );
    free( text );
    return 1;
}

extern "C" int shar_ModSetting( const char* modName, const char* setting, char* out, size_t size )
{
    pthread_once( &s_Once, Init );
    for ( int i = 0; i < g_NumMods; i++ )
    {
        const Mod* mod = &g_Mods[ i ];
        if ( strcasecmp( mod->name, modName ) != 0 )
        {
            continue;
        }
        for ( int s = 0; s < mod->numSettings; s++ )
        {
            if ( strcasecmp( mod->settings[ s ].name, setting ) == 0 )
            {
                snprintf( out, size, "%s", mod->settings[ s ].value ? mod->settings[ s ].value : "" );
                return 1;
            }
        }
        return 0;
    }
    return 0;
}

namespace
{
struct IniLookup
{
    const char* section;
    const char* key;
    char* out;
    size_t size;
    bool found;
};

void OnIniLookup( const char* section, const char* key, const char* value, void* user )
{
    IniLookup* l = (IniLookup*)user;
    if ( key != NULL && strcasecmp( section, l->section ) == 0 && strcasecmp( key, l->key ) == 0 )
    {
        snprintf( l->out, l->size, "%s", value );
        l->found = true;
    }
}
}

extern "C" int shar_ModConfigValue( const char* file, const char* section, const char* key, char* out, size_t size )
{
    char* text = shar_ModConfig( file );
    if ( text == NULL )
    {
        return 0;
    }
    IniLookup lookup = { section, key, out, size, false };
    ParseIni( text, OnIniLookup, &lookup );
    free( text );
    return lookup.found;
}

extern "C" int shar_ModConfigInt( const char* file, const char* section, const char* key, int defaultValue )
{
    char value[ 64 ];
    if ( !shar_ModConfigValue( file, section, key, value, sizeof( value ) ) || value[ 0 ] == '\0' )
    {
        return defaultValue;
    }
    return atoi( value );
}

extern "C" int shar_ModBugFix( const char* section, const char* key, int defaultValue )
{
    if ( !shar_ModHackRequired( "BugFixes" ) )
    {
        return 0;
    }
    return shar_ModConfigInt( "BugFixes.ini", section, key, defaultValue ) != 0;
}

static const Mod* s_FolderMod = NULL;   // the mod the last shar_ModConfigFolder call read from

extern "C" int shar_ModConfigFolder( const char* folder, char** texts, size_t* sizes, char** names, int max )
{
    return shar_ModConfigFolderNth( folder, 0, texts, sizes, names, max, NULL );
}

extern "C" int shar_ModConfigFolderNth( const char* folder, int nth, char** texts, size_t* sizes, char** names, int max,
                                        const char** modName )
{
    pthread_once( &s_Once, Init );
    char key[ PATH_MAX ];
    ModNormalise( folder, key, sizeof( key ) );
    size_t keyLength = strlen( key );
    for ( int i = 0; i < g_NumMods; i++ )
    {
        const Mod* mod = &g_Mods[ i ];
        int count = 0;
        if ( mod->packaged )
        {
            for ( int e = 0; e < mod->numEntries && count < max; e++ )
            {
                const char* rel = mod->entries[ e ].key;
                if ( strncmp( rel, key, keyLength ) == 0 && rel[ keyLength ] == '/' &&
                     strchr( rel + keyLength + 1, '/' ) == NULL )
                {
                    char* text = ReadModFile( mod, rel, &sizes[ count ] );
                    if ( text != NULL )
                    {
                        texts[ count ] = text;
                        names[ count ] = strdup( mod->entries[ e ].rel + keyLength + 1 );
                        count++;
                    }
                }
            }
        }
        else
        {
            char path[ PATH_MAX ];
            if ( ResolveNoCase( mod->root, key, path, sizeof( path ) ) )
            {
                DIR* d = opendir( path );
                struct dirent* de;
                while ( d != NULL && ( de = readdir( d ) ) != NULL && count < max )
                {
                    char file[ PATH_MAX ];
                    struct stat st;
                    snprintf( file, sizeof( file ), "%s/%s", path, de->d_name );
                    if ( de->d_name[ 0 ] == '.' || stat( file, &st ) != 0 || !S_ISREG( st.st_mode ) )
                    {
                        continue;
                    }
                    char* text = ReadWholeFile( file, &sizes[ count ] );
                    if ( text != NULL )
                    {
                        texts[ count ] = text;
                        names[ count ] = strdup( de->d_name );
                        count++;
                    }
                }
                if ( d != NULL )
                {
                    closedir( d );
                }
            }
        }
        if ( count > 0 && nth-- > 0 )
        {
            for ( int f = 0; f < count; f++ )
            {
                free( texts[ f ] );
                free( names[ f ] );
            }
            count = 0;
        }
        if ( count > 0 )
        {
            s_FolderMod = mod;
            if ( modName != NULL )
            {
                *modName = mod->name;
            }
            return count;
        }
    }
    return 0;
}

extern "C" const char* shar_ModConfigFolderLanguage( const char* folder )
{
    // the mod shar_ModConfigFolder takes the folder from
    char* texts[ 64 ];
    size_t sizes[ 64 ];
    char* names[ 64 ];
    s_FolderMod = NULL;
    int count = shar_ModConfigFolder( folder, texts, sizes, names, 64 );
    for ( int i = 0; i < count; i++ )
    {
        free( texts[ i ] );
        free( names[ i ] );
    }
    return s_FolderMod != NULL ? s_FolderMod->language : NULL;
}

int shar_ModHackRequired( const char* hack )
{
    size_t len = strlen( hack );
    for ( const char* p = s_RequiredHacks; *p != '\0'; )
    {
        const char* end = strchr( p, ',' );
        size_t n = end ? (size_t)( end - p ) : strlen( p );
        if ( n == len && strncasecmp( p, hack, len ) == 0 )
        {
            return 1;
        }
        if ( end == NULL )
        {
            break;
        }
        p = end + 2;
    }
    return 0;
}
