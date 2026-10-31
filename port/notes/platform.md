# Platform: window, GPU drivers, input, timing, audio, movies, files

## Window / display (`code/main/linuxplatform_sdl.cpp`, `gldisplay.cpp`)
- `InitDisplay` failure -> error dialog + exit (used to leave loaders calling GL without a context).
- Threads: workers get shared GL contexts (`shar_SetThreadStartHook`/`shar_RunThreadStartHook`, radcontent load loop);
  on Wayland made current surfaceless (`SDL_GL_MakeCurrent(NULL, ctx)`), else "no GL context left". Max texture size cached.
- Wayland native by default (`SHAR_X11=1` or `SDL_VIDEODRIVER=x11` for X11): XWayland with a screen-covering window
  leaked GBs of Shmem in gnome-shell (OOM). ALLOW_HIGHDPI: logical window sizes, drawable from
  `SDL_GL_GetDrawableSize`, mouse normalised. Mouse-look = SDL relative mode (X11 keeps re-centring warp).
  Unverified on Wayland: mouse-look feel, gamepads, minimised pacing.
- NVIDIA 32-bit egl-wayland2 fails window surfaces (EGL_BAD_ALLOC): `InitializeWindow` points
  `__EGL_EXTERNAL_PLATFORM_CONFIG_FILENAMES` at 10_nvidia_wayland.json unless user set it/`_DIRS`. Wayland failure ->
  SDL video restarts on X11. 64-bit egl-wayland2 works.
- gnome-shell leaks ~10 MB VRAM per SDL window (not the game's). Black/frozen at large sizes: `nvidia-smi` first;
  log out/in. Repro: VRAM filler leaving ~450 MB.
- Intel Ice Lake (PCI 0x8a..): GPU hangs (`i915 ... Resetting rcs0`, `journalctl -k`) fixed only by
  `INTEL_DEBUG=nohiz`, set automatically (`SHAR_INTEL_HIZ=1` keeps HiZ). Dumps: `pkexec cat /sys/class/drm/card1/error`.
- Cursor under XWayland: a hidden warped cursor locks the pointer. Only mouse-look hides it; menus use a 1-px
  alpha-1/255 cursor (`ShowTheCursor`). Pointer captured only in mouse-look (`FEMouse::SetupInGameMode`). Game cursor
  hides after pad input (`FEMouse::SetHiddenForGamepad`; revealing click swallowed).

## Frame timing / loading
- Unsynced GL ran 2-4k fps, 0 ms frames broke physics (NaN traffic speed, launches): vsync + 60 fps cap (`game.cpp`,
  `SHAR_FPS_CAP`, `SHAR_VSYNC=0`). `SHAR_FRAME_LOG=1` histogram.
- radcontent load thread runs only inside `SwitchTasks()`: `radLoadManager::Service` hands off up to 8 ms/frame
  (`SHAR_LOAD_BUDGET_MS`).
- NVIDIA throttles a minimised window to ~1 swap/s: `SwapWithVsync` pauses vsync when throttled, retries after 5 s
  backing off to 30 s. Test `SHAR_MINIMIZE_SCRIPT`.

## Input (`sdlcontroller.cpp`)
- SDL2 controllers expose DirectInput-compatible points; "LeftMotor"/"RightMotor" for `RumbleEffect`
  (SDL_GameControllerRumble; ini `useforcefeedback`, default off; `SHAR_RUMBLE_LOG=1`). Wheel FFB not done.
- Linux pad defaults (`UserController::LoadDefaults`) = console layout in SDL order. Start pauses (`feStart`, button 7).
- SDL sends DEVICEADDED for pads present at startup: `OpenGameController` skips open ones.
- Debug: `SHAR_INPUT_LOG`, `SHAR_NO_INPUT`, `SHAR_NO_MOUSE`, `SHAR_FORCE_MOUSELOOK`.

## Audio (`radsound hal/linux/linuxhal.cpp`)
- SDL callback mixer owns voice positions; without device/`SHAR_NO_AUDIO=1` voices advance on a timer.
- Reverb: software EAX2-style (`radSoundLinuxReverb`); only positional voices feed it; presets via effect setters,
  fades via system aux gain. `SHAR_REVERB=0`, `SHAR_REVERB_LOG=1`.
- Retail dialogue is RADP: `radSoundRsdFileDataSource` always reads the header, decodes RADP to PCM. Music split
  `music00-03.rcf` (slot count 10).
- Sound memory: clip budget (PS2 `TOTAL_PS2_FREE_UNCOMPRESSED_CLIP_BYTES`) and root allocations doubled on Linux; a
  failed clip finishes as silence (else `daSoundDynaLoadRegion`, serial, stalled every later clip/dialogue).
- Resource file names become packed radKey32s at lockdown (`SetResourceLockdown`); read them as radKey32[], not
  `FileId[]` (64-bit stride: multi-file resources played a neighbour's clip or noise).
- Logs: `SHAR_SOUND_LOG=1` (players, ducking, queue, memory), `SHAR_DIALOG_LOG=1` (keys = radMakeCaseInsensitiveKey32
  of the path in dialog.rcf), `SHAR_RSD_LOG=1`. No speakers: `SDL_AUDIODRIVER=disk SDL_DISKAUDIOFILE=out.raw`
  (f32 stereo 48 kHz).

## Movies (`radmovie/src/linux/bink.h`, `binkffmpeg.cpp`)
- Bink-SDK stand-in over FFmpeg; wall clock; audio decoded ~0.5 s ahead into the mixer's movie stream.
- `BinkOpen` rejects non-Bink (some retail copies have Ogg logo movies; 1280x720 overflowed the 9-tile renderer).
  Mods: up to 1920x1080 (64 tiles), fitted to window.
- Tile texels past the picture repeat its last column/row (`binkrenderstrategy.cpp UnlockDestination`): filtering
  at the right/bottom edge reads them (uninitialised = noisy strip against the bars, both renderers).

## Files
- radcore drive: POSIX, case-insensitive, every drive letter = game dir.
- Game dir = cwd. `main` (`win32main.cpp FindGameData`): cwd lacking art/+scripts/ -> exe's dir, else message box
  and exit (no data used to hang on a black window).
- Missing file: `OnDriveError` prints `File not found: <path>` and fails the request (original retried forever).
  AnimatedIcon with neither its drawable nor `triggersphere_000` loaded gets an empty drawable (`EmptyDrawable`).
  `tFileFTT` loads a failed open as empty. Mod-occluded files set `radDrive::SetLastError`.
- DSG `SwapArray::Add` refuses when full; `WorldScene` prints `DSG: ...` for unplaceable objects (invisible, collidable).
