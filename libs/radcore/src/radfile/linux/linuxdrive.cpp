//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// Linux port of win32/win32drive.cpp. Same behaviour, POSIX calls. See linuxdrive.hpp for the path model.
//=============================================================================

#include "pch.hpp"
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include "linuxdrive.hpp"
#include <shar_mods.h>

//=============================================================================
// Path resolution
//=============================================================================

static char s_GameRoot[ PATH_MAX ] = "";

static const char* GameRoot( void )
{
    if ( s_GameRoot[ 0 ] == '\0' )
    {
        if ( getcwd( s_GameRoot, sizeof( s_GameRoot ) ) == NULL )
        {
            strcpy( s_GameRoot, "." );
        }
    }
    return s_GameRoot;
}

// Find a directory entry matching name case-insensitively. Returns false if none.
static bool FindEntryNoCase( const char* dir, const char* name, char* out, unsigned int outSize )
{
    DIR* d = opendir( dir );
    if ( d == NULL )
    {
        return false;
    }
    bool found = false;
    struct dirent* e;
    while ( ( e = readdir( d ) ) != NULL )
    {
        if ( strcasecmp( e->d_name, name ) == 0 )
        {
            strncpy( out, e->d_name, outSize - 1 );
            out[ outSize - 1 ] = '\0';
            found = true;
            break;
        }
    }
    closedir( d );
    return found;
}

void radLinuxResolvePath( const char* gamePath, char* realPath, unsigned int size, bool leafMayNotExist )
{
    // Strip a drive letter ("C:"); every drive is the game directory.
    if ( gamePath[ 0 ] != '\0' && gamePath[ 1 ] == ':' )
    {
        gamePath += 2;
    }

    // Split into components, handling both separators and "." / "..".
    char components[ 64 ][ NAME_MAX + 1 ];
    int count = 0;
    const char* p = gamePath;
    while ( *p != '\0' )
    {
        while ( *p == '\\' || *p == '/' )
        {
            p++;
        }
        if ( *p == '\0' )
        {
            break;
        }
        const char* start = p;
        while ( *p != '\0' && *p != '\\' && *p != '/' )
        {
            p++;
        }
        unsigned int len = (unsigned int)( p - start );
        if ( len == 1 && start[ 0 ] == '.' )
        {
            continue;
        }
        if ( len == 2 && start[ 0 ] == '.' && start[ 1 ] == '.' )
        {
            if ( count > 0 )
            {
                count--;
            }
            continue;
        }
        if ( count < 64 && len <= NAME_MAX )
        {
            memcpy( components[ count ], start, len );
            components[ count ][ len ] = '\0';
            count++;
        }
    }

    // Walk from the game root, fixing the case of each component that does not match exactly.
    char path[ PATH_MAX ];
    strncpy( path, GameRoot( ), sizeof( path ) - 1 );
    path[ sizeof( path ) - 1 ] = '\0';
    for ( int i = 0; i < count; i++ )
    {
        size_t base = strlen( path );
        snprintf( path + base, sizeof( path ) - base, "/%s", components[ i ] );

        struct stat st;
        if ( stat( path, &st ) == 0 )
        {
            continue;                                           // exact match
        }
        char real[ NAME_MAX + 1 ];
        path[ base ] = '\0';
        if ( FindEntryNoCase( path, components[ i ], real, sizeof( real ) ) )
        {
            snprintf( path + base, sizeof( path ) - base, "/%s", real );
        }
        else
        {
            // Not found: keep the remaining components as given (a file about to be created,
            // or a genuine "file not found" that open() will report).
            for ( int j = i; j < count; j++ )
            {
                size_t b = strlen( path );
                snprintf( path + b, sizeof( path ) - b, "/%s", components[ j ] );
            }
            break;
        }
    }
    (void)leafMayNotExist;
    strncpy( realPath, path, size - 1 );
    realPath[ size - 1 ] = '\0';
}

//=============================================================================
// Handles: an open file is its fd stored in the pointer (INVALID_HANDLE_VALUE is -1, like a failed open)
//=============================================================================

static inline int FdOf( radFileHandle h )               { return (int)(intptr_t)h; }
static inline radFileHandle HandleOf( int fd )          { return (radFileHandle)(intptr_t)fd; }

