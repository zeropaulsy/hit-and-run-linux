# Testing: game options, scripted runs, memcheck, robustness

## Option details
- `skipfe lN mM`: mission index from MISSION_TITLE_L<l>_M<m> in art/frontend/scrooby2/resource/txtbible/srr2.txt.
  `l7 m3` starts outdoors.
- `raceN`: 2 s after HUD, bonus mission N in level.mfk order (1 time trial, 2 circuit, 3 waypoint, 4 wager); briefing
  waits for Enter. Only loaded zones exist: teleport first (`skipfe tp11 race1`, `skipfe coins tp4 race4`; fee 20).
  `SHAR_RACE_TALK=1`: through the host's dialogue first (not wagers), as walking up to them does; Enter accepts.
  `race5` on L1 = Cletus (bm1; removed at load once completed). `SHAR_RACE_AGAIN=1`: the same mission again 6 s
  after returning to the Sunday drive (replay tests).
- `demo lN`: attract demo, race AI drives the family car, no time limit (driving tests without input).
- `incar`: 2 s after loading, into the mission car wherever parked (not from interiors).
- `tpN`: AddTeleportDest list in level.mfk, from 1 (L1 tp2 = Kwik-E-Mart). `SHAR_TP_POS` goes to a point instead
  (works without destinations, e.g. mods); `SHAR_TP_INTERIOR_POS` once inside (L1 clothes shop 493.6,-20.0,-290.4).
  Teleport to a door and tap E (18). "interior: entering X at x,y,z" is logged.
- Interior doors (InteriorEntrance locators in L*_TERRA.p3d): L1/4/7 KwikEMart 213.9,5.2,-302.7,
  SpringfieldElementary -35.2,4.6,-589.5, SimpsonsHouse 228.9,3.8,-184.1, bartroom (L4/7) 224.7,6.6,-200.8;
  L2/5 moe -173.6,7.1,24.5, dmv -253.3,6.9,350.7; L3/6 Android 22.4,7.0,-6.2, Observatory 177.2,4.3,487.6.
- `skipsunday`: crashes loading L3 M1, L6 M1, L7 M3 (scripts swap the car before avatar/physics exist): use autopass.
  Retry from Mission Failed hangs on loading (original returns to gameplay only outside pause, missionmanager.cpp:447).
- `autopass[N]` (default 30 s game time): every stage completes; logs "autopass: ...". Only stage 1 is in a fully
  loaded world (skipped drives don't stream zones). Skips missions without a DYNALOAD locator.
- `bonus`: from main menu (~65 s after boot) Left x2, Enter, Enter x3. `l0`: original shortcut into bonus menus.
- `SHAR_QUIT_AT=ms` logs "Window closed (context N)" (contextenum.h: 1 bootup, 2 FE, 8 loading, 9 gameplay, 10 pause).
- Verify runs with screenshots (`SHAR_SHOT_EVERY`): logs show little; runs have silently tested nothing (empty ground).

## Memcheck (`port/memcheck.sh`)
- Valgrind fetched/set up on first use (unstripped i386 ld.so built), `--soname-synonyms=somalloc=nouserintercepts`,
  `--fair-sched=yes` (else load thread starves). Game heaps/FBMemoryPool marked undefined on alloc.
- Finding the member: `VALGRIND_GET_VBITS` on the object + `gdb -batch -ex "ptype /o Class"`. Origins follow copies.
- Fix by zero-init (what MSVC effectively gave), mark `// uninit fix (Linux port)`. L1 starts identically with
  `SHAR_FILL_ALLOC=0x00` and `0xFF`.
- Timing under valgrind: L1 ~3 min to playable, L7 3-5 min (key scripts from ~330 s); driving needs
  `SHAR_MAX_FRAME_MS=33` (else car outruns streaming; `DSG: Move: can't find`); mission transitions may be black ~5 min.
- Recipes: `memcheck.sh 900 skipfe demo l1`; races `1200 skipfe race2` (Enter taps from 120 s, W held); missions
  `1500 skipfe incar autopass20 nofail lN m1` (Enter every 15 s, W held, MAX_FRAME_MS=50); chains from M2 `autopass15`
  (75 min/run); bonus `... 1500 skipfe l0` (Enter every 10 s from 90 s).
- Runs on X11 (`SHAR_X11=1`, memcheck.sh default): on Wayland libglycin segfaults under valgrind at window creation.
- A level's last mission ends at the save prompt; Enter taps then loop slot list <-> "Are you sure?" (NO): run idles.
  Check an M7 finished by its objective types in the autopass log (ObjectiveTypeEnum, missionobjective.h).
- Known noise: X11/udev/dbus/SDL startup; vsnprintf %s past terminator (Rig::ReadFromScript, LoadingManager, HandleOption,
  UserController::LoadConfig).
- Covered clean: boot, L1 walk/drive/races 1-4/interiors (all levels' interiors), L7 M1 interior, demos L1-7, bonus
  game (AI driving only), all missions of every level (L3 M7 -> L4 transition too).
  **Not yet: cutscenes beyond those, bonus game with player driving.**
- `MissionObjective::LightPath` with no road within 50 m of player or target (zone not streamed, autopass) used
  uninitialised path elements: now lights no path.

## Robustness / shutdown
- Phone booth, car without a 2D image (rocke_v, cBlbart, mods' cars): the image kept the previous car's unloaded
  sprite and corrupted memory (crashes in sound / Scrooby): now hidden (`CGuiScreenPhoneBooth`, 2D preview).
- Quit sweeps done (release + debug, all contexts incl. mid-load, bonus game): clean.
  Quit in context 0 (before bootup `WorldPhysicsManager::Init`): dtor skips its uninitialised arrays.
- Debug-build teardown asserts are the original's (SimEnvironment/SimUnits refs, loading managers' state,
  EventListener not removed, radLoadObject directly deleted, radFileSystem files/requests): ignore.
- Sound loads complete only from SoundManager::Update/UpdateOncePerFrame (relevant to cancel/shutdown hangs).
- >18 characters in physics: collision area -1 -> now no collision for the extra (raising areas to 32 ran Temp out).
  `PrintOutOfMemoryMessage` prints heap/sizes to stderr.

## Retail data fixes
- `l5_crate` state1 lacks SPAWN_10_COINS (l1-l4 crates have it): appended in `CStatePropDataLoader::LoadObject`.
  Dump stateprop state machines: parse chunk 0x08020000 (layout in that loader). Release builds don't keep entity
  names: compare the loaded string, not `GetName()`.
