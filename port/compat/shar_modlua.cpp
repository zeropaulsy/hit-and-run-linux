// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Lua for mods (the Mod Launcher's LuaSupport / CustomFiles Lua): one Lua 5.4 state per mod, its CustomFiles.lua
// run at load, path handlers (CustomFiles.ini [PathHandlers]) run in the same state when the game opens a
// matching file. The launcher's API subset mods use is below. Sandboxed: no io, package, debug or os other than
// the clock, no binary chunks; dofile / loadfile read through the virtual file system (shar_mods.cpp).
// Lua is C: its errors longjmp through the functions here, so they hold nothing that needs a destructor.
#include "shar_mods_internal.h"
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace {

pthread_mutex_t s_Lock = PTHREAD_MUTEX_INITIALIZER;

// The path handler running now (under s_Lock)
struct HandlerContext
{
    bool active;
    const char* gamePath;
    char* output;
    size_t outputSize, outputCap;
    bool hasOutput;
    char redirect[ PATH_MAX ];
};
HandlerContext s_Handler;

Mod* ModOf( lua_State* L )
{
    return (Mod*)lua_touserdata( L, lua_upvalueindex( 1 ) );
}

HandlerContext* RequireHandler( lua_State* L, const char* function )
{
    if ( !s_Handler.active )
    {
        luaL_error( L, "%s can only be called in a path handler script", function );
    }
    return &s_Handler;
}

// Runs the chunk at vpath (a virtual path) with the arguments above it; returns its results
int LoadVFile( lua_State* L, const char* vpath )
{
    char* data;
    size_t size;
    if ( !ModVfsRead( vpath, &data, &size ) )
    {
        lua_pushnil( L );
        lua_pushfstring( L, "cannot read %s", vpath );
        return 2;
    }
    char name[ PATH_MAX + 1 ];
    snprintf( name, sizeof( name ), "@%s", vpath );
    int status = luaL_loadbufferx( L, data, size, name, "t" );
    free( data );
    if ( status != LUA_OK )
    {
        lua_pushnil( L );
        lua_insert( L, -2 );
        return 2;
    }
    return 1;
}

//=============================================================================
// Base library replacements
//=============================================================================

int L_print( lua_State* L )
{
    int n = lua_gettop( L );
    fprintf( stderr, "[%s]", ModOf( L )->name );
    for ( int i = 1; i <= n; i++ )
    {
        fprintf( stderr, "%s%s", i == 1 ? " " : "\t", luaL_tolstring( L, i, NULL ) );
        lua_pop( L, 1 );
    }
    fprintf( stderr, "\n" );
    return 0;
}

int L_loadfile( lua_State* L )
{
    return LoadVFile( L, luaL_checkstring( L, 1 ) );
}

int L_dofile( lua_State* L )
{
    const char* vpath = luaL_checkstring( L, 1 );
    lua_settop( L, 1 );
    if ( LoadVFile( L, vpath ) != 1 )
    {
        return lua_error( L );
    }
    lua_call( L, 0, LUA_MULTRET );
    return lua_gettop( L ) - 1;
}

// load( chunk [, chunkname [, mode [, env]]] ) without binary chunks
int L_load( lua_State* L )
{
    lua_settop( L, 4 );
    lua_pushliteral( L, "t" );
    lua_replace( L, 3 );
    lua_pushvalue( L, lua_upvalueindex( 2 ) );      // the original load
    lua_insert( L, 1 );
    lua_call( L, 4, LUA_MULTRET );
    return lua_gettop( L );
}

//=============================================================================
// Path handler functions
//=============================================================================

int L_GetPath( lua_State* L )
{
    HandlerContext* h = RequireHandler( L, "GetPath" );
    // As the game asked for it, with backslashes and no drive letter ("scripts\missions\level01\m1l.mfk")
    const char* p = h->gamePath;
    if ( p[ 0 ] != '\0' && p[ 1 ] == ':' )
    {
        p += 2;
    }
    while ( *p == '\\' || *p == '/' )
    {
        p++;
    }
    luaL_Buffer b;
    luaL_buffinit( L, &b );
    for ( ; *p != '\0'; p++ )
    {
        luaL_addchar( &b, *p == '/' ? '\\' : *p );
    }
    luaL_pushresult( &b );
    return 1;
}

int L_Output( lua_State* L )
{
    HandlerContext* h = RequireHandler( L, "Output" );
    int n = lua_gettop( L );
    for ( int i = 1; i <= n; i++ )
    {
        size_t len;
        const char* s = luaL_tolstring( L, i, &len );
        if ( h->outputSize + len > h->outputCap )
        {
            h->outputCap = ( h->outputSize + len ) * 2 + 4096;
            h->output = (char*)realloc( h->output, h->outputCap );
        }
        memcpy( h->output + h->outputSize, s, len );
        h->outputSize += len;
        lua_pop( L, 1 );
    }
    h->hasOutput = true;
    return 0;
}

int L_Redirect( lua_State* L )
{
    HandlerContext* h = RequireHandler( L, "Redirect" );
    snprintf( h->redirect, sizeof( h->redirect ), "%s", luaL_checkstring( L, 1 ) );
    return 0;
}

int L_IsWriting( lua_State* L )
{
    RequireHandler( L, "IsWriting" );
    lua_pushboolean( L, 0 );
    return 1;
}

int L_IsLegacyOutput( lua_State* L )
{
    RequireHandler( L, "IsLegacyOutput" );
    lua_pushboolean( L, 0 );
    return 1;
}

int L_UseCallbacks( lua_State* L )
{
    return luaL_error( L, "UseCallbacks is not supported yet" );
}

//=============================================================================
// Mod functions
//=============================================================================

Mod* FindMod( const char* name )
{
    for ( int i = 0; i < g_NumMods; i++ )
    {
        if ( strcasecmp( g_Mods[ i ].name, name ) == 0 )
        {
            return &g_Mods[ i ];
        }
    }
    return NULL;
}

int L_GetModPath( lua_State* L )
{
    const char* name = luaL_optstring( L, 1, ModOf( L )->name );
    lua_pushfstring( L, "/Mods/%s", name );
    return 1;
}

int L_GetModName( lua_State* L )
{
    lua_pushstring( L, ModOf( L )->name );
    return 1;
}

int L_GetModTitle( lua_State* L )
{
    Mod* mod = lua_isnoneornil( L, 1 ) ? ModOf( L ) : FindMod( luaL_checkstring( L, 1 ) );
    if ( mod == NULL )
    {
        return 0;
    }
    lua_pushstring( L, mod->title[ 0 ] ? mod->title : mod->name );
    return 1;
}

int L_GetModVersion( lua_State* L )
{
    Mod* mod = lua_isnoneornil( L, 1 ) ? ModOf( L ) : FindMod( luaL_checkstring( L, 1 ) );
    if ( mod == NULL )
    {
        return 0;
    }
    lua_pushstring( L, mod->version );
    return 1;
}

// GetEnabledMods( callback ): callback( internalName ) for each mod, until it returns false
int L_GetEnabledMods( lua_State* L )
{
    luaL_checktype( L, 1, LUA_TFUNCTION );
    for ( int i = 0; i < g_NumMods; i++ )
    {
        lua_pushvalue( L, 1 );
        lua_pushstring( L, g_Mods[ i ].name );
        lua_call( L, 1, 1 );
        bool more = lua_toboolean( L, -1 );
        lua_pop( L, 1 );
        if ( !more )
        {
            break;
        }
    }
    return 0;
}

// The launcher's message box: to the log, nobody is there to press OK
int L_Alert( lua_State* L )
{
    fprintf( stderr, "mods: %s: alert: %s\n", ModOf( L )->name, luaL_tolstring( L, 1, NULL ) );
    return 0;
}

int L_Pause( lua_State* L )
{
    (void)L;
    return 0;
}

int L_IsModEnabled( lua_State* L )
{
    const char* name = luaL_checkstring( L, 1 );
    lua_pushboolean( L, FindMod( name ) != NULL || ModSettingHackCovered( name ) );
    return 1;
}

int L_IsHackLoaded( lua_State* L )
{
    lua_pushboolean( L, ModHackLoaded( luaL_checkstring( L, 1 ) ) );
    return 1;
}

void PushSetting( lua_State* L, const ModSetting& s )
{
    switch ( s.type )
    {
    case MOD_SETTING_BOOLEAN:
        lua_pushboolean( L, strcasecmp( s.value, "true" ) == 0 || atof( s.value ) != 0 );
        break;
    case MOD_SETTING_TEXT:
        lua_pushstring( L, s.value );
        break;
    default:
        {
            char* end;
            double v = strtod( s.value, &end );
            if ( end != s.value && v == (lua_Integer)v )
            {
                lua_pushinteger( L, (lua_Integer)v );
            }
            else if ( end != s.value )
            {
                lua_pushnumber( L, v );
            }
            else
            {
                lua_pushstring( L, s.value );
            }
        }
    }
}

int L_GetSetting( lua_State* L )
{
    const char* name = luaL_checkstring( L, 1 );
    Mod* mod = lua_isnoneornil( L, 2 ) ? ModOf( L ) : FindMod( luaL_checkstring( L, 2 ) );
    for ( int i = 0; mod != NULL && i < mod->numSettings; i++ )
    {
        if ( strcasecmp( mod->settings[ i ].name, name ) == 0 )
        {
            PushSetting( L, mod->settings[ i ] );
            return 1;
        }
    }
    return 0;
}

int L_GetSettings( lua_State* L )
{
    Mod* mod = lua_isnoneornil( L, 1 ) ? ModOf( L ) : FindMod( luaL_checkstring( L, 1 ) );
    lua_newtable( L );
    for ( int i = 0; mod != NULL && i < mod->numSettings; i++ )
    {
        if ( mod->settings[ i ].type == MOD_SETTING_CHOICE )
        {
            // 1-based here, 0-based in GetSetting (launcher docs)
            lua_pushinteger( L, atoi( mod->settings[ i ].value ) + 1 );
            lua_setfield( L, -2, mod->settings[ i ].name );
            continue;
        }
        PushSetting( L, mod->settings[ i ] );
        lua_setfield( L, -2, mod->settings[ i ].name );
    }
    return 1;
}

//=============================================================================
// File system functions
//=============================================================================

int L_ReadFile( lua_State* L )
{
    char* data;
    size_t size;
    if ( !ModVfsRead( luaL_checkstring( L, 1 ), &data, &size ) )
    {
        lua_pushnil( L );
        return 1;
    }
    lua_pushlstring( L, data, size );
    free( data );
    return 1;
}

int L_Exists( lua_State* L )
{
    const char* path = luaL_checkstring( L, 1 );
    bool files = lua_isnoneornil( L, 2 ) ? true : lua_toboolean( L, 2 );
    bool dirs = lua_isnoneornil( L, 3 ) ? true : lua_toboolean( L, 3 );
    bool isDir = false;
    bool exists = ModVfsExists( path, &isDir );
    lua_pushboolean( L, exists && ( isDir ? dirs : files ) );
    return 1;
}

struct ListState
{
    lua_State* L;
    int callback;
    char** names;
    bool* dirs;
    int count, cap;
};

bool CollectEntry( const char* name, bool isDir, void* user )
{
    ListState* s = (ListState*)user;
    if ( s->count == s->cap )
    {
        s->cap = s->cap ? s->cap * 2 : 32;
        s->names = (char**)realloc( s->names, s->cap * sizeof( char* ) );
        s->dirs = (bool*)realloc( s->dirs, s->cap * sizeof( bool ) );
    }
    s->names[ s->count ] = ModDup( name );
    s->dirs[ s->count ] = isDir;
    s->count++;
    return true;
}

int L_DirectoryGetEntries( lua_State* L )
{
    const char* path = luaL_checkstring( L, 1 );
    luaL_checktype( L, 2, LUA_TFUNCTION );
    bool reverse = lua_toboolean( L, 3 );

    // Collect first, then call back: the callback may raise errors (longjmp) or list other directories
    ListState s;
    memset( &s, 0, sizeof( s ) );
    bool completed = true;
    ModVfsList( path, CollectEntry, &s, &completed );

    // The names live in a Lua table from here, so an error in the callback cannot leak them
    lua_createtable( L, s.count, 0 );
    lua_createtable( L, s.count, 0 );
    for ( int i = 0; i < s.count; i++ )
    {
        lua_pushstring( L, s.names[ i ] );
        lua_rawseti( L, -3, i + 1 );
        lua_pushboolean( L, s.dirs[ i ] );
        lua_rawseti( L, -2, i + 1 );
        free( s.names[ i ] );
    }
    free( s.names );
    free( s.dirs );
    int dirsTable = lua_gettop( L ), namesTable = dirsTable - 1;

    bool all = true;
    for ( int n = 0; n < s.count; n++ )
    {
        int i = reverse ? s.count - n : n + 1;
        lua_pushvalue( L, 2 );
        lua_rawgeti( L, namesTable, i );
        lua_rawgeti( L, dirsTable, i );
        lua_call( L, 2, 1 );
        bool more = lua_toboolean( L, -1 ) && lua_isboolean( L, -1 );
        lua_pop( L, 1 );
        if ( !more )
        {
            all = false;
            break;
        }
    }
    lua_pushboolean( L, all );
    return 1;
}

//=============================================================================
// Path and general functions
//=============================================================================

const char* FileNameOf( const char* path )
{
    const char* name = path;
    for ( const char* p = path; *p != '\0'; p++ )
    {
        if ( *p == '/' || *p == '\\' )
        {
            name = p + 1;
        }
    }
    return name;
}

int L_GetFileExtension( lua_State* L )
{
    const char* name = FileNameOf( luaL_checkstring( L, 1 ) );
    const char* dot = strrchr( name, '.' );
    lua_pushstring( L, dot != NULL ? dot : "" );
    return 1;
}

int L_RemoveFileExtension( lua_State* L )
{
    const char* path = luaL_checkstring( L, 1 );
    const char* dot = strrchr( FileNameOf( path ), '.' );
    lua_pushlstring( L, path, dot != NULL ? (size_t)( dot - path ) : strlen( path ) );
    return 1;
}

int L_GetFileName( lua_State* L )
{
    lua_pushstring( L, FileNameOf( luaL_checkstring( L, 1 ) ) );
    return 1;
}

int L_GetPathParent( lua_State* L )
{
    const char* path = luaL_checkstring( L, 1 );
    const char* name = FileNameOf( path );
    size_t n = (size_t)( name - path );
    if ( n > 0 )
    {
        n--;            // the separator
    }
    lua_pushlstring( L, path, n );
    return 1;
}

int L_FixSlashes( lua_State* L )
{
    const char* path = luaL_checkstring( L, 1 );
    bool backslashes = lua_isnoneornil( L, 2 ) ? true : lua_toboolean( L, 2 );
    luaL_Buffer b;
    luaL_buffinit( L, &b );
    for ( ; *path != '\0'; path++ )
    {
        char c = *path;
        if ( c == '/' || c == '\\' )
        {
            c = backslashes ? '\\' : '/';
        }
        luaL_addchar( &b, c );
    }
    luaL_pushresult( &b );
    return 1;
}

int L_ComparePaths( lua_State* L )
{
    char a[ PATH_MAX ], b[ PATH_MAX ];
    ModNormalise( luaL_checkstring( L, 1 ), a, sizeof( a ) );
    ModNormalise( luaL_checkstring( L, 2 ), b, sizeof( b ) );
    lua_pushboolean( L, strcmp( a, b ) == 0 );
    return 1;
}

int L_WildcardMatch( lua_State* L )
{
    char s[ PATH_MAX ], p[ PATH_MAX ];
    ModNormalise( luaL_checkstring( L, 1 ), s, sizeof( s ) );
    ModNormalise( luaL_checkstring( L, 2 ), p, sizeof( p ) );
    lua_pushboolean( L, ModWildcardMatch( p, s ) );
    return 1;
}

int L_GetTime( lua_State* L )
{
    struct timespec ts;
    clock_gettime( CLOCK_MONOTONIC, &ts );
    lua_pushnumber( L, (lua_Number)ts.tv_sec + ts.tv_nsec / 1e9 );
    return 1;
}

int L_IsDemo( lua_State* L )
{
    lua_pushboolean( L, 0 );
    return 1;
}

int L_GetGameLanguage( lua_State* L )
{
    lua_pushliteral( L, "English" );
    return 1;
}

int L_IsTesting( lua_State* L )
{
    lua_pushboolean( L, 0 );
    return 1;
}

const luaL_Reg FUNCTIONS[] = {
    { "print", L_print },
    { "dofile", L_dofile },
    { "loadfile", L_loadfile },
    { "GetPath", L_GetPath },
    { "Output", L_Output },
    { "Redirect", L_Redirect },
    { "IsWriting", L_IsWriting },
    { "IsLegacyOutput", L_IsLegacyOutput },
    { "UseCallbacks", L_UseCallbacks },
    { "GetModPath", L_GetModPath },
    { "GetModName", L_GetModName },
    { "GetModTitle", L_GetModTitle },
    { "GetModVersion", L_GetModVersion },
    { "GetEnabledMods", L_GetEnabledMods },
    { "Alert", L_Alert },
    { "Pause", L_Pause },
    { "IsModEnabled", L_IsModEnabled },
    { "IsHackLoaded", L_IsHackLoaded },
    { "GetSetting", L_GetSetting },
    { "GetSettings", L_GetSettings },
    { "ReadFile", L_ReadFile },
    { "Exists", L_Exists },
    { "DirectoryGetEntries", L_DirectoryGetEntries },
    { "GetFileExtension", L_GetFileExtension },
    { "RemoveFileExtension", L_RemoveFileExtension },
    { "GetFileName", L_GetFileName },
    { "GetPathParent", L_GetPathParent },
    { "FixSlashes", L_FixSlashes },
    { "ComparePaths", L_ComparePaths },
    { "WildcardMatch", L_WildcardMatch },
    { "GetTime", L_GetTime },
    { "IsDemo", L_IsDemo },
    { "GetGameLanguage", L_GetGameLanguage },
    { "IsTesting", L_IsTesting },
    { NULL, NULL }
};

int MessageHandler( lua_State* L )
{
    luaL_traceback( L, L, luaL_tolstring( L, 1, NULL ), 1 );
    return 1;
}

// Runs the function on top (no arguments, results dropped); prints errors
bool Call( lua_State* L, const char* what )
{
    lua_pushcfunction( L, MessageHandler );
    lua_insert( L, -2 );
    int handler = lua_gettop( L ) - 1;
    bool ok = lua_pcall( L, 0, 0, handler ) == LUA_OK;
    if ( !ok )
    {
        fprintf( stderr, "mods: Lua error in %s:\n%s\n", what, lua_tostring( L, -1 ) );
        lua_pop( L, 1 );
    }
    lua_remove( L, handler );
    return ok;
}

// Loads and runs a mod's script (mod-relative, normalised)
bool RunScript( Mod* mod, const char* script )
{
    lua_State* L = (lua_State*)mod->lua;
    char vpath[ PATH_MAX ];
    snprintf( vpath, sizeof( vpath ), "/Mods/%s/%s", mod->name, script );
    if ( LoadVFile( L, vpath ) != 1 )
    {
        fprintf( stderr, "mods: %s: %s\n", mod->name, lua_tostring( L, -1 ) );
        lua_pop( L, 2 );
        return false;
    }
    return Call( L, vpath );
}

// SHAR_MOD_DUMP=dir: keep a copy of every file a path handler generates (dir/<game path with '_' for '\'>)
void DumpOutput( const char* gamePath, const char* data, size_t size )
{
    const char* dir = getenv( "SHAR_MOD_DUMP" );
    if ( dir == NULL || dir[ 0 ] == '\0' )
    {
        return;
    }
    char path[ PATH_MAX ];
    int n = snprintf( path, sizeof( path ), "%s/", dir );
    for ( const char* p = gamePath; *p != '\0' && n + 1 < (int)sizeof( path ); p++ )
    {
        path[ n++ ] = ( *p == '\\' || *p == '/' || *p == ':' ) ? '_' : *p;
    }
    path[ n ] = '\0';
    FILE* f = fopen( path, "wb" );
    if ( f != NULL )
    {
        fwrite( data, 1, size, f );
        fclose( f );
    }
}

void OpenLibrary( lua_State* L, const char* name, lua_CFunction open )
{
    luaL_requiref( L, name, open, 1 );
    lua_pop( L, 1 );
}

} // namespace

