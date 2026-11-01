//=============================================================================
// Linux port: the developer console (devconsole.h). Text is DejaVu Sans Mono (port/third_party/dejavu), rasterised
// with stb_truetype into a texture at the window's pixel size, so it is sharp at any resolution, works in release
// builds (pddiRenderContext::DrawString is debug only) and on both renderers, and padded columns line up.
//=============================================================================

#include <SDL2/SDL.h>
#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <radcontroller.hpp>
#include <p3d/utility.hpp>
#include <p3d/matrixstack.hpp>
#include <pddi/pddi.hpp>

// one translation unit uses it: static functions, libc heap (a few KB while the atlas is built)
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_malloc( size, user ) ( (void) ( user ), malloc( size ) )
#define STBTT_free( p, user ) ( (void) ( user ), free( p ) )
#include <stb_truetype.h>
#include <dejavusansmono_ascii.h>     // kDejaVuSansMonoAscii: the font file, printable ASCII only

#include <cheats/cheatinputsystem.h>
#include <console/console.h>
#include <console/devconsole.h>
#include <main/commandlineoptions.h>
#include <worldsim/character/character.h>
#include <worldsim/character/charactermanager.h>
#include <worldsim/coins/coinmanager.h>

namespace
{

enum
{
    LINE_LENGTH = 160,
    SCROLLBACK_LINES = 512,
    HISTORY_LINES = 32,
    MAX_ARGS = 16,
    ARG_LENGTH = 128
};

int s_enabled = -1;                         // -1: not read yet
bool s_open = false;
bool s_inputBlocked = false;                // radControllerSDLSetInputBlocked (stays on after closing, see Update)
bool s_greeted = false;

char s_lines[SCROLLBACK_LINES][LINE_LENGTH];
bool s_lineWarning[SCROLLBACK_LINES];
int s_lineCount = 0;                        // lines ever added; s_lines is a ring
int s_scroll = 0;                           // lines scrolled back from the newest

char s_input[LINE_LENGTH];
int s_inputLength = 0;
int s_cursor = 0;

char s_history[HISTORY_LINES][LINE_LENGTH];
int s_historyCount = 0;
int s_historyIndex = -1;                    // -1: editing a new line

pddiShader* s_backgroundShader = NULL;

void ReleaseFont();

void AddLine( const char* text, bool warning )
{
    char* line = s_lines[s_lineCount % SCROLLBACK_LINES];
    strncpy( line, text, LINE_LENGTH - 1 );
    line[LINE_LENGTH - 1] = '\0';
    s_lineWarning[s_lineCount % SCROLLBACK_LINES] = warning;
    s_lineCount++;
    if( s_scroll > 0 )
    {
        s_scroll++;                         // keep the view where it is while scrolled back
    }
}

// Splits on newlines, drops carriage returns
void AddText( const char* text, bool warning )
{
    char line[LINE_LENGTH];
    int length = 0;
    for( const char* c = text; ; c++ )
    {
        if( *c == '\n' || *c == '\0' )
        {
            line[length] = '\0';
            if( length > 0 || *c == '\n' )
            {
                AddLine( line, warning );
            }
            length = 0;
            if( *c == '\0' || c[1] == '\0' )
            {
                break;
            }
        }
        else if( *c != '\r' && length < LINE_LENGTH - 1 )
        {
            line[length++] = ( *c == '\t' ) ? ' ' : *c;
        }
    }
}

// Console::Printf / Errorf (script errors, Echo)
void OnConsoleOutput( const char* text, bool warning )
{
    AddText( text, warning );
}

void Init()
{
    if( s_enabled >= 0 )
    {
        return;
    }
    const char* env = getenv( "SHAR_CONSOLE" );
    s_enabled = ( ( env != NULL && atoi( env ) != 0 ) || CommandLineOptions::Get( CLO_CONSOLE ) ) ? 1 : 0;
    if( s_enabled )
    {
        Console::SetOutputHook( OnConsoleOutput );
    }
}

void SetInputBlocked( bool on )
{
    s_inputBlocked = on;
    radControllerSDLSetInputBlocked( on );
}

void Open( bool open )
{
    if( open == s_open )
    {
        return;
    }
    s_open = open;
    if( open )
    {
        SetInputBlocked( true );
        SDL_StartTextInput();
        if( !s_greeted )
        {
            s_greeted = true;
            DevConsole::Print( "Developer console. \"help\" lists the commands, \"find <text>\" searches them." );
        }
    }
    else
    {
        SDL_StopTextInput();                // the keyboard comes back once nothing is held (Update)
        if( s_backgroundShader != NULL )
        {
            s_backgroundShader->Release();
            s_backgroundShader = NULL;
        }
        ReleaseFont();
    }
}

//-----------------------------------------------------------------------------
// Commands
//-----------------------------------------------------------------------------

typedef void (*CommandFunction)( int argc, char** argv );

struct Command
{
    const char* name;
    CommandFunction function;
    const char* usage;
    const char* help;
};

void CmdHelp( int argc, char** argv );
void CmdFind( int argc, char** argv );

Character* GetPlayer()
{
    CharacterManager* characters = GetCharacterManager();
    return characters != NULL ? characters->GetCharacter( 0 ) : NULL;
}

void CmdClear( int, char** )
{
    s_lineCount = 0;
    s_scroll = 0;
}

void CmdEcho( int argc, char** argv )
{
    char text[LINE_LENGTH] = "";
    for( int i = 1; i < argc; i++ )
    {
        if( i > 1 )
        {
            strncat( text, " ", sizeof( text ) - strlen( text ) - 1 );
        }
        strncat( text, argv[i], sizeof( text ) - strlen( text ) - 1 );
    }
    DevConsole::Print( "%s", text );
}

void CmdQuit( int, char** )
{
    SDL_Event quit;
    memset( &quit, 0, sizeof( quit ) );
    quit.type = SDL_QUIT;
    SDL_PushEvent( &quit );
}

struct CheatName
{
    const char* name;
    eCheatID id;
};

const CheatName s_cheats[] =
{
    { "unlockcards", CHEAT_ID_UNLOCK_CARDS },
    { "unlockskins", CHEAT_ID_UNLOCK_SKINS },
    { "unlockmissions", CHEAT_ID_UNLOCK_MISSIONS },
    { "unlockmovies", CHEAT_ID_UNLOCK_MOVIES },
    { "unlockvehicles", CHEAT_ID_UNLOCK_VEHICLES },
    { "unlockall", CHEAT_ID_UNLOCK_EVERYTHING },
    { "notopspeed", CHEAT_ID_NO_TOP_SPEED },
    { "highaccel", CHEAT_ID_HIGH_ACCELERATION },
    { "hornjump", CHEAT_ID_CAR_JUMP_ON_HORN },
    { "onehitwreck", CHEAT_ID_FULL_DAMAGE_TO_CAR },
    { "onetaptraffic", CHEAT_ID_ONE_TAP_TRAFFIC_DEATH },
    { "extratime", CHEAT_ID_EXTRA_TIME },
    { "showpos", CHEAT_ID_SHOW_AVATAR_POSITION },
    { "kickmodel", CHEAT_ID_KICK_TOGGLES_CHARACTER_MODEL },
    { "extracoins", CHEAT_ID_EXTRA_COINS },
    { "speedcam", CHEAT_ID_SPEED_CAM },
    { "speedometer", CHEAT_ID_SHOW_SPEEDOMETER },
    { "redbrick", CHEAT_ID_REDBRICK },
    { "invincible", CHEAT_ID_INVINCIBLE_CAR },
    { "showtree", CHEAT_ID_SHOW_TREE },
    { "trippy", CHEAT_ID_TRIPPY },
};

void CmdCheat( int argc, char** argv )
{
    CheatInputSystem* cheats = GetCheatInputSystem();
    if( cheats == NULL )
    {
        DevConsole::Print( "Cheats are not available yet" );
        return;
    }
    if( argc < 2 )
    {
        for( unsigned int i = 0; i < sizeof( s_cheats ) / sizeof( s_cheats[0] ); i++ )
        {
            DevConsole::Print( "  %-16s %s", s_cheats[i].name, cheats->IsCheatEnabled( s_cheats[i].id ) ? "on" : "off" );
        }
        return;
    }
    for( unsigned int i = 0; i < sizeof( s_cheats ) / sizeof( s_cheats[0] ); i++ )
    {
        if( strcasecmp( s_cheats[i].name, argv[1] ) == 0 )
        {
            bool on = argc > 2 ? atoi( argv[2] ) != 0 : !cheats->IsCheatEnabled( s_cheats[i].id );
            cheats->SetCheatEnabled( s_cheats[i].id, on );
            // the unlock cheats need every story mission done in final builds (SetCheatEnabled ignores them)
            DevConsole::Print( "%s %s", s_cheats[i].name, cheats->IsCheatEnabled( s_cheats[i].id ) ? "on" : "off" );
            return;
        }
    }
    DevConsole::Print( "No cheat \"%s\" (\"cheat\" lists them)", argv[1] );
}

void CmdCoins( int argc, char** argv )
{
    CoinManager* coins = GetCoinManager();
    if( coins == NULL )
    {
        DevConsole::Print( "No coins outside a level" );
        return;
    }
    if( argc > 1 )
    {
        coins->AdjustBankValue( atoi( argv[1] ) );
    }
    DevConsole::Print( "%d coins", coins->GetBankValue() );
}

void CmdPos( int, char** )
{
    Character* player = GetPlayer();
    if( player == NULL )
    {
        DevConsole::Print( "No player outside a level" );
        return;
    }
    rmt::Vector position;
    player->GetPosition( position );
    DevConsole::Print( "%.2f %.2f %.2f%s", position.x, position.y, position.z, player->IsInCar() ? " (in a car)" : "" );
}

void CmdTeleport( int argc, char** argv )
{
    Character* player = GetPlayer();
    if( player == NULL )
    {
        DevConsole::Print( "No player outside a level" );
        return;
    }
    unsigned int count = CharacterManager::GetNumTeleportDests();
    if( argc == 1 )
    {
        for( unsigned int i = 0; i < count; i++ )
        {
            DevConsole::Print( "  %2u %s", i, CharacterManager::GetTeleportDest( i ) );
        }
        DevConsole::Print( "tp <number or name> goes to one, tp <x> <y> <z> to a point (\"pos\" shows yours)" );
        return;
    }
    char* end = NULL;
    float xyz[3];
    int numbers = 0;
    for( int i = 1; i < argc && i <= 3; i++ )
    {
        xyz[i - 1] = strtof( argv[i], &end );
        numbers += ( end != argv[i] && *end == '\0' ) ? 1 : 0;
    }
    if( argc == 4 && numbers == 3 )
    {
        if( player->IsInCar() )
        {
            DevConsole::Print( "Get out of the car first" );
            return;
        }
        player->RelocateAndReset( rmt::Vector( xyz[0], xyz[1], xyz[2] ), 0 );
        return;
    }
    // a number, or a name (the words after tp: names have spaces)
    char name[LINE_LENGTH] = "";
    for( int i = 1; i < argc; i++ )
    {
        if( i > 1 )
        {
            strncat( name, " ", sizeof( name ) - strlen( name ) - 1 );
        }
        strncat( name, argv[i], sizeof( name ) - strlen( name ) - 1 );
    }
    unsigned long which = strtoul( name, &end, 10 );
    if( end == name || *end != '\0' )
    {
        for( which = 0; which < count; which++ )
        {
            if( strcasecmp( CharacterManager::GetTeleportDest( which ), name ) == 0 )
            {
                break;
            }
        }
    }
    if( which >= count )
    {
        DevConsole::Print( "No destination \"%s\" (\"tp\" lists the %u here)", name, count );
        return;
    }
    DevConsole::Print( "Teleporting to %s", CharacterManager::GetTeleportDest( which ) );
    CharacterManager::Teleport( which );
}

void RunScript( const char* script )
{
    Console* console = GetConsole();
    if( console == NULL )
    {
        DevConsole::Print( "The script console is not up yet" );
        return;
    }
    char buffer[LINE_LENGTH + 2];
    snprintf( buffer, sizeof( buffer ), "%s", script );
    size_t length = strlen( buffer );
    while( length > 0 && isspace( (unsigned char) buffer[length - 1] ) )
    {
        buffer[--length] = '\0';
    }
    if( length > 0 && buffer[length - 1] != ';' && buffer[length - 1] != '}' )
    {
        strcat( buffer, ";" );              // Name(args) without the semicolon
    }
    console->Evaluate( buffer, "console" );
}

void CmdScript( int, char** )
{
    // Execute passes "script <text>" to RunScript as typed (quotes kept); this only sees a bare "script"
    DevConsole::Print( "script <text>, e.g. script SetMaxTraffic(5)" );
}

void Execute( const char* line );

void CmdExec( int argc, char** argv )
{
    if( argc < 2 )
    {
        DevConsole::Print( "exec <file>" );
        return;
    }
    FILE* file = fopen( argv[1], "r" );     // relative to the game directory (the working directory)
    if( file == NULL )
    {
        DevConsole::Print( "Can't open %s", argv[1] );
        return;
    }
    char line[LINE_LENGTH];
    while( fgets( line, sizeof( line ), file ) != NULL )
    {
        line[strcspn( line, "\r\n" )] = '\0';
        if( line[0] != '\0' && strncmp( line, "//", 2 ) != 0 && line[0] != '#' )
        {
            Execute( line );
        }
    }
    fclose( file );
}

const Command s_commands[] =
{
    { "help", CmdHelp, "[command]", "lists the commands, or describes one" },
    { "find", CmdFind, "<text>", "lists the commands and script functions containing text" },
    { "clear", CmdClear, "", "empties the console" },
    { "echo", CmdEcho, "<text>", "prints text" },
    { "cheat", CmdCheat, "[name] [0|1]", "lists the cheats, or turns one on/off" },
    { "coins", CmdCoins, "[amount]", "shows the coins, or adds (negative: takes away) some" },
    { "pos", CmdPos, "", "shows the player's position" },
    { "tp", CmdTeleport, "[dest | x y z]", "lists the level's destinations, or teleports" },
    { "script", CmdScript, "<script>", "runs script text, e.g. script SetMaxTraffic(5)" },
    { "exec", CmdExec, "<file>", "runs a file of console lines (game directory)" },
    { "quit", CmdQuit, "", "quits the game" },
};

const int s_numCommands = sizeof( s_commands ) / sizeof( s_commands[0] );

const Command* FindCommand( const char* name )
{
    for( int i = 0; i < s_numCommands; i++ )
    {
        if( strcasecmp( s_commands[i].name, name ) == 0 )
        {
            return &s_commands[i];
        }
    }
    return NULL;
}

void CmdHelp( int argc, char** argv )
{
    if( argc > 1 )
    {
        const Command* command = FindCommand( argv[1] );
        int minArgs = 0, maxArgs = 0;
        if( command != NULL )
        {
            DevConsole::Print( "%s %s: %s", command->name, command->usage, command->help );
        }
        else if( GetConsole() != NULL && GetConsole()->GetFunctionArgs( argv[1], &minArgs, &maxArgs ) )
        {
            DevConsole::Print( "%s: script function, %d to %d arguments", argv[1], minArgs, maxArgs );
        }
        else
        {
            DevConsole::Print( "No command \"%s\"", argv[1] );
        }
        return;
    }
    for( int i = 0; i < s_numCommands; i++ )
    {
        DevConsole::Print( "  %-8s %-20s %s", s_commands[i].name, s_commands[i].usage, s_commands[i].help );
    }
    DevConsole::Print( "Script functions too, as \"Name arg arg\" or \"Name(arg, arg);\" (\"find\" lists them). Most "
                       "are for loading mission scripts: called mid-game they can misbehave." );
    DevConsole::Print( "Keys: ` or Esc closes, Tab completes, Up/Down history, PgUp/PgDn scroll." );
}

void CmdFind( int argc, char** argv )
{
    const char* text = argc > 1 ? argv[1] : "";
    int found = 0;
    for( int i = 0; i < s_numCommands; i++ )
    {
        if( strcasestr( s_commands[i].name, text ) != NULL )
        {
            DevConsole::Print( "  %s %s", s_commands[i].name, s_commands[i].usage );
            found++;
        }
    }
    Console* console = GetConsole();
    for( int i = 0; console != NULL && i < console->GetFunctionCount(); i++ )
    {
        const char* name = console->GetFunctionName( i );
        int minArgs = 0, maxArgs = 0;
        if( strcasestr( name, text ) != NULL && console->GetFunctionArgs( name, &minArgs, &maxArgs ) )
        {
            DevConsole::Print( "  %s (script, %d-%d args)", name, minArgs, maxArgs );
            found++;
        }
    }
    DevConsole::Print( "%d found", found );
}


// Splits a line into words; "double quotes" group words. Returns the word count.
int Tokenize( const char* line, char argv[MAX_ARGS][ARG_LENGTH] )
{
    int argc = 0;
    const char* c = line;
    while( argc < MAX_ARGS )
    {
        while( isspace( (unsigned char) *c ) )
        {
            c++;
        }
        if( *c == '\0' )
        {
            break;
        }
        int length = 0;
        bool quoted = ( *c == '"' );
        if( quoted )
        {
            c++;
        }
        while( *c != '\0' && ( quoted ? *c != '"' : !isspace( (unsigned char) *c ) ) )
        {
            if( length < ARG_LENGTH - 1 )
            {
                argv[argc][length++] = *c;
            }
            c++;
        }
        if( quoted && *c == '"' )
        {
            c++;
        }
        argv[argc][length] = '\0';
        argc++;
    }
    return argc;
}

void Execute( const char* line )
{
    while( isspace( (unsigned char) *line ) )
    {
        line++;
    }
    if( *line == '\0' )
    {
        return;
    }
    if( strncasecmp( line, "script", 6 ) == 0 && isspace( (unsigned char) line[6] ) )
    {
        RunScript( line + 7 );
        return;
    }
    if( strchr( line, '(' ) != NULL )
    {
        RunScript( line );                  // script syntax: Name(args);
        return;
    }

    static char words[MAX_ARGS][ARG_LENGTH];
    char* argv[MAX_ARGS];
    int argc = Tokenize( line, words );
    for( int i = 0; i < argc; i++ )
    {
        argv[i] = words[i];
    }

    const Command* command = FindCommand( argv[0] );
    if( command != NULL )
    {
        command->function( argc, argv );
        return;
    }

    // a script function, Source style: Name arg arg -> Name("arg","arg");
    Console* console = GetConsole();
    int minArgs = 0, maxArgs = 0;
    if( console != NULL && console->GetFunctionArgs( argv[0], &minArgs, &maxArgs ) )
    {
        if( argc - 1 < minArgs || argc - 1 > maxArgs )
        {
            DevConsole::Print( "%s takes %d to %d arguments", argv[0], minArgs, maxArgs );
            return;
        }
        char script[MAX_ARGS * ( ARG_LENGTH + 3 ) + 8];
        int length = snprintf( script, sizeof( script ), "%s(", argv[0] );
        for( int i = 1; i < argc; i++ )
        {
            length += snprintf( script + length, sizeof( script ) - length, "%s\"%s\"", i > 1 ? "," : "", argv[i] );
        }
        snprintf( script + length, sizeof( script ) - length, ");" );
        console->Evaluate( script, "console" );
        return;
    }
    DevConsole::Print( "Unknown command \"%s\" (\"help\" lists them)", argv[0] );
}

//-----------------------------------------------------------------------------
// Line editing
//-----------------------------------------------------------------------------

void SetInput( const char* text )
{
    strncpy( s_input, text, LINE_LENGTH - 1 );
    s_input[LINE_LENGTH - 1] = '\0';
    s_inputLength = (int) strlen( s_input );
    s_cursor = s_inputLength;
}

void Submit()
{
    DevConsole::Print( "] %s", s_input );
    if( s_inputLength > 0 &&
        ( s_historyCount == 0 || strcmp( s_history[( s_historyCount - 1 ) % HISTORY_LINES], s_input ) != 0 ) )
    {
        strcpy( s_history[s_historyCount % HISTORY_LINES], s_input );
        s_historyCount++;
    }
    s_historyIndex = -1;
    s_scroll = 0;
    char line[LINE_LENGTH];
    strcpy( line, s_input );
    SetInput( "" );
    Execute( line );
}

void History( int step )
{
    int oldest = s_historyCount > HISTORY_LINES ? s_historyCount - HISTORY_LINES : 0;
    int index = s_historyIndex < 0 ? s_historyCount : s_historyIndex;
    index += step;
    if( index < oldest )
    {
        return;
    }
    if( index >= s_historyCount )
    {
        s_historyIndex = -1;
        SetInput( "" );
        return;
    }
    s_historyIndex = index;
    SetInput( s_history[index % HISTORY_LINES] );
}

// Tab: completes the first word from the commands and script functions; several matches are listed and
// completed as far as they agree
void Complete()
{
    if( strchr( s_input, ' ' ) != NULL || strchr( s_input, '(' ) != NULL )
    {
        return;
    }
    const char* prefix = s_input;
    size_t prefixLength = strlen( prefix );
    char common[LINE_LENGTH] = "";
    int matches = 0;
    Console* console = GetConsole();
    int total = s_numCommands + ( console != NULL ? console->GetFunctionCount() : 0 );
    for( int pass = 0; pass < 2; pass++ )
    {
        for( int i = 0; i < total; i++ )
        {
            const char* name = i < s_numCommands ? s_commands[i].name : console->GetFunctionName( i - s_numCommands );
            if( name == NULL || strncasecmp( name, prefix, prefixLength ) != 0 )
            {
                continue;
            }
            if( pass == 0 )
            {
                if( matches == 0 )
                {
                    snprintf( common, sizeof( common ), "%s", name );
                }
                else
                {
                    size_t n = 0;
                    while( common[n] != '\0' && tolower( (unsigned char) common[n] ) == tolower( (unsigned char) name[n] ) )
                    {
                        n++;
                    }
                    common[n] = '\0';
                }
                matches++;
            }
            else if( matches > 1 )
            {
                DevConsole::Print( "  %s", name );
            }
        }
    }
    if( matches == 1 )
    {
        strncat( common, " ", sizeof( common ) - strlen( common ) - 1 );
    }
    if( matches > 0 && strlen( common ) >= prefixLength )
    {
        SetInput( common );
    }
}

void Insert( const char* text )
{
    for( const char* c = text; *c != '\0'; c++ )
    {
        // printable ASCII only (the font has nothing else); the toggle key's characters never go in
        if( *c < 32 || *c > 126 || *c == '`' || *c == '~' || s_inputLength >= LINE_LENGTH - 1 )
        {
            continue;
        }
        memmove( s_input + s_cursor + 1, s_input + s_cursor, s_inputLength - s_cursor + 1 );
        s_input[s_cursor++] = *c;
        s_inputLength++;
    }
}

void Erase( int at )
{
    if( at < 0 || at >= s_inputLength )
    {
        return;
    }
    memmove( s_input + at, s_input + at + 1, s_inputLength - at );
    s_inputLength--;
    if( s_cursor > at )
    {
        s_cursor--;
    }
}

void HandleKey( const SDL_KeyboardEvent& key )
{
    int visible = 10;                       // lines a PgUp/PgDn scrolls
    bool ctrl = ( key.keysym.mod & KMOD_CTRL ) != 0;
    switch( key.keysym.sym )
    {
    case SDLK_ESCAPE:       Open( false ); break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER:     Submit(); break;
    case SDLK_BACKSPACE:    Erase( s_cursor - 1 ); break;
    case SDLK_DELETE:       Erase( s_cursor ); break;
    case SDLK_LEFT:         if( s_cursor > 0 ) s_cursor--; break;
    case SDLK_RIGHT:        if( s_cursor < s_inputLength ) s_cursor++; break;
    case SDLK_HOME:         s_cursor = 0; break;
    case SDLK_END:          s_cursor = s_inputLength; break;
    case SDLK_UP:           History( -1 ); break;
    case SDLK_DOWN:         History( 1 ); break;
    case SDLK_TAB:          Complete(); break;
    case SDLK_PAGEUP:       s_scroll += visible; break;
    case SDLK_PAGEDOWN:     s_scroll = s_scroll > visible ? s_scroll - visible : 0; break;
    case SDLK_l:            if( ctrl ) CmdClear( 0, NULL ); break;
    case SDLK_u:            if( ctrl ) SetInput( "" ); break;
    default:                break;
    }
    int maxScroll = ( s_lineCount < SCROLLBACK_LINES ? s_lineCount : SCROLLBACK_LINES ) - 1;
    if( s_scroll > maxScroll )
    {
        s_scroll = maxScroll > 0 ? maxScroll : 0;
    }
}

//-----------------------------------------------------------------------------
// Drawing
//-----------------------------------------------------------------------------

// The font: every printable ASCII glyph rasterised at the pixel size the window needs into one atlas texture (white,
// coverage in alpha), rebuilt when the size changes, released on close. Monospace: one advance, rounded to whole
// pixels, per character, and glyphs at whole pixels, so text stays sharp and padded columns line up.
enum
{
    FIRST_CHAR = 32,
    CHAR_COUNT = 95,
    ATLAS_COLUMNS = 16
};

struct AtlasGlyph
{
    int x, y, width, height;    // in the atlas
    int offsetX, offsetY;       // from the cell's left edge and the baseline
};

struct Font
{
    pddiTexture* texture;
    pddiShader* shader;
    int pixelHeight;            // what it was built for (0: not built)
    int advance;
    int ascent;
    int lineHeight;
    float atlasScale;           // 1 / the atlas size
    AtlasGlyph glyphs[CHAR_COUNT];
};

Font s_font;

bool Printable( unsigned char c )
{
    return c >= FIRST_CHAR && c < FIRST_CHAR + CHAR_COUNT;
}

void ReleaseFont()
{
    if( s_font.shader != NULL )
    {
        s_font.shader->Release();
    }
    if( s_font.texture != NULL )
    {
        s_font.texture->Release();
    }
    memset( &s_font, 0, sizeof( s_font ) );
}

bool BuildFont( int pixelHeight )
{
    ReleaseFont();
    s_font.pixelHeight = pixelHeight;           // also when it fails: not retried every frame
    stbtt_fontinfo info;
    if( !stbtt_InitFont( &info, kDejaVuSansMonoAscii, 0 ) )
    {
        return false;
    }
    float scale = stbtt_ScaleForPixelHeight( &info, (float) pixelHeight );
    int ascent, descent, lineGap, advance, bearing;
    stbtt_GetFontVMetrics( &info, &ascent, &descent, &lineGap );
    stbtt_GetCodepointHMetrics( &info, 'M', &advance, &bearing );
    s_font.ascent = (int) ceilf( ascent * scale );
    s_font.lineHeight = s_font.ascent + (int) ceilf( ( lineGap - descent ) * scale );
    s_font.advance = (int) ( advance * scale + 0.5f );

    // a grid of cells the size of the largest glyph, a pixel apart (bilinear filtering never reaches a neighbour)
    int cellWidth = 1, cellHeight = 1;
    for( int c = 0; c < CHAR_COUNT; c++ )
    {
        int x0, y0, x1, y1;
        stbtt_GetCodepointBitmapBox( &info, FIRST_CHAR + c, scale, scale, &x0, &y0, &x1, &y1 );
        AtlasGlyph& glyph = s_font.glyphs[c];
        glyph.width = x1 - x0;
        glyph.height = y1 - y0;
        glyph.offsetX = x0;
        glyph.offsetY = y0;
        cellWidth = glyph.width > cellWidth ? glyph.width : cellWidth;
        cellHeight = glyph.height > cellHeight ? glyph.height : cellHeight;
    }
    cellWidth++;
    cellHeight++;
    int rows = ( CHAR_COUNT + ATLAS_COLUMNS - 1 ) / ATLAS_COLUMNS;
    int size = 64;
    while( size < ATLAS_COLUMNS * cellWidth + 1 || size < rows * cellHeight + 1 )
    {
        size *= 2;
    }
    s_font.atlasScale = 1.0f / (float) size;

    pddiTextureDesc desc;
    desc.SetSizeX( size );
    desc.SetSizeY( size );
    desc.SetBitDepth( 32 );
    desc.SetAlphaDepth( 8 );
    desc.SetMipMapCount( 0 );
    desc.SetType( PDDI_TEXTYPE_RGB );
    s_font.texture = p3d::device->NewTexture( &desc );
    unsigned char* coverage = (unsigned char*) malloc( cellWidth * cellHeight );
    if( s_font.texture == NULL || coverage == NULL )
    {
        fprintf( stderr, "console: no %dx%d font texture\n", size, size );
        free( coverage );
        return false;
    }
    s_font.texture->AddRef();

    pddiLockInfo* lock = s_font.texture->Lock( 0, NULL );
    PDDI_U32 clear = lock->MakeColour( 0x00ffffff );
    for( int y = 0; y < lock->height; y++ )
    {
        PDDI_U32* row = (PDDI_U32*) ( (char*) lock->bits + y * lock->pitch );
        for( int x = 0; x < lock->width; x++ )
        {
            row[x] = clear;
        }
    }
    for( int c = 0; c < CHAR_COUNT; c++ )
    {
        AtlasGlyph& glyph = s_font.glyphs[c];
        glyph.x = 1 + ( c % ATLAS_COLUMNS ) * cellWidth;
        glyph.y = 1 + ( c / ATLAS_COLUMNS ) * cellHeight;
        if( glyph.width <= 0 || glyph.height <= 0 )
        {
            continue;
        }
        stbtt_MakeCodepointBitmap( &info, coverage, glyph.width, glyph.height, glyph.width, scale, scale,
                                   FIRST_CHAR + c );
        for( int y = 0; y < glyph.height; y++ )
        {
            PDDI_U32* row = (PDDI_U32*) ( (char*) lock->bits + ( glyph.y + y ) * lock->pitch ) + glyph.x;
            for( int x = 0; x < glyph.width; x++ )
            {
                row[x] = lock->MakeColour( ( (PDDI_U32) coverage[y * glyph.width + x] << 24 ) | 0x00ffffff );
            }
        }
    }
    s_font.texture->Unlock( 0 );
    free( coverage );

    s_font.shader = p3d::device->NewShader( "simple" );
    s_font.shader->AddRef();
    s_font.shader->SetTexture( PDDI_SP_BASETEX, s_font.texture );
    s_font.shader->SetInt( PDDI_SP_SHADEMODE, PDDI_SHADE_FLAT );
    s_font.shader->SetInt( PDDI_SP_BLENDMODE, PDDI_BLEND_ALPHA );
    s_font.shader->SetInt( PDDI_SP_FILTER, PDDI_FILTER_BILINEAR );
    s_font.shader->SetInt( PDDI_SP_UVMODE, PDDI_UV_CLAMP );
    return true;
}

// Width in pixels of the first length characters
float TextWidth( const char* text, int length )
{
    int cells = 0;
    for( int i = 0; i < length && text[i] != '\0'; i++ )
    {
        cells += Printable( (unsigned char) text[i] ) ? 1 : 0;
    }
    return (float) ( cells * s_font.advance );
}

// One line of text, its top at y
void DrawText( const char* text, float x, float y, float z, pddiColour colour )
{
    if( s_font.shader == NULL )
    {
        return;
    }
    int quads = 0;
    for( const unsigned char* s = (const unsigned char*) text; *s; s++ )
    {
        quads += Printable( *s ) && s_font.glyphs[*s - FIRST_CHAR].width > 0 ? 1 : 0;
    }
    if( quads == 0 )
    {
        return;
    }

    pddiPrimStream* stream = p3d::pddi->BeginPrims( s_font.shader, PDDI_PRIM_TRIANGLES, PDDI_V_CT, quads * 6 );
    int cellX = (int) x;
    int baseline = (int) y + s_font.ascent;
    for( const unsigned char* s = (const unsigned char*) text; *s; s++ )
    {
        if( !Printable( *s ) )
        {
            continue;
        }
        const AtlasGlyph& glyph = s_font.glyphs[*s - FIRST_CHAR];
        if( glyph.width > 0 )
        {
            float x0 = (float) ( cellX + glyph.offsetX ), x1 = x0 + glyph.width;
            float y0 = (float) ( baseline + glyph.offsetY ), y1 = y0 + glyph.height;
            float u0 = glyph.x * s_font.atlasScale, u1 = ( glyph.x + glyph.width ) * s_font.atlasScale;
            // v runs bottom-up: Lock's first row (negative pitch) is the texture's last
            float v0 = 1.0f - glyph.y * s_font.atlasScale, v1 = 1.0f - ( glyph.y + glyph.height ) * s_font.atlasScale;
            const float corners[6][4] = { { x0, y0, u0, v0 }, { x1, y0, u1, v0 }, { x1, y1, u1, v1 },
                                          { x0, y0, u0, v0 }, { x1, y1, u1, v1 }, { x0, y1, u0, v1 } };
            for( int i = 0; i < 6; i++ )
            {
                stream->Colour( colour );
                stream->UV( corners[i][2], corners[i][3] );
                stream->Coord( corners[i][0], corners[i][1], z );
            }
        }
        cellX += s_font.advance;
    }
    p3d::pddi->EndPrims( stream );
}

void DrawBackground( float width, float height, float z )
{
    if( s_backgroundShader == NULL )
    {
        s_backgroundShader = p3d::device->NewShader( "simple" );
        s_backgroundShader->AddRef();
        s_backgroundShader->SetInt( PDDI_SP_SHADEMODE, PDDI_SHADE_FLAT );
        s_backgroundShader->SetInt( PDDI_SP_BLENDMODE, PDDI_BLEND_ALPHA );
    }
    pddiPrimStream* stream = p3d::pddi->BeginPrims( s_backgroundShader, PDDI_PRIM_TRIANGLES, PDDI_V_C, 12 );
    pddiColour panel( 0, 0, 0, 210 );
    pddiColour edge( 255, 160, 0, 255 );
    float edgeTop = height - 2.0f;
    stream->Colour( panel ); stream->Coord( 0.0f, 0.0f, z );
    stream->Colour( panel ); stream->Coord( width, 0.0f, z );
    stream->Colour( panel ); stream->Coord( width, edgeTop, z );
    stream->Colour( panel ); stream->Coord( 0.0f, 0.0f, z );
    stream->Colour( panel ); stream->Coord( width, edgeTop, z );
    stream->Colour( panel ); stream->Coord( 0.0f, edgeTop, z );
    stream->Colour( edge ); stream->Coord( 0.0f, edgeTop, z );
    stream->Colour( edge ); stream->Coord( width, edgeTop, z );
    stream->Colour( edge ); stream->Coord( width, height, z );
    stream->Colour( edge ); stream->Coord( 0.0f, edgeTop, z );
    stream->Colour( edge ); stream->Coord( width, height, z );
    stream->Colour( edge ); stream->Coord( 0.0f, height, z );
    p3d::pddi->EndPrims( stream );
}

}   // namespace

