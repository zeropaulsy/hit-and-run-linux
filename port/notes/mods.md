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
- OggVorbisSupport: x.ogg serves x.rsd (`radsound/linux/oggdecoder.cpp`, "OggS" in `rsdfiledatasource.cpp`).
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
  Traffic groups -> `TrafficManager::SetModTrafficGroup`: SetStageTrafficGroup at stage start (kept after);
  UseTrafficGroup / a retry's SetCheckpointTrafficGroup in `Mission::InitDynaLoad` (restart/select, not SD -> main).
  SetHitAndRunDecayInterior corrected only when hack required.
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
- Not done: HoverCarRefraction (a mod).
- Material shaders: every mod's `Shaders/materials.ini` (`shar_ModConfigFolderNth`), after the user's, in load
  order; a mod's files are named `<InternalName>:<file>` so same-named files in two mods stay apart.

## Test mods
- Futurama demo 0.1.3: L1 missions m0-m4 pass with autopass (not hand-played); shops/booth user-tested; booths FB001-6.
- Fully Connected Map 1.0.7: L1 plays; its doors: Moe's 872.3,7.1,-473.8, DMV 951.9,6.9,-799.9, Android's
  426.5,36.3,-1145.9, Observatory -67.4,33.5,-991.1. Its L2/L5 interiors lie outside the L2 render tree (data issue).
  Bridge-road traffic unchecked.
- SHAR Remastered 1.0: boots.
