// This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
// was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

//=============================================================================
// Linux port: the developer console. A drop-down console in the style of Source engine games, enabled by the
// "console" game option or SHAR_CONSOLE=1 and opened with the ` key. Its own commands (help lists them) and, as a
// fallback, the script functions mission scripts use (Console::Evaluate), Source style ("SetMaxTraffic 5") or as
// script ("SetMaxTraffic(5);").
//=============================================================================

#ifndef DEVCONSOLE_H
#define DEVCONSOLE_H

namespace DevConsole
{
    bool IsEnabled();

    // Called for every SDL_Event (linuxplatform_sdl.cpp PumpEvents); true: the console took it (keyboard, text
    // and mouse events while open, the toggle key always)
    bool HandleEvent( const void* sdlEvent );

    // Once per PumpEvents: gives the keyboard back to the game once the keys held at closing are released
    void Update();

    // Draws the console over the frame (RenderManager::ContextUpdate, before EndFrame)
    void Render();

    // Adds a line (or lines) to the console's scrollback, also printed on stderr
    void Print( const char* fmt, ... );
}

#endif // DEVCONSOLE_H
