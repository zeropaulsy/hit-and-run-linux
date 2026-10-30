# Mods (Lucas' Mod Launcher format)

Docs: docs.donutteam.com/docs/LucasSimpsonsHitAndRunModLauncher/

## Loading (`port/compat/shar_mods.cpp`)
- Mod = folder: `Meta.ini` (`RequiredHack=`), `CustomFiles/`, per-hack configs, optional Lua (`CustomFiles.lua`,
  `CustomFiles.ini`). `.lmlm` = "LSPA" package (512-byte sectors; `mod/tools/lmlm_extract.py`), read in place.
- `SHAR_MODS` ':'-separated, earlier wins. Unsupported hacks reported at startup.
- Every read-only open -> `shar_ModResolve` once (`requests.cpp` -> `shar_ModTakeResolved` in `linuxdrive.cpp`;
  `binkffmpeg.cpp`): Lua handlers, CustomFiles.ini (OccludedPath, PathRedirections/AdditionalFiles), CustomFiles.
  Result: mod file (packaged = fd on .lmlm + slice), Lua output (memfd), redirect, or missing.
- Sound opens by key (decimal radMakeCaseInsensitiveKey32 of backslashed path): files/rules indexed by key too.
- Lua 5.4.8 (`port/third_party/lua`): one sandboxed state per mod, launcher API subset, VFS /Mods /GameData /GameDir
  (`shar_modlua.cpp`).
- `Main=1` mods save to ModSaves/<InternalName>/. Settings = Meta.ini defaults unless ModSettings.ini overrides.
- Config APIs: `shar_ModConfig*`, `shar_ModConfigFolder[Language]`, XML via `shar_modxml.h`; limits
  `shar_ModLimit*`/`shar_ModLimitFloat`/`shar_ModLimitBlank`; `shar_ModHackRequired`.
- Logs: `SHAR_MOD_LOG=1` (files provided, passengers, checkpoints), `SHAR_MOD_DUMP=dir`.

## Hacks implemented (where)
- OggVorbisSupport, FLACSupport: x.ogg / x.flac serves x.rsd (`radsound/linux/oggdecoder.cpp`, "OggS" / "fLaC" / ID3
  in `rsdfiledatasource.cpp`; `SHAR_RSD_LOG=1` logs "decoded"). Test FE sounds: soundfx/optionsmenu/button_22, scroll.
- VideoTextureSupport (`radmovie/src/linux/binkvideotexture.cpp`): shader texture names starting with / (file's TEX
  param or OverrideShaderParameters, `SetModVideoTexture` in p3d `shader.cpp`) open a Bink via `shar_ModVfsResolve`
  (`BinkOpen` with `BINKLINUXNOAUDIO`), looped; advanced before `BeginFrame` in RenderManager; freed when only the
  tTexture holds the pddi texture (`pddiObject::GetRefCount`). Power-of-two only (retail movies are 640x480: test
  by cropping the copy to 512x256). Failed paths logged once.
- HackSupport: HackSupport.ini [Miscellaneous] OverrideTexture (`ApplyModOverrideTexture`, p3d `shader.cpp`: base
  texture of every shader loaded after the texture is; a / path is a video). With any mod loaded, a car's follow
  camera data loaded again replaces the first copy (`cameradataloader.cpp`, `shar_ModsLoaded`). The rest is Windows
  plumbing (window title/icon, crash dialogs, D3D hooks): not ported.
- CustomLimits (doubles; several mods: largest wins, smallest for BoundsMinimumY):
  [Billboards]; [Regions] Limit/EntityLimit/RoadSegmentLimit; [Scripting] ArgumentLengthLimit; [Roads] RoadManager;
  [Pedestrians] PathLimit/GroupLimit (PathManager, `GetMaxModelGroups`, allocated); [TreeNodes] DrawDistance/Bounds*
  (WorldScene; blank keeps k-d tree's); [CollisionIndices] WorldPhysicsManager (+16 KB Temp per extra area + 256 KB in
  `PrepareHeapsStartup`); [Heaps] TempSize/LevelHUD*Size (MB, `PrepareHeaps*`); [Cars] CarLimit (VehicleCentral, 127
  slots, `GetMaxActiveVehicles()`), HuskLimit, SkidMarkLimit; [AnimEntities] (<=127); [Missions] StageLimit (128,
  `Mission::GetMaxStages`); [Triggers] VolumeLimit (TriggerVolumeTracker, allocated); [Miscellaneous]
  DeletedEntityLimit (RenderManager), ActionButtonLimit, old CarLimit/PathLimit.
  Not done: [Sound] Playing*PlayerLimit (docs' defaults 25/8 match nothing here: 64 clip / 9 stream fixed arrays,
  `playermanager.cpp`).
