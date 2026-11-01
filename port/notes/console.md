# Developer console (`code/console/devconsole.cpp`)

## Use
- On with game option `console` or `SHAR_CONSOLE=1`; ` toggles, Esc closes. Tab completes the first word, Up/Down
  history, PgUp/PgDn/wheel scroll, Ctrl+L clear, Ctrl+U clear line.
- Own commands (`s_commands`):

| command | does |
|---|---|
| `help [command]` | lists the commands, or describes one (script functions: arg range) |
| `find <text>` | lists commands and script functions whose name contains text (no text: all) |
| `clear` | empties the scrollback |
| `echo <text>` | prints text |
| `cheat [name] [0\|1]` | lists the cheats with on/off, or toggles/sets one: unlockcards, unlockskins, unlockmissions, unlockmovies, unlockvehicles, unlockall, notopspeed, highaccel, hornjump, onehitwreck, onetaptraffic, extratime, showpos, kickmodel, extracoins, speedcam, speedometer, redbrick, invincible, showtree, trippy |
| `coins [amount]` | shows the bank, or adds (negative: removes) coins (`CoinManager::AdjustBankValue`) |
| `pos` | prints the player's x y z (notes if in a car) |
| `tp` | lists the level's teleport destinations (AddTeleportDest) |
| `tp <number\|name>` | teleports there (`CharacterManager::Teleport`; names case-insensitive, may have spaces) |
| `tp <x> <y> <z>` | moves the player to a point (`RelocateAndReset`; on foot only) |
| `script <text>` | runs script text as typed, e.g. `script SetMaxTraffic(5)` |
| `exec <file>` | runs a file of console lines (game dir; `//` and `#` lines skipped) |
| `quit` | quits the game (SDL_QUIT) |

- Else script functions (`Console`'s table): `Name a b` -> `Name("a","b");` after an arg-count check
  (`Console::GetFunctionArgs`); a line with `(` (or `script ...`) goes to `Console::Evaluate` as typed (`;` added).
  Most are mission-load functions: mid-game calls can misbehave. Debug builds: script syntax errors hit `rTuneAssert`.
- `tp x y z` only with three numbers (else the words are a destination name, which has spaces); on foot only.
- `cheat`: own name table -> `CheatInputSystem::SetCheatEnabled`; FINAL ignores unlock cheats before 100% story.

## Wiring
- Events: `PumpEvents` (linuxplatform_sdl.cpp) gives every SDL_Event to `DevConsole::HandleEvent` first; open, it takes
  keyboard/text/mouse events (never controller/window). `DevConsole::Update` at the end of `PumpEvents`.
- Game input: radcore polls the keyboard/mouse state, so `radControllerSDLSetInputBlocked` (sdlcontroller.cpp) makes
  them read idle while open and after closing until every key/button is up (else Esc reached the pause menu).
  `SHAR_KEY_SCRIPT` holds bypass it.
- Text: `SDL_StartTextInput`/`StopTextInput` on open/close; printable ASCII only (stroke font), ` and ~ dropped.
- Drawing: `DevConsole::Render` before `EndFrame` (RenderManager.cpp), device projection, z compare always, no z write.
  Text via `pddiFont` directly (`DrawString` is compiled out in RAD_RELEASE); widths from a copy of
  `pddi/base/romans.h`. `pddiFont::Print` multiplies y by its scale: pass y / scale. Bold (drawn twice) at scale >= 1.5
  (lines are 1 px). Background: "simple" shader, AddRef'd, released on close. Vulkan draws the panel lighter than GL.
- Output: `Console::SetOutputHook` gets Printf/Errorf in all builds (script errors in red). Release `tName`s keep no
  text: `FunctionTableEntry::text` (64 chars) holds the names for completion/find.
- Fixed static buffers only (scrollback 512 x 160, history 32), no allocation.

## Testing
- `SHAR_CONSOLE_SCRIPT="ms:line;ms:line"` opens the console and runs the lines at those times; `DevConsole::Print`
  output also goes to stderr as `console: ...`. Verify drawing with `SHAR_SHOT_EVERY`. Close: `SHAR_KEYPRESS_SCRIPT=Escape@ms`.
- Not done: memcheck/audit build, real typing on Wayland IME.

## Next
- Pause the game while open (Source single-player), cvars (`name value`), autoexec, Lua lines, stderr tee.
