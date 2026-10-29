# Progress

What is left for the port. Completed items move to [`DONE.md`](DONE.md). User-facing overview: [`README.md`](README.md); porting details:
[`CLAUDE.md`](CLAUDE.md).

## To do

### Verification (playing the game)
- [ ] **Full playthrough** of all seven levels: missions and cutscenes (in progress: Levels 1-6 done, smooth; on Level 7)
- [ ] **Bonus game** (Itchy & Scratchy racing) with 2-4 pads
- [ ] **Street races, wager races and time trials** in every level
- [ ] **French, German and Spanish**

### Memory checking
Every mission of every level is done (see DONE.md). Left:
- [ ] Cutscenes other than those the mission runs passed through (FMV objectives, the ending)
- [ ] The bonus game with a player driving (so far only AI racers)
- [ ] Street races, wager races and time trials outside Level 1

### Robustness
- [ ] Shutdown paths: closing the window in every context. Release and debug builds swept with `SHAR_QUIT_AT` (boot,
  front end, Level 1/7 load (100 ms steps across its end) and gameplay, pause, interior, car, race, demo): no hangs or
  crashes left; still to do: the bonus game, missions in progress, cutscenes, the front end's Quit, quitting while saving
- [x] Debug-build teardown: no crashes; the asserts left come from the original teardown order (see CLAUDE.md)
- [ ] Missing-data tolerance where it is cheap (a missing `art\wrench.p3d` is handled: its icons draw nothing)

### Mods
Order of attack: the character/animation hacks (what the larger total conversions use); the small cosmetic hacks are quick wins
in between. Hacks a mod requires but the port lacks are listed at startup (`mods:   not supported yet, ignored: ...`).

#### Hacks not implemented (all required by the Futurama demo)
- [ ] **HoverCarRefraction**: a mod (not a hack) the Futurama demo lists as required
- [ ] The Mod Launcher's other mod-requirable hacks (docs' All Hacks page, 1.27), none of the test mods uses:
  - [ ] configured:
    CustomRewardQuestSupport (large: multi-reward quests, 11 new types, save counters, booth/shop text), DrunkCamera,
    IncreasedRewardLimits, PerLevelCoinCount, RefractionShaderSupport, WalkerCameraDataSupport
  - [ ] FLACSupport, VideoTextureSupport; HackSupport (the launcher's own plumbing: check what mods rely on)

#### Partly done
- [ ] **AdditionalScriptFunctionality** (`code/mission/modscript.cpp`): every command registered (unimplemented ones log
  once when used). Done: every objective/condition type, the If* conditionals,
  stage payout / Hit & Run / ped group / music / cancel settings, DisableTrigger, checkpoints, passengers
  (AddStageVehicleCharacter), SetObj/CondSound, SetObjExplosion, SetObjUseCameraPosition, SetCollectibleSoundEffect
  (not checked in play), SetPedsEnabled / SetParkedCarsEnabled. Left:
  - [ ] passengers: animation, scale, visibility, jumping out
  - [ ] stage vehicle settings, SetStageCharacterModel
  - [ ] forces and gravity
  - [ ] stage dyna load data
  - [ ] AddParkedCar
  - [ ] CON commands (SetConditionalParameter, SetWheelieOffsetX)
- [ ] **CustomLimits [Sound]** PlayingClipPlayerLimit / PlayingStreamPlayerLimit: the docs' defaults (25, 8) match
  nothing in this source (64 clip, 9 stream players); needs the retail behaviour. Values up to 64/9 fit already
- [ ] **CustomRoadBehaviour UTurnTolerance**: undocumented, logged and ignored
- [ ] **CustomCarSupport [FakeWheel] CopySkidMarks**: read, not drawn (the skid mark generator has four wheels)

#### Checks in play (so far only automated runs and screenshots)
- [ ] **Futurama** Level 1 missions by hand: m1-m4 and bm1 (kicking the pawn in m2, the wasps in m3, the speed
  objective and the speed-range bar)
- [ ] **Custom cars**: a destroyed car leaving a custom husk (Husk=), PreviewScale, IsHusk
- [ ] **Fully Connected Map**: the Level 2/3 areas inside its Level 1, the DMV, Android's Dungeon and Observatory
  interiors (doors listed in CLAUDE.md), traffic on its bridges (Road006/Road0061), its warp missions
- [ ] **Traffic groups**: UseTrafficGroup on a mission restart / selection, SetCheckpointTrafficGroup on a retry
  (SetStageTrafficGroup and the slot model swaps were run with a test copy of the Futurama demo)
- [ ] **CustomTriggerActions** in play: a test mod's door trigger checked (H&R meter, TiltCamera ease, nested and
  Setting conditions, PreventDefault, an event binding); Futurama's M1Char texture swaps run on spawn. Not triggered
  yet: ChangeHUDMapDrawable, DestroyCar, ChangeTrafficGroup, a music event that exists, AI cars, Mission conditions
- [ ] **CustomBonusMissionSupport** in play: a test mod's start bitmap and ResetPlayer=0 checked on L1 SR1;
  Futurama's race hosts' intro dialogue (tinny, alien, flexo) and the end bitmaps not heard/seen yet
- [ ] **CustomAudioSupport** in play: the Futurama demo's mission ambience and a test mod's ForcedMusicRMSLevel were
  checked in logs only; BirdSound and <Interior> music not triggered yet
- [ ] **Raised CustomLimits** pushed past the game's own values in play (cars, stages, trigger volumes, ...): so far
  only checked that a mod raising them all loads and quits cleanly

### Platforms
- [x] **64-bit build** boots and plays: movies, front end, Levels 1/7, races, interiors, driving, sound; AddressSanitizer clean
- [ ] **64-bit verification**: long play sessions. Done: the bonus game (ASan clean, quits in its menu/load/race), mods under
  ASan, memcheck tooling (Level 1 clean), packaged bundle (`port/package.sh`)
- [ ] **Windows x64**
- [ ] **macOS** (Apple silicon and Intel)
- [ ] **Packaging**: an AppImage-style bundle per platform (Linux has a tarball from `port/package.sh`)

### Renderer extras
- [ ] Anti-aliasing entry in the display menu
- [ ] Sun lighting on the world (needs shadow maps; prototyped and dropped)
- [ ] **Vulkan renderer** (shelved 2026-10-19): a third pddi beside GL, GL kept as fallback. Reuses the GL
  pipeline's CPU state tracking and GLSL (compiled to SPIR-V with glslang); new work is pipeline objects/cache,
  memory and frame sync, swapchain, texture Lock/readbacks. For Android, macOS (MoltenVK), low-power devices,
  loading threads without the shared GL context pool, ray-traced shadows, DLSS/FSR. Not for desktop speed.
  Cheaper alternative to evaluate first: `pddidx8` on DXVK-native's D3D8 (loses the GL extras). Start after 64-bit
  is the default, with the legal screen and front end compared to GL by screenshot

### Other
- [ ] **Steering-wheel force feedback**: springs and dampers; needs a wheel to test with
