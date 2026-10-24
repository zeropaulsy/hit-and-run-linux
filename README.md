# The Simpsons Hit & Run: Linux port

<img width="4476" height="1091" alt="Screenshot From 2026-10-23 20-03-31" src="https://github.com/user-attachments/assets/3def41df-9d76-474a-90c8-9d84a62eb772" />

A Linux 64-Bit build of The Simpsons Hit & Run (2003) using SDL2 and OpenGL 3.3. It runs on X11 and Wayland, with
widescreen & ultrawide, high resolutions, anti-aliasing, anisotropic filtering, controllers and optional post-processing shaders.

**Bring your own game.** This release has no game data. You need your own copy of the PC game, installed or copied
from the disc: the folder holding `art/`, `scripts/` and the `.rcf` files.

| Original (2003) | This project |
|---|---|
| Direct3D 8, fixed-function | OpenGL 3.3 core, GLSL shaders matching Direct3D 8's output |
| DirectInput | SDL2: keyboard, mouse, any modern gamepad, rumble |
| DirectSound + EAX hardware reverb | Software 3D mixer with EAX-style reverb in software |
| Bink video | FFmpeg |
| Win32 threads, files, windowing | Portable platform layer |
| 640x480 to 1600x1200, 4:3 | Any resolution, any aspect ratio, resizable window |

## Requirements

A 64-bit (x86-64) Linux with glibc 2.43 or newer (Ubuntu 26.04 or a distribution of the same age) and a desktop. The
graphics driver and X11 / Wayland libraries come from your system; everything else is included.

## Quick start

```bash
port/play.sh --64 --data "/path/to/The Simpsons - Hit & Run"
```

The first run builds the game, creates `run/` with links to your game data and its own `simpsons.ini` and save
files, then starts the game. Your original install is never written to.

Useful options:

```bash
port/play.sh --64 --data DIR --mod MyMod.lmlm    # load a mod (repeatable; earlier mods win)
port/play.sh --64 --data DIR --no-build          # skip the build step
port/play.sh --64 --data DIR skipfe              # straight into Level 1
port/play.sh --64 --data DIR bonus               # Bonus Game and its tracks open without completed card decks
```
## Building from source

### 64-bit build (Recommended):
```bash
sudo apt install cmake ninja-build python3 build-essential \
    libsdl2-dev libgl-dev libopenal-dev \
    libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libavutil-dev
```
Then:
```bash
cmake -B build-linux64 -G Ninja -DSHAR_64BIT=ON && ninja -C build-linux64
port/play.sh --64 --data DIR                # builds and runs it (configures build-linux64 if needed)
```

### 32-bit build (enable i386 with `sudo dpkg --add-architecture i386`):
```bash
sudo apt install cmake ninja-build python3 gcc-multilib g++-multilib \
    libsdl2-dev:i386 libgl-dev:i386 libopenal-dev:i386 \
    libavformat-dev:i386 libavcodec-dev:i386 libswscale-dev:i386 libswresample-dev:i386 libavutil-dev:i386
```
Then:
```bash
cmake -B build-linux -G Ninja                              # release build
cmake -B build-linux-debug -G Ninja -DSHAR_RAD_DEBUG=ON    # debug build: asserts and Radical's debug output
ninja -C build-linux
```

Boot movies only play in the release build. `port/package.sh` (`--64` for the 64-bit build) makes a self-contained
tarball for another Linux machine. The porting details are in [`CLAUDE.md`](CLAUDE.md).

`CMakeLists.txt` is generated from the original Visual Studio projects: change `port/gen_cmake.py`, never the
generated file.

<details>
<summary>Debug and test switches</summary>