void ModLuaStart( Mod* mod )
{
    lua_State* L = luaL_newstate( );
    if ( L == NULL )
    {
        return;
    }
    mod->lua = L;

    OpenLibrary( L, LUA_GNAME, luaopen_base );
    OpenLibrary( L, LUA_STRLIBNAME, luaopen_string );
    OpenLibrary( L, LUA_TABLIBNAME, luaopen_table );
    OpenLibrary( L, LUA_MATHLIBNAME, luaopen_math );
    OpenLibrary( L, LUA_UTF8LIBNAME, luaopen_utf8 );
    OpenLibrary( L, LUA_COLIBNAME, luaopen_coroutine );

    // os: the clock only
    lua_newtable( L );
    luaopen_os( L );
    static const char* const OS_KEEP[] = { "time", "clock", "date", "difftime" };
    for ( unsigned i = 0; i < sizeof( OS_KEEP ) / sizeof( OS_KEEP[ 0 ] ); i++ )
    {
        lua_getfield( L, -1, OS_KEEP[ i ] );
        lua_setfield( L, -3, OS_KEEP[ i ] );
    }
    lua_pop( L, 1 );
    lua_setglobal( L, "os" );

    // Launcher API; every function gets the mod as upvalue 1
    lua_pushglobaltable( L );
    lua_pushlightuserdata( L, mod );
    luaL_setfuncs( L, FUNCTIONS, 1 );
    lua_getfield( L, -1, "load" );
    lua_pushlightuserdata( L, mod );
    lua_insert( L, -2 );
    lua_pushcclosure( L, L_load, 2 );
    lua_setfield( L, -2, "load" );
    lua_pop( L, 1 );

    pthread_mutex_lock( &s_Lock );
    char key[] = "customfiles.lua";
    bool hasScript = false;
    if ( mod->packaged )
    {
        for ( int i = 0; i < mod->numEntries; i++ )
        {
            hasScript |= strcmp( mod->entries[ i ].key, key ) == 0;
        }
    }
    else
    {
        bool isDir;
        char vpath[ PATH_MAX ];
        snprintf( vpath, sizeof( vpath ), "/Mods/%s/CustomFiles.lua", mod->name );
        hasScript = ModVfsExists( vpath, &isDir ) && !isDir;
    }
    if ( hasScript )
    {
        RunScript( mod, key );
    }
    pthread_mutex_unlock( &s_Lock );
    fprintf( stderr, "mods:   %s: Lua started (%d path handlers)\n", mod->name, mod->numHandlers );
}

