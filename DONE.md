# Progress

What the port does so far. User-facing overview: [`README.md`](README.md); porting details:
[`CLAUDE.md`](CLAUDE.md).

## Done

### Build and platform
- [x] **Build system**: CMake + Ninja project generated from the original Visual Studio projects
  (`port/gen_cmake.py`), 32-bit (`-m32`), release and debug (original `RAD_DEBUG`) configurations
- [x] **Platform layer**: SDL2 window and event loop; Win32 threads, sync, fibers and timers on POSIX;
  case-insensitive file access to the game data
- [x] **Launcher**: `port/play.sh` builds if needed, sets up the run directory and starts the game
- [x] **Crash reports**: each session logged; crashes kept with the backtrace resolved to source lines
- [x] **Closing the window during boot or loading**: no longer hangs or crashes (sound loads, file errors at unmount,
  radscript teardown, vehicles destroyed after their managers, load callbacks while cancelling)
- [x] **Missing files**: reported (`File not found: …`) instead of hanging behind the original retry screen
- [x] **Self-contained Linux build**: `port/package.sh` bundles the game with its 32-bit libraries and a launcher

### Renderer
- [x] OpenGL Pure3D backend on SDL2
- [x] **OpenGL 3.3 core profile**: GLSL shaders reproduce the Direct3D 8 renderer's materials and lighting (car
  paint reflections, two-sided materials, fading cars and characters); geometry in GPU buffers
- [x] DXT textures stay compressed on the GPU (decoded in software on drivers without S3TC)
- [x] Multithreaded loading via shared GL contexts (Direct3D 8 was multithreaded, GL is not)
- [x] Texture clamping matching Direct3D (no dark seams)
- [x] Vsync plus a 60 fps cap (physics and AI break at thousands of fps)
- [x] Colour-write masks and alpha testing matching Direct3D (HUD map, see-through fences and ramps)
- [x] Gamma applied by a shader pass (works in a window and under Wayland)
- [x] Post-processing passes at reduced size (`#pragma shar_scale`); SSAO at half size
- [x] Multisample anti-aliasing (4x by default) with alpha-to-coverage for cut-out textures
- [x] Mipmaps with up to 16x anisotropic filtering
- [x] User post-processing shaders (ambient occlusion, bloom, colour grading, tone mapping, CRT, ...)
- [x] User material shaders (toon shading, car paint, ...), by material type, file or name; mods can ship them

### Display
- [x] **Any aspect ratio**: Hor+ 3D view, centred 4:3 UI; letterbox bars, fades and the front-end TV frame span
  the window; verified in play, including extreme ratios
- [x] **Resizable window**: adapts live to any size
- [x] **Fullscreen**: borderless at the desktop resolution, or exclusive at the chosen resolution
- [x] **Resolution menu**: common PC sizes plus the desktop's own, filtered to what fits; any `WxH` from
  `simpsons.ini` accepted

### Input
- [x] Keyboard and mouse (mouse look via relative mode), wired and wireless gamepads through SDL; verified in play
- [x] Start pauses; the menu cursor hides while a gamepad is in use
- [x] Gamepad rumble (the Windows release never rumbled gamepads)

### Sound and video
- [x] Software mixer on SDL audio with 3D positioning: sound effects, music, dialogue
- [x] EAX reverb in software, driven by the game's own presets; verified by ear
- [x] Bink movies decoded with FFmpeg, audio through the game's mixer
- [x] 64-bit: sound resources made of several files play the right clip (keys read as 32-bit)

### Game data and gameplay
- [x] **Retail PC data**: split music archives (`music00`–`03.rcf`), Radical ADPCM dialogue, logo movies replaced
  by stubs in some copies
- [x] **Gameplay**: loads levels and plays, tested into Level 7
- [x] **Uninitialised reads** that were zero by luck under MSVC: fixed where found
- [x] **Retail data fix**: Level 5 crates give their 10 coins on the first hit, like Levels 1-4
- [x] **Saving from a `skipfe` session**
- [x] **Save, quit, load** round trip from a normal game; verified in play
- [x] **Test launch options**: `skipfe`, `skipfe l<N> m<N>`, `demo l<N>`, `incar`, `tp<N>`, `race<N>`, `coins`