// A mod file inside a .lmlm package is an fd on the package plus the file's slice of it, kept here by fd
// (each slot is only touched by the thread that owns the fd). Fds past the table cannot hold a packaged file.
struct PackagedSlice
{
    unsigned long long offset;
    unsigned int size;
    bool used;
};
static const int MAX_SLICE_FDS = 4096;
static PackagedSlice s_Slices[ MAX_SLICE_FDS ];

static const PackagedSlice* SliceOf( int fd )
{
    return ( fd >= 0 && fd < MAX_SLICE_FDS && s_Slices[ fd ].used ) ? &s_Slices[ fd ] : NULL;
}

struct LinuxDirSearch
{
    DIR* dir;
    char pattern[ NAME_MAX + 1 ];
};

//=============================================================================
// Public Functions
//=============================================================================

void radLinuxDriveFactory
(
    radDrive**         ppDrive,
    const char*        pDriveName,
    radMemoryAllocator alloc
)
{
    *ppDrive = new( alloc ) radLinuxDrive( pDriveName, alloc );
    rAssert( *ppDrive != NULL );
}

//=============================================================================
// Public Member Functions
//=============================================================================

radLinuxDrive::radLinuxDrive( const char* pdrivespec, radMemoryAllocator alloc )
    :
    radDrive( ),
    m_OpenFiles( 0 ),
    m_pMutex( NULL )
{
    radThreadCreateMutex( &m_pMutex, alloc );
    rAssert( m_pMutex != NULL );

    m_pDriveThread = new( alloc ) radDriveThread( m_pMutex, alloc );
    rAssert( m_pDriveThread != NULL );

    strncpy( m_DriveName, pdrivespec, radFileDrivenameMax );
    m_DriveName[ radFileDrivenameMax ] = '\0';

    m_Capabilities = ( radDriveEnumerable | radDriveWriteable | radDriveDirectory | radDriveFile );
}

radLinuxDrive::~radLinuxDrive( void )
{
    m_pMutex->Release( );
    m_pDriveThread->Release( );
}

void radLinuxDrive::Lock( void )
{
    m_pMutex->Lock( );
}

void radLinuxDrive::Unlock( void )
{
    m_pMutex->Unlock( );
}

unsigned int radLinuxDrive::GetCapabilities( void )
{
    return m_Capabilities;
}

const char* radLinuxDrive::GetDriveName( void )
{
    return m_DriveName;
}

radDrive::CompletionStatus radLinuxDrive::Initialize( void )
{
    SetMediaInfo( );                // a fixed disk: media never changes
    m_LastError = Success;
    return Complete;
}

radDrive::CompletionStatus radLinuxDrive::OpenFile
(
    const char*         fileName,
    radFileOpenFlags    flags,
    bool                writeAccess,
    radFileHandle*      pHandle,
    unsigned int*       pSize
)
{
    char fullName[ PATH_MAX ];
    char redirected[ PATH_MAX ];
    const char* openName = fileName;
    if ( !writeAccess && flags == OpenExisting )
    {
        // Mods (shar_mods.h): usually resolved by the file request already (requests.cpp)
        SharModFile modFile;
        const char* redirect = NULL;
        switch ( shar_ModTakeResolved( fileName, &modFile, &redirect ) )
        {
        case SHAR_MOD_MISSING:
            *pHandle = INVALID_HANDLE_VALUE;
            m_LastError = FileNotFound;
            return Error;
        case SHAR_MOD_REDIRECT:
            strncpy( redirected, redirect, sizeof( redirected ) - 1 );
            redirected[ sizeof( redirected ) - 1 ] = '\0';
            openName = redirected;
            break;
        case SHAR_MOD_FILE:
            {
                // generated content (an fd we now own), a mod folder's file, or a slice of a .lmlm package
                int fd = modFile.fd >= 0 ? modFile.fd : open( modFile.path, O_RDONLY | O_CLOEXEC );
                if ( fd != -1 && modFile.packaged && fd >= MAX_SLICE_FDS )
                {
                    close( fd );
                    fd = -1;
                    errno = EMFILE;
                }
                if ( fd == -1 )
                {
                    *pHandle = INVALID_HANDLE_VALUE;
                    m_LastError = TranslateError( errno );
                    return Error;
                }
                if ( modFile.packaged )
                {
                    s_Slices[ fd ].offset = modFile.offset;
                    s_Slices[ fd ].size = modFile.size;
                    s_Slices[ fd ].used = true;
                }
                m_OpenFiles++;
                *pHandle = HandleOf( fd );
                *pSize = modFile.size;
                m_LastError = Success;
                return Complete;
            }
        }
    }
    radLinuxResolvePath( openName, fullName, sizeof( fullName ), flags != OpenExisting );

    int oflags = writeAccess ? O_RDWR : O_RDONLY;
    switch( flags )
    {
    case OpenExisting:
        break;
    case OpenAlways:
        oflags |= O_CREAT;
        break;
    case CreateAlways:
        oflags |= O_CREAT | O_TRUNC;
        break;
    default:
        rAssertMsg( false, "radFileSystem: linuxdrive: attempting to open file with unknown flag" );
    }

    int fd = open( fullName, oflags, 0644 );
    if ( fd != -1 )
    {
        struct stat st;
        fstat( fd, &st );
        m_OpenFiles++;
        *pHandle = HandleOf( fd );
        *pSize = (unsigned int) st.st_size;
        m_LastError = Success;
        return Complete;
    }

    *pHandle = INVALID_HANDLE_VALUE;
    m_LastError = TranslateError( errno );
    return Error;
}