//-----------------------------------------------------------------------------
// DevConsole
//-----------------------------------------------------------------------------

bool DevConsole::IsEnabled()
{
    Init();
    return s_enabled != 0;
}

void DevConsole::Print( const char* fmt, ... )
{
    char text[1024];
    va_list args;
    va_start( args, fmt );
    vsnprintf( text, sizeof( text ), fmt, args );
    va_end( args );
    AddText( text, false );
    fprintf( stderr, "console: %s\n", text );
}

bool DevConsole::HandleEvent( const void* sdlEvent )
{
    if( !IsEnabled() )
    {
        return false;
    }
    const SDL_Event& e = *(const SDL_Event*) sdlEvent;
    if( e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_GRAVE )
    {
        if( !e.key.repeat )
        {
            Open( !s_open );
        }
        return true;
    }
    if( !s_open )
    {
        return false;
    }
    switch( e.type )
    {
    case SDL_KEYDOWN:
        HandleKey( e.key );
        return true;
    case SDL_TEXTINPUT:
        Insert( e.text.text );
        return true;
    case SDL_MOUSEWHEEL:
        s_scroll += e.wheel.y * 3;
        if( s_scroll < 0 )
        {
            s_scroll = 0;
        }
        return true;
    case SDL_KEYUP:
    case SDL_TEXTEDITING:
    case SDL_MOUSEMOTION:
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP:
        return true;
    default:
        return false;
    }
}