### Memory checking (valgrind memcheck, clean after fixes)
- [x] Boot and the front end
- [x] Level 1: walking, the first mission, player driving
- [x] Level 1 races: time trial (including its restart), circuit, waypoint, wager
- [x] Interiors: Kwik-E-Mart, school, Simpsons' house and Bart's room, Moe's, DMV, Android's Dungeon, Observatory
- [x] Level 7 Mission 1 (Simpsons' house, talking to Lisa, walking outdoors)
- [x] Attract demos (AI driving) of all seven levels
- [x] The bonus game (64-bit: menus, a 3-lap AI race, standings; fix: WorldScene view vector)
- [x] Every mission of every level, every stage via `autopass`: M1 of each level (5 fixes), L1, L2, L4, L3 M2-5
  (2026-10-20, 1 fix + 2 crashes fixed); L3 M6-7, L5-L7 M2-7 and the L3 -> L4 transition (2026-10-23, one crash
  fixed: arrow path lighting with no road nearby)

### Mods (Lucas' Mod Launcher)
- [x] `.lmlm` packages and extracted folders, several at once
- [x] CustomFiles, LuaSupport, OggVorbisSupport
- [x] CustomLimits: every section except [Sound] (its defaults match nothing in this source; see CLAUDE.md)
- [x] CustomInteriorSupport, CustomRoadBehaviour (DisableTrafficGroundSnapping), DynamicTreeNodeEntityLimits
- [x] Fully Connected Map 1.0.7: Level 1 plays, interiors entered (Kwik-E-Mart, Moe's)
- [x] CustomLicenseScreenTime (Time, Skippable), NoMainMenuCameraAnimations, HUDMapIgnorePlayerHeight
- [x] CustomTrafficSupport: counts, distances, intervals, lane limits, acceleration, colours, groups, DynamicTraffic;
  PreallocatedCars and cars made or swapped mid-game; AllocatedCars caps parked cars
- [x] AdditionalScriptFunctionality traffic groups: UseTrafficGroup, SetStageTrafficGroup, SetCheckpointTrafficGroup
- [x] IncreasedVideoResolutionSupport (movies up to 1920x1080)
- [x] CustomText (with the Language setting)
- [x] CustomShopSupport: car shop seller and conversation; phone booth and skin shop item lists and icons
- [x] CustomDialogueCharacterCodes: new speaker codes, outfit voices ([NongenericOutfits]/[NongenericCharacters])
- [x] 3DPhoneBoothPreviewSupport (all settings)
- [x] AdditionalScriptFunctionality: what the Futurama demo uses (see To do for the rest)
- [x] CustomCarSupport: every setting ([FakeWheel] CopySkidMarks read, not drawn)
- [x] CustomAudioSupport: every setting (mission/level ambience, interior music, BirdSound, MusicSamplingRate,
  ForcedMusicRMSLevel)
- [x] CustomStatsTotals: every setting (scrapbook, level progress, mission select, completion percentages)
- [x] CustomTriggerActions: every action, condition and trigger type, game event bindings, PreventDefault
- [x] CustomBonusMissionSupport: race host intro dialogue, ResetPlayer / ForcePlayerInCar, start and end bitmaps
- [x] CustomSkinPreviewAnimations: the clothes shop preview's animation per level
- [x] OverrideShaderParameters: colour, float, int, vector and texture overrides by shader name (wildcards)
- [x] Material shaders from every mod's `Shaders/` folder (earlier mods' rules win)
- [x] CustomCharacterSupport: BlendModeSupport, IsLisa, IsMarge, IdleAnimationWeight
- [x] Switch hacks: NoCheats, NoWrenches, NoHitAndRunMusic, NoBustedHitAndRunMeterReset, NoFastCarReset,
  NoAutomaticSavedGameLoad, NoSavedGames, UnlockAllMissions, UnlockAllVehicles,
  UnlockAllOutfits, UnlockBonusGame, NoBonusGame; NoMissionStartCameras, NoNeitherRoadArrowProcessing,
  NoInactiveDynamicObjectCollisions (a guess at the mechanism), ReplayableBonusMissions; SkippableFMVs accepted (movies are
  always skippable here)
- [x] CustomCollectorCardSupport: per-card world and collection drawables, quote characters (custom ones too)
- [x] CustomDutchAngleNPCCameraTilt: per level / mission NPC camera tilt and angle
- [x] CustomHeadlights: head and tail light brightness per level and bonus track
- [x] CustomMissionSkipFailCounts: failures before Skip per game, level and mission
- [x] CustomVideoResolutionSupport: movie segment (tile) size
- [x] BugFixes: physics (baby van), air vent audio, wasp collision, stuck lights, steering animations, character
  rotations, HUD coin depth, phone booth camera change, road arrow path size, car deleted during .con load; coin
  count and Unlock All Cameras overflows fixed always; the rest Windows-only, already fixed or not reproduced
- [x] CustomAnimationSetSupport: accepted with no effect (undocumented; custom sets already load where scripts name one)
- [x] Futurama: Hit & Run demo plays Level 1 with its shops and phone booth; missions m0-m4 run through (autopass)