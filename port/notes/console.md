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
| `debugcam [xz y [dist]]` | toggles the debug view: a camera orbiting the game camera, frustum in white (`SuperCamCentral::ToggleDebugView`); angles in degrees (y from straight above), default 70 m |
| `damage <hood\|back\|driver\|passenger>` | `Vehicle::DebugInflictDamage*` on the player's car (15% of hit points a hit) |
| `cards [all\|none]` | counts collected cards, or `CardGallery::Add/RemoveAllCollectedCards` |
| `npc [model]` | `CharacterManager::Spawn` (default homer) in front of the player; replaces the last one |
| `skin` | `CharacterManager::NextSkin`: next outfit for Bart/Homer/Lisa/Marge/Apu |
| `lang [english\|french\|german\|spanish]` | text only (`CGuiTextBible::SetCurrentLanguage(l, false)`); speech, movies and simpsons.ini use `GetStartupLanguage` |
| `save [slot]` / `load [slot]` | `GameDataManager::SaveGame/LoadGame` (slots 1-8); save in a level, load in the front end only |
| `quit` | quits the game (SDL_QUIT) |

- Else script functions (`Console`'s table): `Name a b` -> `Name("a","b");` after an arg-count check
  (`Console::GetFunctionArgs`); a line with `(` (or `script ...`) goes to `Console::Evaluate` as typed (`;` added).
  Most are mission-load functions: mid-game calls can misbehave. Debug builds: script syntax errors hit `rTuneAssert`.
- `tp x y z` only with three numbers (else the words are a destination name, which has spaces); on foot only.
- debugcam..load are the original's Watcher functions (`radDbgWatchAddFunction`, DEBUGWATCH: off on Linux).
  `SUPERCAM_DEBUG` (supercam.h) and `DrawFrustrum` are on for Linux release builds for debugcam. The debug view
  doesn't show during the level-start intro (another camera has the view).
- `lang` must not swap the dialogue cement file mid-level (`daSoundRenderingManager::SetLanguage`): heap corruption
  (`free(): invalid size` at shutdown); retail PC has only `dialog.rcf` anyway. Text: the game loads only the
  startup language's strings (`FeTextBibleLoader::SetOnlyLoadCurrentLanguage`, guisystem.cpp); with the console on
  it loads all four (retail `srr2.p3d` has them), so `lang` switches text (seen: mission objective in French).
- `save` mid-level: the save only reports back through `GameDataManager::Update` (minimum save time), which the
  original calls only in bootup/frontend/pause contexts; `GameplayContext::OnUpdate` calls it too on Linux.
  `load` mid-level would change progress under a running level: front end only (then Resume Game).
- `npc` checks `art\chars\<6 letters>_m.p3d` with `radFileOpenSync` first; `Destroy` clears `sSpawnedCharacter`.
- `cheat`: own name table -> `CheatInputSystem::SetCheatEnabled( id, on, force=true )`: skips FINAL's unlock-cheats-need-100%-story rule (typed cheat codes keep it); `unlockall` sets every unlock cheat (as `ReceiveInputs`).

## Wiring
- Events: `PumpEvents` (linuxplatform_sdl.cpp) gives every SDL_Event to `DevConsole::HandleEvent` first; open, it takes
  keyboard/text/mouse events (never controller/window). `DevConsole::Update` at the end of `PumpEvents`.
- Game input: radcore polls the keyboard/mouse state, so `radControllerSDLSetInputBlocked` (sdlcontroller.cpp) makes
  them read idle while open and after closing until every key/button is up (else Esc reached the pause menu).
  `SHAR_KEY_SCRIPT` holds bypass it.
- Text: `SDL_StartTextInput`/`StopTextInput` on open/close; printable ASCII only (the font has nothing else), ` and ~ dropped.
- Drawing: `DevConsole::Render` before `EndFrame` (RenderManager.cpp), device projection, z compare always, no z write.
  Font: DejaVu Sans Mono cut to ASCII (`port/third_party/dejavu`, embedded header; README has the remake commands),
  rasterised by stb_truetype (`port/third_party/stb`, STBTT_STATIC, malloc) into one 32-bit atlas texture by
  `BuildFont`: 18 px at 1080 lines (min 14), rebuilt when the window height changes, released on close with the
  background shader. Whole-pixel advance and glyph positions keep it sharp (bilinear, 1 px gaps); `%-Ns` columns line
  up. Lock's rows are bottom-up (negative pitch): v is `1 - row / size`. `DrawText`: one textured prim stream per
  line (`PDDI_V_CT`, "simple" shader, alpha blend). Background: "simple" shader, AddRef'd, released on close. Vulkan draws the panel lighter than GL.
- Output: `Console::SetOutputHook` gets Printf/Errorf in all builds (script errors in red). Release `tName`s keep no
  text: `FunctionTableEntry::text` (64 chars) holds the names for completion/find.
- Fixed static buffers (scrollback 512 x 160, history 32); allocation only while open: the font texture and shaders
  (pddi), stb_truetype's scratch while building (libc malloc).

## Testing
- `SHAR_CONSOLE_SCRIPT="ms:line;ms:line"` opens the console and runs the lines at those times; `DevConsole::Print`
  output also goes to stderr as `console: ...`. Verify drawing with `SHAR_SHOT_EVERY`. Close: `SHAR_KEYPRESS_SCRIPT=Escape@ms`.
- Scratch run dir for save/load (run/ has the user's saves).
- Not done: memcheck/audit build, real typing on Wayland IME.

## Next
- Pause the game while open (Source single-player), cvars (`name value`), autoexec, Lua lines, stderr tee.