radDrive::CompletionStatus radLinuxDrive::CloseFile( radFileHandle handle, const char* fileName )
{
    int fd = FdOf( handle );
    if ( fd >= 0 && fd < MAX_SLICE_FDS )
    {
        s_Slices[ fd ].used = false;
    }
    close( fd );
    m_OpenFiles--;
    return Complete;
}

radDrive::CompletionStatus radLinuxDrive::ReadFile
(
    radFileHandle   handle,
    const char*     fileName,
    IRadFile::BufferedReadState buffState,
    unsigned int    position,
    void*           pData,
    unsigned int    bytesToRead,
    unsigned int*   bytesRead,
    radMemorySpace  pDataSpace
)
{
    rAssertMsg( pDataSpace == radMemorySpace_Local,
                "radFileSystem: radLinuxDrive: External memory not supported for reads." );

    // A packaged mod file: read inside its slice only, so its end is a real end of file
    off_t base = 0;
    unsigned int toRead = bytesToRead;
    const PackagedSlice* slice = SliceOf( FdOf( handle ) );
    if ( slice != NULL )
    {
        base = (off_t)slice->offset;
        toRead = position >= slice->size ? 0 : ( bytesToRead < slice->size - position ? bytesToRead : slice->size - position );
    }

    // Win32 ReadFile is synchronous here and reports bytesToRead on success; keep that contract.
    unsigned int done = 0;
    while ( done < toRead )
    {
        ssize_t n = pread( FdOf( handle ), (char*)pData + done, toRead - done, base + (off_t)position + done );
        if ( n < 0 )
        {
            if ( errno == EINTR )
            {
                continue;
            }
            m_LastError = TranslateError( errno );
            return Error;
        }
        if ( n == 0 )
        {
            break;                                      // end of file: Win32 also succeeds with a short read
        }
        done += (unsigned int) n;
    }
    *bytesRead = bytesToRead;
    m_LastError = Success;
    return Complete;
}

radDrive::CompletionStatus radLinuxDrive::WriteFile
(
    radFileHandle     handle,
    const char*       fileName,
    IRadFile::BufferedReadState buffState,
    unsigned int      position,
    const void*       pData,
    unsigned int      bytesToWrite,
    unsigned int*     bytesWritten,
    unsigned int*     pSize,
    radMemorySpace    pDataSpace
)
{
    if ( !( m_Capabilities & radDriveWriteable ) )
    {
        rWarningMsg( m_Capabilities & radDriveWriteable, "This drive does not support the WriteFile function." );
        return Error;
    }

    rAssertMsg( pDataSpace == radMemorySpace_Local,
                "radFileSystem: radLinuxDrive: External memory not supported for reads." );

    int fd = FdOf( handle );
    if ( position + bytesToWrite > *pSize )
    {
        if ( ftruncate( fd, (off_t)( position + bytesToWrite ) ) != 0 )    // presize, like SetEndOfFile
        {
            m_LastError = TranslateError( errno );
            return Error;
        }
    }

    unsigned int done = 0;
    while ( done < bytesToWrite )
    {
        ssize_t n = pwrite( fd, (const char*)pData + done, bytesToWrite - done, (off_t)position + done );
        if ( n < 0 )
        {
            if ( errno == EINTR )
            {
                continue;
            }
            m_LastError = TranslateError( errno );
            return Error;
        }
        done += (unsigned int) n;
    }
    *bytesWritten = done;

    struct stat st;
    fstat( fd, &st );
    *pSize = (unsigned int) st.st_size;
    m_LastError = Success;
    return Complete;
}