void DevConsole::Update()
{
    // debug: SHAR_CONSOLE_SCRIPT="ms:line;ms:line;..." opens the console and runs those lines at those times (tests
    // commands; SHAR_KEYPRESS_SCRIPT=grave@ms closes it)
    static const char* s_script = getenv( "SHAR_CONSOLE_SCRIPT" );
    static int s_scriptDone = 0;
    int scriptIndex = 0;
    for( const char* c = s_script; IsEnabled() && c != NULL && *c != '\0'; scriptIndex++ )
    {
        unsigned int at = 0;
        int skip = 0;
        if( sscanf( c, "%u:%n", &at, &skip ) == 1 && skip > 0 && scriptIndex == s_scriptDone && SDL_GetTicks() >= at )
        {
            char line[LINE_LENGTH];
            size_t length = strcspn( c + skip, ";" );
            snprintf( line, sizeof( line ), "%.*s", (int) length, c + skip );
            Open( true );
            SetInput( line );
            Submit();
            s_scriptDone++;
        }
        c = strchr( c, ';' );
        if( c != NULL ) c++;
    }

    if( !s_inputBlocked || s_open )
    {
        return;
    }
    // closed: the game gets the keyboard and mouse back once the keys held (Esc, `) are up, else it would see them
    int count = 0;
    const Uint8* keys = SDL_GetKeyboardState( &count );
    for( int i = 0; i < count; i++ )
    {
        if( keys[i] )
        {
            return;
        }
    }
    if( SDL_GetMouseState( NULL, NULL ) == 0 )
    {
        SetInputBlocked( false );
    }
}

