//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        commandlineoptions.h
//
// Description: CommandLineOptions class declaration.
//
// History:     + Created -- Darwin Chau
//
//=============================================================================

#ifndef COMMANDLINEOPTIONS_H
#define COMMANDLINEOPTIONS_H

#include <main/globaltypes.h>

//#if defined( WORLD_BUILDER ) || defined( TOOLS )
//typedef long simpsonsUInt64;
//#endif

//
// Remember, we can only have 64 of these things
// 
enum CmdLineOptionEnum
{
    CLO_NO_MUSIC,       // Disable music
    CLO_NO_EFFECTS,     // Disable sound effects
    CLO_NO_DIALOG,      // Disable dialog
    CLO_MUTE,           // Disable all sound
    CLO_SKIP_MOVIE,     // Skip intro movie
    CLO_MEMORY_MONITOR, // Enable RadMemoryMonitor
    CLO_HEAP_STATS,     // Enable heap stats display
    CLO_CD_FILES_ONLY,  // PS2 Only - Only load files from CD/DVD
    CLO_FIREWIRE,       // PS2 Only - Enable IEEE Firewire support
    CLO_SN_PROFILER,    // PS2 Only - Enable SN profiler
    CLO_ART_STATS,      // Custom display for the Artists, Such as FPS,Texture/Mesh Allocations etc.
    CLO_PROP_STATS,     // Enables logging of prop memory usage

    CLO_RANDOM_BUTTONS, // Enables random button pressing
    CLO_DEMO_TEST,      // Enables one-second demo loop time for testing purposes
    CLO_SEQUENTIAL_DEMO,// Enable demo loop in sequential order
    CLO_SHORT_DEMO,     // Run demo loop at 60 seconds per demo

    CLO_FE_UNJOINED,    // Load Scrooby FE resources as un-joined files.
    CLO_FE_GAGS_TEST,   // Test FE main menu gags.
    CLO_NO_SPLASH,      // Disables the splash screen
    CLO_SKIP_FE,        // Skip FE upon boot-up
    CLO_LANG_PROMPT,    // Force language selection prompt at boot-up. (PAL only)
    CLO_SKIP_MEMCHECK,  // Skip memory card boot-up check.
    CLO_SHOW_SPEED,     // Show Speedometer(s) on In-game HUD
    CLO_NO_HUD,         // No Heads-Up-Display
    CLO_NO_TUTORIAL,    // No Tutorial Pop-Up Messages
    CLO_COINS,          // Starts game w/ 100 coins

    CLO_DEBUGBV,        // Display Debug Bounding Volumes.
    CLO_SKIP_SUNDAY,    // Jump into the mission, skip sunday drive
    CLO_NO_TRAFFIC,     // Disable traffic ya!
    CLO_FPS,            // Display render stats always
    CLO_DESIGNER,       // Designer-optimised tune parameter
    CLO_DETECT_LEAKS,   // Enables printout of all the memory leaks in the 
    CLO_NO_HEAPS,       // Don't use radcore heaps - it's easier to track leaks without them
    CLO_PRINT_MEMORY,   // Print memory information to the TTY every few frames
    CLO_NO_HAPTIC,      // Disables controller rumble
    CLO_PARKED_CAR_TEST,// Place parked cars on all locators (bye, bye framerate)
    CLO_NO_AVRIL,       // Disable licensed music
    CLO_PRINT_LOAD_TIME,// print the time taken on the loading screen
    CLO_PRINT_FRAMERATE,// print the framerate after a few seconds of running

    CLO_SHOW_DYNA_ZONES,// renders all the dyna-load zones as white boxes
    
    CLO_MANUAL_RESET_DAMAGE,    // will reset the cars damage state when the user does a manual reset

    CLO_NO_PEDS,

    CLO_WINDOW_MODE,    // windowed mode (versus fullscreen) for windows.

    CLO_PROGRESSIVE_SCAN,
    CLO_LARGEHEAPS,

    CLO_MEMCARD_CHEAT,  // unlock everything in the game temporarily before saving data to memory card
    
    CLO_PS2_TOOL,
    CLO_FILE_NOT_FOUND,     //Testing for file not found
    CLO_NO_LOADING_SPEW,    // Don't Spew <<START>>, <<END>> messages
    CLO_AUDIO_LOADING_SPEW, // Spew Audio loading info
    CLO_START_IN_CAR,       // Linux port: with skipfe, seat the player in the mission's car once the level is up
    CLO_RUNTIME_DEMO,       // Linux port: with skipfe, run level l<N>'s attract demo (AI drives the car), no time limit
    CLO_UNLOCK_BONUS,       // Linux port: Bonus Game and all its tracks selectable without completed card decks
    CLO_NO_FAIL,            // Linux port test option: mission conditions never fail a stage, the mission timer stops at 0
    CLO_CONSOLE,            // Linux port: the developer console (` key; code/console/devconsole.cpp), also SHAR_CONSOLE=1

    NUM_CLO             // Must not exceed 64
};

//-----------------------------------------------------------------------------
//
// Synopsis: Any user specified command line options are stored here.
//
//-----------------------------------------------------------------------------
class CommandLineOptions
{
    public:

        //
        // Initialize default command-line options.
        //
        static void InitDefaults();

        //
        // Interpret the command line string token.
        //
        static void HandleOption( const char* const optionIn );

        //
        // Retrive the specified option.
        //
        static bool Get( CmdLineOptionEnum eOption );

        inline static short GetDefaultLevel() { return s_defaultLevel; }
        inline static short GetDefaultMission() { return s_defaultMission; }
        // Linux port: "race<N>" with skipfe: start the level's bonus mission N-1 (1-3 street races, 4 wager race)
        inline static short GetDefaultRace() { return s_defaultRace; }
        // Linux port: "tp<N>" with skipfe: teleport the player to the level's teleport destination N (the
        // AddTeleportDest list in the level's level.mfk, from 1) once the level is up
        inline static short GetDefaultTeleport() { return s_defaultTeleport; }
        // Linux port test option: "autopass[<N>]": each mission stage counts as completed once it has run N seconds
        // of game time (default 30), so the mission goes through all its stages; 0 = off
        inline static short GetAutoPassSeconds() { return s_autoPassSeconds; }

    private:

        // Declared but not defined to prevent copying and assignment.
        CommandLineOptions( const CommandLineOptions& );
        CommandLineOptions& operator=( const CommandLineOptions& );

        static simpsonsUInt64 sOptions;

        // default level and mission to load for 'skipfe' option
        //
        static short s_defaultLevel;
        static short s_defaultMission;
        static short s_defaultRace;
        static short s_defaultTeleport;
        static short s_autoPassSeconds;

};

#endif // COMMANDLINEOPTIONS_H
