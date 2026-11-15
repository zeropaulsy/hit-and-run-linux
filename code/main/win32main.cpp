//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        xboxmain.cpp
//
// Description: This file contains the main enrty point to the game.
//
// History:     + Based on xbox main and winmain from squidney
//
//=============================================================================

//========================================
// System Includes
//========================================
// Standard Library
#include <string.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
// Foundation Tech
#include <raddebug.hpp>
#include <radobject.hpp>
// file reading before radtech
#include <stdio.h>

//========================================
// Project Includes
//========================================
#include <main/game.h>
#include <main/win32platform.h>
#include <main/singletons.h>
#include <main/commandlineoptions.h>
#include <memory/memoryutilities.h>
#include <memory/srrmemory.h>

//========================================
// Forward Declarations
//========================================
static void ProcessCommandLineArguments( LPSTR lpCmdLine );

static void ProcessCommandLineArgumentsFromFile();


//=============================================================================
// Function:    WinMain
//=============================================================================
//
// Description: Main Windows entry point.
// 
// Parameters:  win32 parameters
//
// Returns:     win32 return.
//
//=============================================================================
int WINAPI WinMain( HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow )
{
    //
    // Pick out and store command line settings.
    //
    CommandLineOptions::InitDefaults();
    ProcessCommandLineArguments( lpCmdLine );
    ProcessCommandLineArgumentsFromFile();

    //
    // Have to get FTech setup first so that we can use all the memory services.
    // The initialize window call will fail if another Simpsons window exists. In
    // this case, we exit.
    //
    if( !Win32Platform::InitializeWindow( hInstance ) )
    {
        return 0;
    }
    Win32Platform::InitializeFoundation();

    srand (Game::GetRandomSeed ());


    // Now disable the default heap
    //
#ifndef RAD_RELEASE
    tName::SetAllocator (GMA_DEBUG);

    //g_HeapActivityTracker.EnableHeapAllocs (GMA_DEFAULT, false);
    //g_HeapActivityTracker.EnableHeapFrees (GMA_DEFAULT, false);
#endif

    HeapMgr()->PushHeap (GMA_PERSISTENT);

    //
    // Instantiate all the singletons before doing anything else.
    //
    CreateSingletons();

    //
    // Construct the platform object.
    //
    Win32Platform* pPlatform = Win32Platform::CreateInstance( hInstance, hPrevInstance, lpCmdLine, nCmdShow );
    rAssert( pPlatform != NULL );

    //
    // Create the game object.
    //
    Game* pGame = Game::CreateInstance( pPlatform );
    rAssert( pGame != NULL );


    //
    // Initialize the game.
    //
    pGame->Initialize();

    HeapMgr()->PopHeap (GMA_PERSISTENT);

    //
    // Run it!  Control will not return from here until the game is stopped.
    //
    pGame->Run();

    //
    // Terminate the game (this frees all resources allocated by the game).
    //
    pGame->Terminate();

    //
    // Dump all the singletons.
    //
    DestroySingletons();

    //
    // Destroy the game object.
    //
    Game::DestroyInstance();

    //
    // Shutdown the platform.
    //
    pPlatform->ShutdownPlatform();

    //
    // Destroy the game and platform (do it in this order in case the game's 
    // destructor references the platform.
    //
    Win32Platform::DestroyInstance();

    // As a last thing, shut down the memory.
    Win32Platform::ShutdownMemory();

    // Re-enable the default heap
    //
#ifndef RAD_RELEASE
    tName::SetAllocator (RADMEMORY_ALLOC_DEFAULT);
#endif

    //
    // Pass any error codes back to the operating system.
    //
    return 0;
}


//=============================================================================
// Function:    ProcessCommandLineArguments
//=============================================================================
//
// Description: Pick out the command line options and store them.
// 
// Parameters:  None.
//
// Returns:     None.
//
//=============================================================================
void ProcessCommandLineArguments( LPSTR lpCmdLine )
{
    char* argument;
    argument = strtok( lpCmdLine, " " );    

    rDebugPrintf( "*************************************************************************\n" );
    rDebugPrintf( "Command Line Args:\n" );

    //
    // Pick out all the command line options and store them in GameDB.
    // Also dump them to the output for handy dandy viewing.
    //
    int i = 0;
    while( NULL != argument )
    {
        rDebugPrintf( "arg%d: %s\n", i++, argument );

        CommandLineOptions::HandleOption( argument );

        argument = strtok( NULL, " " );
    }

    if( !CommandLineOptions::Get( CLO_ART_STATS ) )
    {
        //CommandLineOptions::HandleOption( "noheaps" );
    }

    rDebugPrintf( "*************************************************************************\n" );
}