| Variable | Effect |
|---|---|
| `SHAR_FPS_CAP=n` | Frame-rate cap (default 60, 0 = off) |
| `SHAR_VSYNC=0` | Turn vsync off |
| `SHAR_FRAME_LOG=1` | Print a frame-time histogram every 5 s |
| `SHAR_NO_AUDIO=1` | Run without an audio device |
| `SHAR_SHOT_EVERY=n` | Save `shot_NNN.png` in the run directory every n seconds |
| `SHAR_KEY_SCRIPT="dik@start-end,..."` | Hold DirectInput key codes during time windows (ms; 1000+n = mouse button n) |
| `SHAR_RESIZE_SCRIPT="WxH@ms,..."` | Resize the window at those times |
| `SHAR_QUIT_AT=ms` | Close the window at that time (tests shutting down from any context) |
| `SHAR_KEYPRESS_SCRIPT="M@ms,..."` | Press keys (SDL key names) at those times, through the window's event queue |
| `SHAR_INPUT_LOG`, `SHAR_NO_INPUT`, `SHAR_NO_MOUSE`, `SHAR_FORCE_MOUSELOOK` | Input debugging |
| `SHAR_ASSERT_ABORT=1` | Make failed asserts abort instead of logging |
| `SHAR_LOAD_BUDGET_MS=n` | Time per frame given to the background loader |
| `SHAR_CRASH_HANDLER=0` | Don't install the crash-report signal handler |
| `SHAR_MSAA=n` | Anti-aliasing samples, overriding `simpsons.ini` (0 = off) |
| `SHAR_ALPHA_TO_COVERAGE=0` | Hard edges on cut-out textures even with anti-aliasing |
| `SHAR_MIPMAPS=0`, `SHAR_ANISOTROPY=n` | Texture filtering, overriding `simpsons.ini` |
| `SHAR_SHADERS=dir`, `SHAR_POST=0` | Shaders folder (default `shaders/`); post-processing off |
| `SHAR_MATERIAL_LOG=1` | Print each material's name and type when first drawn (for `materials.ini`) |
| `SHAR_GL_DEBUG=1`, `SHAR_GL_STATS=1` | OpenGL debug messages; per-frame draw/upload counts and render times |

`port/run.sh [seconds] [args]` rebuilds and runs the game under gdb, printing a backtrace on a crash.

</details>

## Repository layout

| Path | Contents |
|---|---|
| `code/` | The game |
| `libs/` | Radical's libraries: Pure3D renderer, radcore, radsound, radmovie, Scrooby UI and more |
| `build/` | The original Visual Studio projects, read by the generator |
| `port/` | Everything added for the port: build generator, platform layer, mod support, scripts |
| `shaders/` | Example shaders and their documentation |
| `mod/tools/` | Modding tools (`.lmlm` extractor) |

## Mods (work in progress)

> **Mod support is a work in progress and is here for testing.** Many mods load and play, but some launcher
> features are missing or only partly done, and mods may show wrong textures, break missions or crash the game. Don't
> rely on it for a full playthrough yet, and keep backups of any saves you care about.

Mods made for Lucas' Simpsons Hit & Run Mod Launcher load without the launcher. Give `run.sh` the mod with `--mod`:
either a `.lmlm` package (read in place, no need to unpack it) or an extracted mod folder (the one holding
`Meta.ini`):

```bash
port/play.sh --64 --data "/path/to/simpsons" --mod "/path/to/SomeMod.lmlm"
port/play.sh --64 --data "/path/to/simpsons" --mod ~/mods/SomeMod.lmlm --mod ~/mods/AnotherModFolder
```

- Repeat `--mod` to load several. If two mods replace the same file, the one given first wins.
- Mods load only for that run. Start without `--mod` to play the unmodded game.
- A mod's settings take the defaults from its `Meta.ini`. To change them, create `run/ModSettings.ini` with a
  `[ModInternalName]` section and `SettingName=value` lines.
- Total-conversion mods (`Main=1` in `Meta.ini`) keep their own saves in `run/ModSaves/<ModInternalName>/`, apart
  from the normal game's.
- At startup the log (`run/logs/last-session.log`) lists each mod loaded and any launcher hacks it needs that aren't
  supported yet. Those parts of the mod are ignored. For more detail, run with `SHAR_MOD_LOG=1 ./run.sh ...`.

What works so far: Lua scripting, Ogg audio, HD movies, custom text, car shops and skin shops, the 3D phone booth
preview, custom cars, new dialogue characters, mission script extensions, moved or added interiors, raised engine
limits, custom traffic and per-road traffic behaviour. 

Not done yet: custom characters' animation sets, custom audio,
custom bonus missions, skin preview animations, shader parameter overrides, custom trigger actions and hover car
refraction.

Tested so far: the Futurama demo (level 1), Fully Connected Map (level 1) and SHAR Remastered (boots). If a mod
misbehaves, report the mod's name and version along with `run/logs/last-session.log`.

## Platforms

| Platform | Status |
|---|---|
| Linux x86 (32-bit) | **Playable.** Tested through Level 7 |
| Linux x86-64 | **Boots and plays** (tested: movies, front end, levels, races, interiors, the bonus game, mods); long play sessions in progress |
| Windows x64 | Planned |
| macOS (Apple silicon / Intel) | Planned |
| Android / handhelds | Possible later: the renderer and input layers already allow it |