int ModLuaRunHandler( Mod* mod, const char* script, const char* gamePath, int* fd, unsigned int* size,
                      char* redirect, size_t redirectSize )
{
    if ( mod->lua == NULL )
    {
        return 0;
    }
    pthread_mutex_lock( &s_Lock );
    HandlerContext& h = s_Handler;
    h.active = true;
    h.gamePath = gamePath;
    h.outputSize = 0;
    h.hasOutput = false;
    h.redirect[ 0 ] = '\0';
    bool ok = RunScript( mod, script );
    h.active = false;

    int result = 0;
    if ( ok && h.hasOutput )
    {
        // The output as a file of its own: an anonymous in-memory one
#ifdef __APPLE__
        // no memfd on macOS: an unlinked temporary file behaves the same
        char tmpPath[] = "/tmp/shar-mod-output-XXXXXX";
        int memfd = mkstemp( tmpPath );
        if ( memfd >= 0 )
        {
            unlink( tmpPath );
            fcntl( memfd, F_SETFD, FD_CLOEXEC );
        }
#else
        int memfd = memfd_create( "shar-mod-output", MFD_CLOEXEC );
#endif
        size_t done = 0;
        while ( memfd >= 0 && done < h.outputSize )
        {
            ssize_t n = write( memfd, h.output + done, h.outputSize - done );
            if ( n <= 0 )
            {
                close( memfd );
                memfd = -1;
                break;
            }
            done += (size_t)n;
        }
        if ( memfd >= 0 )
        {
            *fd = memfd;
            *size = (unsigned int)h.outputSize;
            result = 1;
            DumpOutput( gamePath, h.output, h.outputSize );
        }
    }
    else if ( ok && h.redirect[ 0 ] != '\0' )
    {
        snprintf( redirect, redirectSize, "%s", h.redirect );
        result = 2;
    }
    pthread_mutex_unlock( &s_Lock );
    return result;
}