void DevConsole::Render()
{
    if( !s_open || p3d::pddi == NULL || p3d::display == NULL )
    {
        return;
    }
    float width = (float) p3d::display->GetWidth();
    float screenHeight = (float) p3d::display->GetHeight();
    float height = (float) (int) ( screenHeight * 0.5f );
    // 18 px at 1080 lines, no smaller than 14 px
    int pixelHeight = (int) ( screenHeight * 18.0f / 1080.0f + 0.5f );
    pixelHeight = pixelHeight < 14 ? 14 : pixelHeight;
    if( pixelHeight != s_font.pixelHeight )
    {
        BuildFont( pixelHeight );
    }
    float lineHeight = (float) s_font.lineHeight;
    float margin = (float) (int) ( pixelHeight * 0.5f );

    p3d::stack->Push();
    p3d::stack->LoadIdentity();
    pddiProjectionMode projection = p3d::pddi->GetProjectionMode();
    pddiCompareMode zCompare = p3d::pddi->GetZCompare();
    bool zWrite = p3d::pddi->GetZWrite();
    pddiRect scissor;
    p3d::pddi->GetScissor( &scissor );
    pddiRect fullScreen( 0, 0, (int) width, (int) screenHeight );
    p3d::pddi->SetScissor( &fullScreen );
    p3d::pddi->SetProjectionMode( PDDI_PROJECTION_DEVICE );
    p3d::pddi->SetCullMode( PDDI_CULL_NONE );
    p3d::pddi->SetZCompare( PDDI_COMPARE_ALWAYS );
    p3d::pddi->SetZWrite( false );
    float nearPlane, farPlane, fov, aspect;
    p3d::pddi->GetCamera( &nearPlane, &farPlane, &fov, &aspect );
    float z = nearPlane + 0.01f;

    DrawBackground( width, height, z );

    // the input line at the bottom, a caret at the cursor
    float inputY = height - margin - lineHeight;
    char prompt[LINE_LENGTH + 4];
    snprintf( prompt, sizeof( prompt ), "] %s", s_input );
    DrawText( prompt, margin, inputY, z, pddiColour( 255, 255, 255 ) );
    if( ( SDL_GetTicks() / 500 ) % 2 == 0 )
    {
        float caretX = margin + TextWidth( prompt, s_cursor + 2 );
        DrawText( "_", caretX, inputY, z, pddiColour( 255, 160, 0 ) );
    }

    // the scrollback above it, newest at the bottom
    int rows = (int) ( ( inputY - margin ) / lineHeight );
    int stored = s_lineCount < SCROLLBACK_LINES ? s_lineCount : SCROLLBACK_LINES;
    float y = inputY - lineHeight;
    if( s_scroll > 0 )
    {
        DrawText( "^ ^ ^", margin, y, z, pddiColour( 255, 160, 0 ) );
        y -= lineHeight;
        rows--;
    }
    for( int row = 0; row < rows; row++ )
    {
        int back = s_scroll + row;
        if( back >= stored )
        {
            break;
        }
        int index = ( s_lineCount - 1 - back ) % SCROLLBACK_LINES;
        pddiColour colour = s_lineWarning[index] ? pddiColour( 255, 120, 100 ) : pddiColour( 210, 210, 210 );
        DrawText( s_lines[index], margin, y, z, colour );
        y -= lineHeight;
    }

    p3d::pddi->SetZWrite( zWrite );
    p3d::pddi->SetZCompare( zCompare );
    p3d::pddi->SetProjectionMode( projection );
    p3d::pddi->SetScissor( &scissor );
    p3d::stack->Pop();
}