radDrive::CompletionStatus radLinuxDrive::FindFirst
(
    const char*                 searchSpec,
    IRadDrive::DirectoryInfo*   pDirectoryInfo,
    radFileDirHandle*           pHandle,
    bool                        firstSearch
)
{
    if ( !firstSearch && *pHandle != INVALID_HANDLE_VALUE && *pHandle != NULL )
    {
        FindClose( pHandle );
    }

    // Split "dir\pattern" (pattern may contain * and ?)
    char spec[ radFileFilenameMax + 1 ];
    strncpy( spec, searchSpec, radFileFilenameMax );
    spec[ radFileFilenameMax ] = '\0';
    char* slash = NULL;
    for ( char* c = spec; *c; c++ )
    {
        if ( *c == '\\' || *c == '/' )
        {
            slash = c;
        }
    }
    const char* pattern = slash ? slash + 1 : ( spec[ 0 ] && spec[ 1 ] == ':' ? spec + 2 : spec );
    if ( slash )
    {
        *slash = '\0';
    }
    else if ( spec[ 0 ] && spec[ 1 ] == ':' )
    {
        spec[ 2 ] = '\0';
    }
    else
    {
        spec[ 0 ] = '\0';
    }

    char dirPath[ PATH_MAX ];
    radLinuxResolvePath( spec, dirPath, sizeof( dirPath ), false );

    DIR* d = opendir( dirPath );
    if ( d == NULL )
    {
        *pHandle = INVALID_HANDLE_VALUE;
        if ( errno == ENOENT )
        {
            pDirectoryInfo->m_Name[ 0 ] = '\0';
            pDirectoryInfo->m_Type = IRadDrive::DirectoryInfo::IsDone;
            m_LastError = Success;
            return Complete;
        }
        m_LastError = TranslateError( errno );
        return Error;
    }

    LinuxDirSearch* search = new LinuxDirSearch;
    search->dir = d;
    strncpy( search->pattern, *pattern ? pattern : "*", NAME_MAX );
    search->pattern[ NAME_MAX ] = '\0';
    *pHandle = search;

    return FindNext( pHandle, pDirectoryInfo );
}

radDrive::CompletionStatus radLinuxDrive::FindNext( radFileDirHandle* pHandle, IRadDrive::DirectoryInfo* pDirectoryInfo )
{
    if ( *pHandle == INVALID_HANDLE_VALUE || *pHandle == NULL )
    {
        m_LastError = FileNotFound;
        return Error;
    }

    LinuxDirSearch* search = (LinuxDirSearch*) *pHandle;
    struct dirent* e;
    while ( ( e = readdir( search->dir ) ) != NULL )
    {
        if ( strcmp( e->d_name, "." ) == 0 || strcmp( e->d_name, ".." ) == 0 )
        {
            continue;                                   // Win32 FindFirstFile on "dir\*" does list them, but
        }                                               // the game only ever looks for files
        if ( fnmatch( search->pattern, e->d_name, FNM_CASEFOLD ) != 0 )
        {
            continue;
        }
        strncpy( pDirectoryInfo->m_Name, e->d_name, radFileFilenameMax );
        pDirectoryInfo->m_Name[ radFileFilenameMax ] = '\0';
        pDirectoryInfo->m_Type = ( e->d_type == DT_DIR ) ? IRadDrive::DirectoryInfo::IsDirectory
                                                         : IRadDrive::DirectoryInfo::IsFile;
        m_LastError = Success;
        return Complete;
    }

    pDirectoryInfo->m_Name[ 0 ] = '\0';
    pDirectoryInfo->m_Type = IRadDrive::DirectoryInfo::IsDone;
    m_LastError = Success;
    return Complete;
}