void ProcessCommandLineArgumentsFromFile()
{
#ifndef FINAL

    //Chuck: looking for additional command line args being passed in from a file
    //its for QA testing etc.

    FILE* pfile = fopen( "command.txt", "r" );

    if (pfile != NULL)
    {
        int ret = fseek( pfile, 0, SEEK_END ); 
        rAssert( ret == 0 );

        int len = ftell( pfile );
        rAssertMsg( len < 256, "Command line file too large to process." );

        rewind( pfile );

        if( len > 0 && len < 256 )
        {
            char commandlinestring[256] = {0};

            fgets( commandlinestring, 256, pfile );
                    
            char* argument = strtok(commandlinestring," ");
            while (argument != NULL)
            {
                CommandLineOptions::HandleOption(argument);
                argument=strtok(NULL," ");
            }
        }

        fclose( pfile );
    }
#endif //FINAL
} //end of Function

#ifdef RAD_LINUX
//=============================================================================
// Function:    main
//=============================================================================
// Linux entry point: rebuild the Windows command-line string from argv and run the original WinMain.
//=============================================================================
extern "C" void shar_CrashInstall( void );    // port/compat/shar_crash.h

#include <dirent.h>
#include <libgen.h>
#include <limits.h>
#include <strings.h>
#include <unistd.h>
#include <SDL2/SDL.h>

// True if dir holds the game data (art/ and scripts/, any case).
static bool HasGameData( const char* dir )
{
    DIR* d = opendir( dir );
    if( d == NULL )
    {
        return false;
    }
    bool art = false, scripts = false;
    while( struct dirent* e = readdir( d ) )
    {
        art |= strcasecmp( e->d_name, "art" ) == 0;
        scripts |= strcasecmp( e->d_name, "scripts" ) == 0;
    }
    closedir( d );
    return art && scripts;
}

// The game root is the working directory. Started from elsewhere (a file manager, a terminal in the build dir)
// every load failed and the game hung on a black window: use the executable's directory if the data is there,
// otherwise say what is wrong and stop.
static void FindGameData( void )
{
    if( HasGameData( "." ) )
    {
        return;
    }
    char exe[ PATH_MAX ];
#ifdef __APPLE__
    uint32_t exeSize = sizeof( exe );
    ssize_t len = _NSGetExecutablePath( exe, &exeSize ) == 0 ? (ssize_t) strlen( exe ) : -1;
#else
    ssize_t len = readlink( "/proc/self/exe", exe, sizeof( exe ) - 1 );
#endif
    if( len > 0 )
    {
        exe[ len ] = '\0';
        const char* dir = dirname( exe );
        if( HasGameData( dir ) && chdir( dir ) == 0 )
        {
            return;
        }
    }
    const char* msg = "The Simpsons: Hit & Run could not find its game data.\n\n"
                      "Run it from your PC game folder (the one holding art/, scripts/ and the .rcf files), "
                      "or start it with play.sh --data <that folder>.";
    fprintf( stderr, "%s\n", msg );
    SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "The Simpsons: Hit & Run", msg, NULL );
    exit( 1 );
}

int main( int argc, char** argv )
{
    shar_CrashInstall();
    FindGameData();
    static char cmdLine[ 4096 ];
    cmdLine[ 0 ] = '\0';
    for( int i = 1; i < argc; i++ )
    {
        if( i > 1 )
        {
            strncat( cmdLine, " ", sizeof( cmdLine ) - strlen( cmdLine ) - 1 );
        }
        strncat( cmdLine, argv[ i ], sizeof( cmdLine ) - strlen( cmdLine ) - 1 );
    }
    return WinMain( NULL, NULL, cmdLine, 0 );
}
#endif // RAD_LINUX