- DynamicTreeNodeEntityLimits: tree node lists grow (`WorldScene::AddGrowing`, `SwapArray::Grow`, level heap); always on.
- CustomInteriorSupport (`shar_modinteriors.cpp`, InteriorManager): [Interior] list replaces `ClassifyPoint` positions;
  Mirror*/ResetAmbientCharacter override Moe's/Bart's room; AllowJumping/Kicking/Sprinting (`CanJump/CanKick/CanSprint`).
- CustomRoadBehaviour (`shar_modroads.cpp`): DisableTrafficGroundSnapping (`Road::GetModNoGroundSnap`, set by
  RoadLoader from the name; release keeps no road names). UTurnTolerance ignored.
- CustomLicenseScreenTime (`bootupcontext.cpp`; Skippable via `BootupContext::SkipLicenseScreen`).
- NoMainMenuCameraAnimations (`CGuiScreen::StartTransitionAnimation`). HUDMapIgnorePlayerHeight (`HudMapCam::Update`).
- IncreasedVideoResolutionSupport (see platform.md, movies).
- CustomTrafficSupport (`shar_modtraffic.cpp`, `TrafficManager::ApplyModSettings`): AllocatedCars, MaxTraffic,
  MaxTrafficOnFoot, road/lane limits, intervals/distances, TrafficAI accel/decel, colours (level car -> car -> level
  -> root -> game's 25; also parked cars), group multiplier/weights; DynamicTraffic None/Models/Slots. PreallocatedCars
  (Slots: 0) cars made at level load; the rest and model swaps in `GetFreeTrafficVehicle`, ONE car at a time
  (`InitVehicle` queues its .con load; one per spawn overflowed the loading queue): the slot is unusable while
  `VehicleCentral::IsUnderConstruction`, and spawns wait for it (`mWantedModel`). Never load .con sync mid-game
  (`radFile::WaitForCompletion` runs other files' callbacks). Swaps free only cars with refcount 1, not in the
  active list (`ReleaseSlotVehicle`). AllocatedCars caps parked cars (`ParkedCarManager`, zone load).
  `SHAR_TRAFFIC_LOG=1` (slots made/freed, group changes, 5 s summary).
- CustomShopSupport: <CarShop> (`actionbuttonhandler.cpp`); <PhoneBooth>/<SkinShop> (`shar_modshops.cpp`,
  `IGuiScreenRewards::ApplyModShop`: Included/Excluded/FreeItems, HideLocked, Drawable). Release keeps only locator
  name UIDs: `ActionEventLocator::IsNamed` compares UIDs; locators named before `AddToGame`. Clothes menu 16 items.
- CustomDialogueCharacterCodes: [CharacterCodes] -> `dialogline.cpp` characterTable; [NongenericOutfits]/
  [NongenericCharacters] -> `dialoglist.cpp` (skinNameTable, characterMatches, GetStinkySkinPointer).
- CustomText (`guitextbible.cpp`, hook in FeLanguage::GetString): sections, $(Variables); language = ModSettings
  Language, else game's language title, else English, else first.
- 3DPhoneBoothPreviewSupport (`guiscreenphonebooth.cpp`): DarkenLockedCars (`Pure3dObject::SetLightScale`), FullScreen.
- CustomCarSupport (`shar_modcars.cpp`; `Vehicle::mModCar` from `AssignEnumBasedOnName`; unknown cars have
  `mVehicleID` INVALID 96): Index (`GetID`, follow-cam chunk), NoHusk, NoSkidMarks/NoFrontSkidMarks, ShadowVisible,
  Abductable, Recolourable, Invincible, NoBumperCam, RearWheelSparks, PreviewScale, steering/skid multipliers,
  [FakeWheel] (`FakeWheelJointDriver`), IsHusk/Husk= (`Vehicle::IsHuskModel()`, `HuskPool`). Unplayed: custom husk,
  PreviewScale, IsHusk.
- AdditionalScriptFunctionality (`code/mission/modscript.cpp`, `objectives/modobjectives.cpp`,
  `conditions/modconditions.cpp`; LINUX_EXTRA): all commands registered (console table 512); unimplemented log once.
  Objectives run as type "dummy": `ModCounter` (coins, wrench, wasps, destroycars, event, hitandrun*, peds, jump,
  speed, spring), AnimatedCam (+ SetObjUseCameraPosition -> `ModScript::SpawnCenter`), insidetrigger/outsidetrigger
  (`ModTrigger`). Conditions: counters after SetCondDelay, maintainspeed, triggers; unknown names skipped.
  `event` numbers are retail's (+1 from 177; `ModTranslateEvent`). Stage settings `MissionStage::GetModStage()`
  applied end of `Reset`, restored in Finalize. Checkpoints `Mission::mModCheckpoint` (`ModScript::OnRetry`;
  `Mission::Initialize` moves reset stage/locators/dynaload/peds). Passengers `Character::SetModSeat`.
  SetStageVehicleCharacter* -> `Character::ModSeatSettings` (kept per passenger; a seated one sits down again):
  animation in `InCarAction::IWannaRock` (missing from the set: in_car_idle), bobbing (default on; off once Animation
  is called) = `InCar::BobModPassenger` (the car's vertical accel, BounceCharacters' constants) + the passenger sway;
  scale/visible in `InCar::Enter`. `InCarAction::DoSimulation` faced every in-car character PI: mod seats keep their
  rotation. Destroyed car: no JumpOut = stays in InCar and leaves with the car; JumpOut = the game's panic GetOut,
  facing the given degrees (seat-rotation convention) or PI/2 when the seat is on the driver's side (x sign), else
  -PI/2; then kept (`jumped`) until removed or the mission ends. Checked: L1 m1sd famil_v (scale 2, rotation 0,
  visible; car destroyed in gdb with SyncVisualDamage(0): Lisa jumps, Bart goes). Bobbing/animation not looked at.
  CON AddVehicleCharacter*/SetVehicleCharacter*: stored on the car under construction (`Vehicle::mModCharacters`,
  4, worldsim/character/modseat.h); every active car of the type seated each frame (`UpdateConPassengers`, 32
  passengers, only with 8 character slots left: `CharacterManager::AddCharacter` overran its 64 when full; the
  stage ones check for 1). Suppressed = SuppressDriver of itself or a SuppressionCharacter ("none": never); L1
  suppresses lisa. Leave on `VehicleCentral::RemoveVehicleFromActiveList` / `~Vehicle` (`ModScript::OnVehicleRemoved`;
  also stage passengers), mission end. Jumped-out ones drop their target car once in Loco, gone beyond 150 m.
  Checked: famil_v Bart (scale, jump out), L1 minivanA Milhouse in traffic (seated/left 44 times over m1-m3sd).
  CON SetConditionalParameter -> `Vehicle::mModConditionals` (8), evaluated in `PostSubstepUpdate`
  (`UpdateModConditionals`): a target is on while any of its conditions holds; vehicleHealth = `mHitPoints` in
  [min, max] (the docs' 0-3 scale fits hit points; unconfirmed); drawableVisibility = `GeometryVehicle::
  SetModDrawableVisible` (composite elements by UID), hasDoors/highRoof = `mHasDoors`/`mHighRoof` (untouched without
  one). SetWheelieOffsetX -> `mModWheelieXOffset`, added with the Y/Z offsets (physicslocomotioncontrollerforces).
  Checked: famil_v DoorDRotShape (level 2) and HoodRotShape (health 0-1) hidden in L1; wheelie not driven.
  Traffic groups -> `TrafficManager::SetModTrafficGroup`: SetStageTrafficGroup at stage start (kept after);
  UseTrafficGroup / a retry's SetCheckpointTrafficGroup in `Mission::InitDynaLoad` (restart/select, not SD -> main).
  SetHitAndRunDecayInterior corrected only when hack required.
  SetStageAirGravity/KickForce/SlamForce: `CharacterTune` statics, saved/restored with the H&R settings (gravity >= 0
  ignored, as documented).
  Add/SetStageDynaLoadData once per stage entry (`StageDynaLoad`, locator in `Mission::mModStageDynaLoad`): Add =
  `EVENT_LOCATOR+DYNAMIC_ZONE` (street race props' way); Set = `DumpAllDynaLoads(1)` + `EVENT_FIRST_DYNAMIC_ZONE_START`
  (debug teleport's way). Interior name -> locator name. Stage 1's Reset runs twice at mission start: loads twice.
  AddParkedCar -> `ParkedCarManager::AddCarType` (array 5 -> `MAX_MOD_CARS` 16, extra ignored with a message); the
  mod loads the car's p3d itself. Zones take the first free type, so later types park only once enough zones load.
  Stage vehicle settings at stage start (`ApplyVehicleSettings`): Abductable -> `Vehicle::mModAbductable` (ufobehaviour,
  after CustomCarSupport's), AllowSeatSlide -> `mAllowSlide`; both kept until the mission ends (`RestoreVehicles`,
  the car AddRef'd meanwhile; "for a stage" in the docs, but their example re-enables it itself). Reset "current" =
  the car a Set "current" changed. NoDestroyedJumpOut -> `mModNoDestroyedJumpOut` (`InCar::Update` keeps the
  driver; leaves the scene with the car), never cleared (as documented). SetStageVehicleReset: at script time on the
  stage's car (`Vehicle::mModReset`, so ActivateVehicle's later AI obeys it): VehicleAI's stuck-reverse ResetOnSpot
  and WaypointAI's flipped/destroyed reset. Checked: log only (L1 M1 skinn_v), not destroyed/stuck in play.
  SetStageCharacterModel: `CharacterManager::SwapData` on the player at stage start (anim set default = the player's
  own, as 1.19); the own model comes back when a stage without one starts or in `ModScript::OnMissionFinalize` (not
  on Mission::Reset: stage 1 sets it again; not while leaving gameplay). Checked: L1 M1 h_fat on, off at stage 2.
  SetStageEnabledCheats: `ModStage::cheats` -> `CheatInputSystem::SetModStageCheats` (ORed in `IsCheatEnabled`, the
  player's own untouched) at stage start, off at Finalize / mission reset. x5StageTime also counts in that stage's
  SetStageTime/AddStageTime when it comes before them (times are scaled as the script runs). Checked: L1 M1 10 s -> 50.
  SetNoHitAndRunMusicForStage: `ModScript::NoHitAndRunMusic()` while the stage runs; `MusicPlayer::HandleEvent` skips a
  Hit & Run start then, and that Hit & Run's caught/evaded (whatever stage it ends in). Checked: meter forced to 100
  in gdb, L1 M1: MEVENT 32 without, none with.
  SetHUDMapDrawable -> `Mission::mModHudMap`, pending from `Mission::Initialize` (SD -> main too, retries) until a stage
  starts with a HUD (`ApplyHudMap`, `CHudMap::ModSetMapDrawable`, any inventory section); never undone (documented).
  Checked: L1 M1 with l2hudmap.p3d loaded in m1l: L2 map shown.
- CustomStatsTotals (`charactersheet/modstatstotals.cpp`): totals for scrapbook, pause progress, mission select,
  `QueryPercent*`; defaults reproduce original (100.3% on a complete save).
- CustomAudioSupport (`shar_modaudio.cpp`; `musicplayer.cpp` startingAmbience/startingExteriorAmbience/
  interiorMusicEvent): Mission StartAmbienceEventIndex = AmbientEventList (76 = AEVENT_INTERIOR_HOUSE -> level's);
  Level's = LocatorEvent (AMBIENT_SOUND_CITY 7); Interior MusicEventIndex = MusicEventList, also unknown interiors.
  BirdSound (`soundfxgameplaylogic.cpp` EVENT_PLAY_BIRD_SOUND, "null" none). MusicSamplingRate (`soundnucleus.cpp`).
  ForcedMusicRMSLevel: level number or path (docs don't say), queued once in `QueueMusicLevelLoad`,
  `UnloadRadmusicScript` keeps it. Debug Text mode not done.
- CustomTriggerActions (`code/meta/modtriggeractions.cpp`, ini walked by `shar_ModConfigIni`, two passes so names
  resolve forwards): hooks `TriggerVolume::Trigger` (PreventDefault: the locator never hears enter or exit),
  `EventManager::TriggerEvent` (bitset, PreventDefault drops it), `SuperCam::SetCameraValues` (tilt added to the
  `mSCTwist` target: eases in like L7's dutch angle; degrees). Actions run once per enter (death volumes re-enter every
  frame: `s_Inside` per locator+playerID). playerID >= MAX_PLAYERS = AI car (`TriggerVolumeTracker::GetAI`; only
  EVENT locators by default). Mission condition positional: story `GetCurrentMissionIndex` (+1 outside L1), Mission
  without SundayDrive = main part; BonusMission N = bonus slot N-1 (21-25 accepted). Unlocked = mission select's rule.
  Music by name via `MusicPlayer::ModTriggerEventByName` (radmusic event names). HUD map: `CHudMap::ModSetMapDrawable`.
  `CharacterSheetManager::QueryStreetRaceStatus(level, int)` was declared only: defined. Event conditions have no
  entity (entity conditions false). Debug Text mode not done.
- CustomAnimationSetSupport: accepted, no effect. Undocumented (docs: "custom animation sets for characters", no
  config). Custom sets already work where a script names one (`AddCharacter`, `AddNPCCharacterBonusMission`: any
  `art\chars\<set>.cho` + `<set6>_a.p3d`; Futurama's fry set checked). Other NPCs/drivers/peds are hard-wired to "npd";
  guess, unconfirmed: the hack makes an NPC use `<model>.cho` when present (Futurama ships leela.cho, Leela only ambient).
- CustomBonusMissionSupport (`shar_modbonus.cpp`): race intro dialogue mission by host model (`dialoglist.cpp`, the
  "intro" conversation in Sunday Drive; unbound hosts -> [Miscellaneous] index, clamped to the list). [L<n><mission>]
  ResetPlayer replaces `IsRaceMission()` in MissionManager's load-time `ResetPlayer` calls; ForcePlayerInCar the one
  in `Mission::ResetPlayer` (needs a vehicle restart locator). Base race scripts place the car and
  PutMFPlayerInCar themselves, so ResetPlayer=0 changes nothing visible there. StartBitmaps: host NPC name ("b_"
  prefix) compared by UID (release tNames have no text), `ReplaceMissionBriefingBitmap`. EndBitmaps: mission name
  stored when the Patty & Selma screen is requested (`CGuiScreenMissionSuccess::SetModMissionName`; the current
  mission moves on before InitIntro). Test: `SHAR_RACE_TALK=1 skipfe tp11 race1` goes through the host's dialogue.
- CustomSkinPreviewAnimations (`guiscreenpurchaserewards.cpp`): [Miscellaneous] <level index>=animation; one not
  loaded falls back to the game's (the original dereferences NULL). Checked with L1 hom_loco_walk.
- OverrideShaderParameters (`shar_modshaders.cpp`, end of `tShaderLoader::LoadObject`): type from the parameter name
  (pddishade.hpp table in the .cpp; unlisted = int). DIFF also sets AMBI unless AMBI is given (as the loader).
  Only shaders loaded from files; 2SID draws nothing different (culling off). Checked: docs' red famil_v*.
- CustomCharacterSupport (`shar_modcharacters.cpp`, by model name): IsLisa/IsMarge in `AddCharacter`; BlendModeSupport
  via `tShader::mFileBlendMode` (loader's BLMD) in `CharacterRenderable::DisplayModel`, flag set in `SetupCharacter`;
  IdleAnimationWeight in `WalkerLocomotionAction::Update` (the game's no-repeat rule still turns a repeat into idle0).
  The Lisa seat offset only applies once `InCar::SequenceAction` turns bouncing on: the `incar` test option skips
  it (get out and in again). BlendModeSupport not seen in play.
- Switch hacks (only when a mod requires them; the Mod Launcher's Settings page has no equivalent here):
  NoCheats (`CheatInputSystem::ReceiveInputs` drops entered codes; not tried in play). NoWrenches
  (`ActionButtonManager::NewActionButtonHandler` makes no "Wrench" handler; checked at the L1 start).
  NoHitAndRunMusic (`MusicPlayer::HandleEvent` ignores the three EVENT_HIT_AND_RUN_* events; not tried in play).
  NoBustedHitAndRunMeterReset (`HitnRunManager` busted keeps mCurrHitnRun, music skips the caught sting/restart;
  the docs expect the mod to set decay 0; not tried in play). NoFastCarReset (`Vehicle::Update` skips the 200 m/s
  ResetOnSpot). NoAutomaticSavedGameLoad (`CGuiScreenMemCardCheck::OnMemoryCardCheckDone`; checked: menu says New
  Game). NoSavedGames: Load Game greyed like the locked mini-game (`CGuiScreenMainMenu`), pause Save Game disabled
  (as RAD_DEMO), no save prompts on quit / next level (`CGuiManagerInGame`), no auto-load; checked: menu and pause.
  Release builds play the boot movies when no save is loaded: for menu tests point the run dir's movies/ at an
  empty folder.
  Unlock* hacks answer `CheatInputSystem::IsCheatEnabled` for the matching cheat (FINAL builds gate the cheats on
  every story mission done; the hacks don't). UnlockAllMissions: checked on a new game (pause Mission Select).
  `skipfe` enables the unlock-missions cheat itself: test from the menu. UnlockAllVehicles: checked at the L1 booth
  (228.7,6.7,-149.6: every car offered, new game). UnlockAllOutfits (skins cheat: shop and gallery): checked in the
  L1 Kwik-E-Mart shop. UnlockBonusGame (`IsMiniGameUnlocked`, and every track in `CGuiScreenMiniMenu::InitIntro`, else
  no track is enabled): checked from a fresh profile. NoBonusGame: `IsMiniGameUnlocked` false (wins over
  UnlockBonusGame), menu entry greyed; the card-deck "track unlocked" message stays (docs: the mod's to change).
  SkippableFMVs: accepted, no effect: the port never defines FINAL, so `FMVPlayer::Play` never disables skipping
  (retail's unskippable intro / unseen mission movies only exist in FINAL builds). NoMissionStartCameras: the
  scripts' SetAnimatedCameraName / SetAnimCamMulticontName / SetMissionStart* and SetInitialWalk do nothing
  (mod objectives' cameras still play); checked on L1 M1's start. NoNeitherRoadArrowProcessing:
  `MissionObjective::UpdateLightPath` returns for NEITHER arrow stages, as `LightPath` already did (no crash case
  known to test). NoInactiveDynamicObjectCollisions (guess, docs give no detail): a dynamic at rest when
  `SubmitDynamicsPseudoCallback` adds it is not auto-paired, so it collides only with its submitter (player, car);
  smoke-tested driving in L1 only. ReplayableBonusMissions (`GameplayManager::SetBonusMissionInfo`): the
  game drops a completed "bm1" NPC at level load (by name: `AddNPCCharacterBonusMission` never gets bm1's record)
  and a completed one-shot's (`BonusMissionInfo::SetCompleted`); the hack skips both, so it is offered again after a
  reload. One-shot arg 0 replays at once with or without the hack (2nd briefing shows the default picture). Mod
  scripts go under `CustomFiles/`. Checked: L1 Cletus with completion forced in gdb, `SHAR_RACE_TALK=1
  SHAR_RACE_AGAIN=1 skipfe race5 autopass8`.
- CustomCollectorCardSupport (`shar_modcards.cpp`): quotes replace `s_cardsDB` quotes in `CardsDB::LoadCards`
  (custom indices pass the card assert); Drawable in `CollectibleCard::Create`; CollectDrawable is the card's own
  icon (`mModCollectionThing`: the game's effect is one static for all cards, inited by the first card created),
  updated by `UpdateThing` while it plays. Checked: L1 cards (quotes in gdb, phone_icon in the world, collection
  through gdb-called OnEnter; card 1 is up the house wall, L1 224.3,7.2,-201.5).
- CustomDutchAngleNPCCameraTilt (`shar_moddutch.cpp`, `WalkerCam` "creepy twist" near peds, game: L7 only,
  0.176 rad): mission = `GetCurrentMissionIndex` (story mission; its Sunday drive and bonus missions share it).
  Checked in gdb forcing pedPCT (L1: off vanilla, 45 deg with the mod).
- CustomHeadlights (`GeometryVehicle` init, after the game's per-level scales): key = `GetCurrentLevelIndex`
  (0-6, bonus tracks B01-B07 = 8-14); [CustomTaillights] or the docs' [CustomTailights]. Checked: L1 lit.
- CustomMissionSkipFailCounts (`CGuiScreenMissionOver::InitIntro`, attempts = `QueryNumberOfAttempts`): L7 M5-M7
  only by FailCountL7MX (else never). Checked by calling InitIntro from gdb in L1 M2 (FailCountL1=0: Skip on).
- CustomVideoResolutionSupport (`radMovieRenderStrategyBink::ChangeParameters`): tile size = Segment*, default 256;
  max 64 tiles (more: not drawn, was an overflow). Without it the port already plays up to 64 x 256^2 tiles.
- DrunkCamera (`SuperCam::SetCameraValues`): sine wobble Min..MaxDegrees (default -10..10), 6 s period (the
  launcher's period unknown), added after the twist easing; `mDrunkTwist` is taken off again before easing.
- IncreasedRewardLimits: forsale merchandise 8 -> 64 per level (`RewardsManager::AddMerchandise`, slots past 8
  allocated there), SetCarAttributes 50 -> 512, previews `IGuiScreenRewards::PreviewLimit` ([Previews] CarLimit /
  SkinLimit, 64; without: 64 / 16), arrays 256. Always on: car inventory past MAX_CARS_OWNED (60) and purchases past
  MAX_PURCHASED_ITEMS (12) go to `<save>.mods` (`CharacterSheetManager::ModExtraWrite/Read`, text; removed when
  empty or the save is deleted); the saved CharacterSheet stays vanilla-sized (count clamped to 60).
- PerLevelCoinCount (`CharacterSheetManager::ModWallet`, used by Get/Add/SubtractTokens, which always had a level
  argument): wallet per level, [SharedWallet] Level= groups (each section one wallet). Only when required (the
  launcher's settings page toggle: none). Saved: the current level's wallet in mNumberOfTokens, all in the sidecar
  (`wallet L n`); a save without one: its coins go to its level's wallet. Bonus tracks: the story level's wallet.
- RefractionShaderSupport: "refract" installed (`pglInstallShaders`) only when required (D3D8 drew it as simple).
  `pglRefractionMat` (REFC colour, REFI index, REFB kept unused) + `PGL_PROGRAM_REFRACTION`: screen copy
  (`pglPipeline::CaptureScreen`, blit of the bound draw FBO, first refract draw per frame, unit 2) sampled at
  gl_FragCoord + (n.x, -n.y) * REFI * height/448, opaque, as ps2RefractionShader. Tested by mapping spheremap to it.
- HoverCarRefraction: on with RequiredHack=, RequiredMod=HoverCarRefraction (Futurama demo: it was a mod once), or
  ModSettings.ini [HoverCarRefraction] Enabled=1 (`shar_ModHackSetting`, also PerCarScreenCapture, default 1).
  `GeometryVehicle::Display` (frink_v) skips the PC's alpha fade and sets REFRACTBLEND (speed fraction; windshield
  special_m 0.5 + 0.5x) / REFRACTINDEX on the 12 `mRefractionShader` materials (simple on PC), per car as they are
  shared; per-car capture calls `pglRecaptureScreen`. `pglMat` with blend >= 0 draws `PGL_PROGRAM_REFRACTION` as the
  Xbox d3dRefractVertexShader: lit textured colour mixed with the screen copy by blend, index in 480-line pixels,
  opaque. Deviation: eye-space normal offset (Xbox used world-space). Test: L1's car swapped to frink_v in
  level01/level(i).mfk; at speed the body goes glassy, drivers stay solid.
- WalkerCameraDataSupport: 0x3000101 chunks were loaded (win32platform.cpp handler) into the current inventory
  section: level.mfk loads before `SuperCamCentral::Init` adds CameraInventorySection; `CameraDataLoader::Load` adds
  it first. `WalkerCam::SetTarget` then takes min/max magnitude, start magnitude, elevation, target offset (it set
  only max twice + elevation). Characters' ID is 9 (CharacterEnum::INVALID). SHAR_MOD_LOG prints the data used.
- CustomRewardQuestSupport (`code/mission/rewards/modrewardquests.cpp`): BindReward keeps every binding
  (`RewardsManager::BindModReward`: first of a game type fills the slot, the rest and the new types are mod rewards;
  enumerate with `GetNumRewards`/`GetRewardAt`, used by booth, gallery, bonus-game cars, CustomStatsTotals, shops).
  New types after `Reward::NUM_QUESTS`. With the hack the game's own slot unlocks are skipped: `ModRewardQuests::Synch`
  (UpdateRewardsManager, save loaded) sets every reward from the sheet; `Update` (MissionManager::Update) unlocks
  quests done, relocks coins, queues the HUD message (HudWaspDestroyed, extra hard-coded string; a wasp/gag message
  gives way). getin polls the player's car (incar/scripts put players in without an event); doorbell =
  EVENT_DING_DONG (locator name after DB_); H&R counts from the HitnRunManager events. Wager time: best < time.
  Sidecar lines `questcounts`, `questdone`. Skin shop: the level's quest skins, Price = COMPLETE_QUEST..., ToPurchase
  extra string TO_UNLOCK. `SHAR_MOD_LOG=1` logs unlocks. Checked: coins/defaultcar/getin unlocks + messages (custom
  QUEST_COMPLETE_<NAME>), booth list, skin shop texts, sidecar write/read (gdb).
- BugFixes (`shar_ModBugFix(section, key, default)`: BugFixes.ini, else the launcher's defaults: on, off for its
  -testing ones and RoadArrowPath...): Physics baby van = clone copies `mTimeComputeInertiaMatrix`
  (articulatedphysicsobject.cpp; else the port's 0); NoAirVentAudio = `EventLocator::OnTrigger` drops BOUNCEPAD
  for a player in a car; RestoreWaspCollision = `ActorManager::CreateActor` re-enables a banked actor's collision;
  FixStuckBrakeAndReverseLights = `Vehicle::ResetFlagsOnly` and `Avatar::SetOutOfCarController` hide them;
  FixSteeringAnimations = `VehicleController::GetEffectiveSteering` (stick or keys, as VehicleCentral) in
  `InCar` (state.cpp; GetSteering is stick only: no steer/sway anims on PC) and `SteerAction::ShouldRelease` (read
  the character's stick: released at once); checked by counting SteerActions while turning (0 vs 1 held);
  FixCharacterRotations = shadow (`Character::Display`) and radar (`CharacterTarget::GetHeading`) rotated by the
  parent transform; HUDCoinDepth = depth cleared, test/write on for the spinning HUD coin (`CoinManager::HUDRender`);
  FixRelativeAnimatedCamCameraChange = `AllowCameraToggle` false for RELATIVE_ANIMATED_CAM;
  RoadArrowPathItemLimitIncludeIntersectionCount = `MissionObjective::ArrowPathCapacity` (+intersections; Add is
  bounds-checked, the path was cut short); FixCarDeletedWhileLoadingCONFileCrash = the under-construction queue
  holds a reference (vehiclecentral.cpp). Always on (memory safety): coin counts of 8+ digits overflowed 8-byte
  buffers (`UpdateNumCoinsDisplay`, `HudCoinCollected::SetItemCount`, shown capped at 9999999 =
  FixHighCoinCountCrash); Unlock All Cameras set the PC walking camera count to the console list's 4, read past
  `PC_CAMERAS_FOR_WALKING` (guiscreenpausesettings.cpp). No code: FixCursorPosition (`FEMouse::Update` already maps
  to the 4:3 page), FixLateFocusFreeze and FixTimeSkip (Win32 focus / modal drag loop), FixZoneLoadOnExitCrash (quit
  sweeps incl. mid-load clean); FixMainMenuCheatCodeEnterCrash not reproduced (every cheat enabled at the main menu
  in gdb: no crash).
- Material shaders: every mod's `Shaders/materials.ini` (`shar_ModConfigFolderNth`), after the user's, in load
  order; a mod's files are named `<InternalName>:<file>` so same-named files in two mods stay apart.

## Test mods
- Futurama demo 0.1.3: L1 missions m0-m4 pass with autopass (not hand-played); shops/booth user-tested; booths FB001-6.
- Fully Connected Map 1.0.7: L1 plays; its doors: Moe's 872.3,7.1,-473.8, DMV 951.9,6.9,-799.9, Android's
  426.5,36.3,-1145.9, Observatory -67.4,33.5,-991.1. Its L2/L5 interiors lie outside the L2 render tree (data issue).
  Bridge-road traffic unchecked.
- SHAR Remastered 1.0: boots.