radDrive::CompletionStatus radLinuxDrive::FindClose( radFileDirHandle* pHandle )
{
    if ( *pHandle != INVALID_HANDLE_VALUE && *pHandle != NULL )
    {
        LinuxDirSearch* search = (LinuxDirSearch*) *pHandle;
        closedir( search->dir );
        delete search;
    }
    *pHandle = INVALID_HANDLE_VALUE;
    return Complete;
}

radDrive::CompletionStatus radLinuxDrive::CreateDir( const char* pName )
{
    rWarningMsg( m_Capabilities & radDriveDirectory,
        "This drive does not support the CreateDir function." );

    char fullSpec[ PATH_MAX ];
    radLinuxResolvePath( pName, fullSpec, sizeof( fullSpec ), true );

    if ( mkdir( fullSpec, 0755 ) == 0 )
    {
        m_LastError = Success;
        return Complete;
    }
    m_LastError = TranslateError( errno );
    return Error;
}

radDrive::CompletionStatus radLinuxDrive::DestroyDir( const char* pName )
{
    rWarningMsg( m_Capabilities & radDriveDirectory,
        "This drive does not support the DestroyDir function." );

    char fullSpec[ PATH_MAX ];
    radLinuxResolvePath( pName, fullSpec, sizeof( fullSpec ), false );

    if ( rmdir( fullSpec ) == 0 )
    {
        m_LastError = Success;
        return Complete;
    }
    m_LastError = TranslateError( errno );
    return Error;
}

radDrive::CompletionStatus radLinuxDrive::DestroyFile( const char* filename )
{
    rWarningMsg( m_Capabilities & radDriveWriteable, "This drive does not support the DestroyFile function." );

    char fullSpec[ PATH_MAX ];
    radLinuxResolvePath( filename, fullSpec, sizeof( fullSpec ), false );

    if ( unlink( fullSpec ) == 0 )
    {
        m_LastError = Success;
        return Complete;
    }
    m_LastError = TranslateError( errno );
    return Error;
}

//=============================================================================
// Private Member Functions
//=============================================================================

void radLinuxDrive::SetMediaInfo( void )
{
    strncpy( m_MediaInfo.m_VolumeName, "LINUX", sizeof( m_MediaInfo.m_VolumeName ) - 1 );
    m_MediaInfo.m_VolumeName[ sizeof( m_MediaInfo.m_VolumeName ) - 1 ] = '\0';
    m_MediaInfo.m_MediaState = IRadDrive::MediaInfo::MediaPresent;

    struct statvfs vfs;
    if ( statvfs( GameRoot( ), &vfs ) == 0 )
    {
        unsigned long long freeBytes = (unsigned long long) vfs.f_bavail * vfs.f_frsize;
        m_MediaInfo.m_FreeSpace = freeBytes > UINT_MAX ? UINT_MAX : (unsigned int) freeBytes;
    }
    else
    {
        m_MediaInfo.m_FreeSpace = 0;
    }

    m_MediaInfo.m_SectorSize = LINUX_DEFAULT_SECTOR_SIZE;
    m_MediaInfo.m_FreeFiles = m_MediaInfo.m_FreeSpace / m_MediaInfo.m_SectorSize;
}

radFileError radLinuxDrive::TranslateError( int posixError )
{
    switch( posixError )
    {
    case 0:
        rAssertMsg( false, "radFileSystem: internal error\n" );
        return( Success );

    case ENOENT:
    case ENOTDIR:
    case ENAMETOOLONG:
        return( FileNotFound );

    case ENOSPC:
    case EDQUOT:
        return ( NoFreeSpace );

    case ENXIO:
    case ENODEV:
#ifdef ENOMEDIUM
    case ENOMEDIUM:
        return( NoMedia );
#endif

#ifdef EMEDIUMTYPE
    case EMEDIUMTYPE:
        return( MediaWrongType );
#endif

    default:
        return( HardwareFailure );
    }
}
