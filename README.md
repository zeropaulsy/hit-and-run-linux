# The Simpsons Hit & Run: Linux port

<img width="2538" height="777" alt="Screenshot From 2026-11-02 18-21-11" src="https://github.com/user-attachments/assets/d17480cd-92df-433c-88dd-12667ec562f6" />

A 64-Bit Linux/macOS build of The Simpsons Hit & Run (2003) using SDL2 and Vulkan 1.3, with OpenGL 3.3 as a fallback. It runs on X11 and Wayland, with widescreen & ultrawide, high resolutions, anti-aliasing, anisotropic filtering,
controllers, dynamic sun shadows and optional post-processing shaders.

**Bring your own game.** This release has no game data. You need your own copy of the PC game, the folder holding `art/`, `scripts/` and the `.rcf` files.

> **AI disclaimer.** Aspects of this port are written with the help of an AI coding
> assistant (Claude). Builds are always manually playtested for bugs and crashes.

| Original (2003) | This project |
|---|---|
| Direct3D 8, fixed-function | Vulkan 1.3 (default) or OpenGL 3.3 core, shaders matching Direct3D 8's output |
| Blob shadows under characters, cars and props | Dynamic sun shadows (Vulkan): ray traced on RTX-class GPUs, shadow map on any other |
| DirectInput | SDL2: keyboard, mouse, any modern gamepad, rumble |
| DirectSound + EAX hardware reverb | Software 3D mixer with EAX-style reverb in software |
| Bink video | FFmpeg |
| Win32 threads, files, windowing | Portable platform layer |
| 640x480 to 1600x1200, 4:3 | Any resolution, any aspect ratio, resizable window |

## Requirements

A 64-bit (x86-64) Linux as new as Ubuntu 24.04 / Linux Mint 22 (glibc 2.38 or newer), and a GPU driver with Vulkan
1.3 (NVIDIA, or Mesa for AMD / Intel). Without Vulkan the game starts on OpenGL 3.3 instead. High shadows need a GPU
with ray tracing (`VK_KHR_ray_query`); without it they run as Low. The graphics driver and X11 / Wayland libraries
come from your system; everything else is included.

## Quick start

```bash
port/play.sh --data "/path/to/The Simpsons - Hit & Run"
```

The first run builds the game, creates `run/` with links to your game data and its own `simpsons.ini` and save
files, then starts the game. Your original install is never written to.

Useful options:

```bash
port/play.sh --data DIR --mod MyMod.lmlm    # load a mod (repeatable; earlier mods win)
port/play.sh --data DIR --no-build          # skip the build step
port/play.sh --data DIR skipfe              # straight into Level 1
port/play.sh --data DIR bonus               # Bonus Game and its tracks open without completed card decks
port/play.sh --data DIR console             # developer console on the ` key (or SHAR_CONSOLE=1); type help
port/play.sh --data DIR --32                # the legacy 32-bit build
```

## Graphics settings

Options > Graphics (front end or pause menu) holds the port's own settings, saved in `run/simpsons.ini`:

| Setting | Values | ini key |
|---|---|---|
| Renderer | Vulkan (default), OpenGL; takes effect at the next start | `renderer=vulkan\|opengl` |
| Anti-aliasing | Off, 2x, 4x (default), 8x | `antialiasing=` |
| Shaders | Off, or a shader pack from `shaders/` (`vanilla-pack`, `soft-pack`, `toon-pack`, your own) | `shaders=` |
| Shadows | Off, Low (default), High; Vulkan only | `shadows=off\|low\|high` |

**Sun shadows** come from the sun (or moon) painted in each level's sky, so they fall the way the sky suggests; the light warms as the sun gets low and turns moon-blue on the night levels.

- **Low** bakes the world's shadow into a map and works on any Vulkan GPU. Cars and characters keep the game's blob
  shadows. Costs about half a millisecond a frame on an RTX 5060.
- **High** ray traces cars and characters into the sun's shadow (no blobs), refines every shadow edge with rays,
  and takes the sun off places under cover (tunnels, the power plant). About 5 ms a frame on an RTX 5060 at 2560x1600
  with 8x MSAA.
- **F9** cycles Off, Low and High in game.

The OpenGL renderer has no sun shadows: it draws the original blob shadows. Compiled Vulkan pipelines are kept in
`run/vkpipelines.cache`, so later starts don't pause to build them.
## Language

Set the language in `simpsons.ini` (in `run/`), then start the game:

```ini
language=spanish
```

`english` (the default), `french`, `german` or `spanish` sets the text, the loading screens, the speech and the audio
of the localized movies. Speech needs that language's voice file in your game folder: `dialogf.rcf` (French),
`dialogg.rcf` (German) or `dialogs.rcf` (Spanish). Without it you get English voices with the translated text. On an
English copy, translated tutorials also have gaps where button names should be, as the game's translated text was
never adapted for PC keys.

## Building from source

### Linux:
```bash
sudo apt install cmake ninja-build python3 build-essential \
    libsdl2-dev libgl-dev libvulkan-dev libopenal-dev \
    libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libavutil-dev
```
Then:
```bash
cmake -B build-linux -G Ninja                              # release build
cmake -B build-linux-debug -G Ninja -DSHAR_RAD_DEBUG=ON    # debug build: asserts and Radical's debug output
ninja -C build-linux
port/play.sh --data DIR                                    # builds and runs it (configures build-linux if needed)
```

### macOS (work in progress)

You need the Xcode Command Line Tools and [Homebrew](https://brew.sh/), with `brew` on your `PATH`:
```bash
xcode-select --install
brew install cmake ninja pkgconf sdl2 ffmpeg openal-soft molten-vk vulkan-loader vulkan-headers vulkan-tools
```

**Apple silicon:**
```bash
cmake -S . -B build-macos -G Ninja                         # release build (optimised by default on macOS)
ninja -C build-macos
port/play.sh --data DIR                                    # builds and runs it (configures build-macos if needed)
```

**Intel:** `port/play.sh --data DIR` detects an Intel Mac and sets up the build for it on the first run. It uses MacPorts
packages if MacPorts is installed, otherwise Homebrew's. To run `cmake` yourself instead, set `SHAR_BREW` to where your
packages live:
- `-DSHAR_BREW=/usr/local` for Homebrew on Intel (not tested yet)
- `-DSHAR_BREW=/opt/local` for MacPorts (tested on Big Sur)
```bash
cmake -S . -B build-macos -G Ninja -DCMAKE_OSX_ARCHITECTURES=x86_64 -DSHAR_BREW=/usr/local
ninja -C build-macos
port/play.sh --data DIR
```
If the Vulkan SDK came from LunarG's installer, also pass `-DCMAKE_CXX_FLAGS="-isystem /usr/local/include"` to find
its headers.

The game runs on Vulkan through MoltenVK, or OpenGL 4.1 if the GPU doesn't get Vulkan 1.3. On macOS, High shadows run as Low, and 8x anti-aliasing drops to 4x on an Apple M2.

Boot movies only play in the release build. `port/package.sh` (`--32` legacy 32-bit build) makes a self-contained
tarball for another Linux machine; add `--noble` to build it in an Ubuntu 24.04 container (needs `podman`) so it runs
on Ubuntu 24.04 / Linux Mint 22 and newer, not just on systems as new as yours. The porting details are in [`CLAUDE.md`](CLAUDE.md).

`CMakeLists.txt` is generated from the original Visual Studio projects: change `port/gen_cmake.py`, never the generated file.

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
| `SHAR_MSAA=n` | Anti-aliasing samples, overriding `simpsons.ini` and Options > Graphics (0 = off) |
| `SHAR_ALPHA_TO_COVERAGE=0` | Hard edges on cut-out textures even with anti-aliasing |
| `SHAR_MIPMAPS=0`, `SHAR_ANISOTROPY=n` | Texture filtering, overriding `simpsons.ini` |
| `SHAR_SHADERS=dir`, `SHAR_POST=0` | Shaders folder (default `shaders/`), or one pack in it; post-processing off |
| `SHAR_MATERIAL_LOG=1` | Print each material's name and type when first drawn (for `materials.ini`) |
| `SHAR_RENDERER=vulkan\|opengl` | Renderer, overriding `simpsons.ini` and Options > Graphics |
| `SHAR_SUN=0\|low\|1` | Sun shadows off, Low or High, overriding `simpsons.ini` (0 also skips the ray-tracing setup) |
| `SHAR_SUN_SOFT`, `SHAR_SUN_RAYS`, `SHAR_SUN_SHADOW`, `SHAR_SUN_ELEVATION`, ... | Sun tuning: edge softness, rays, shade brightness, sun height (all listed in `port/notes/vulkan.md`) |
| `SHAR_VK_DEBUG=1`, `SHAR_VK_DEVICE=n` | Vulkan validation layers; pick the n-th GPU |
| `SHAR_VK_PIPELINE_CACHE=0` | Don't read or write `vkpipelines.cache` |
| `SHAR_GL_DEBUG=1`, `SHAR_GL_STATS=1` | OpenGL debug messages; per-frame draw/upload counts and render times (both renderers) |

`port/run.sh [seconds] [args]` rebuilds and runs the game under gdb, printing a backtrace on a crash.

</details>

## Repository layout

| Path | Contents |
|---|---|
| `code/` | The game |
| `libs/` | Radical's libraries: Pure3D renderer, radcore, radsound, radmovie, Scrooby UI and more |
| `build/` | The original Visual Studio projects (dev branch only) |
| `port/` | Everything added for the port: build generator, platform layer, mod support, scripts |
| `shaders/` | Shader packs and their documentation |
| `mod/tools/` | Modding tools (`.lmlm` extractor) |

## Mods (work in progress)

> **Mod support is a work in progress and is here for testing.** Many mods load and play, only a few launcher
> features are missing or only partly done. Backup saves before testing mods, playtesting is still in progress.

Mods made for Lucas' Simpsons Hit & Run Mod Launcher load without the launcher. Give `run.sh` the mod with `--mod`:
either a `.lmlm` package or an extracted mod folder (the one holding
`Meta.ini`):

```bash
port/play.sh --data "/path/to/simpsons" --mod "/path/to/SomeMod.lmlm"
port/play.sh --data "/path/to/simpsons" --mod ~/mods/SomeMod.lmlm --mod ~/mods/AnotherModFolder
```

- Repeat `--mod` to load several. If two mods replace the same file, the one given first wins.
- Mods load only for that run. Start without `--mod` to play the unmodded game.
- A mod's settings take the defaults from its `Meta.ini`. To change them, create `run/ModSettings.ini` with a
  `[ModInternalName]` section and `SettingName=value` lines.
- Total-conversion mods (`Main=1` in `Meta.ini`) keep their own saves in `run/ModSaves/<ModInternalName>/`, apart
  from the normal game's.
- At startup the log (`run/logs/last-session.log`) lists each mod loaded and any launcher hacks it needs that aren't
  supported yet. Those parts of the mod are ignored. For more detail, run with `SHAR_MOD_LOG=1 ./run.sh ...`.

What works: nearly every launcher hack, including Lua scripting, Ogg and FLAC audio, HD movies, custom text, car and
skin shops, the 3D phone booth preview, custom cars and characters, new dialogue characters, mission script
extensions, custom trigger actions, bonus missions and audio, moved or added interiors, raised engine limits, custom
traffic, per-road traffic behaviour, shader parameter overrides and video textures.

Not done yet: the CustomLimits [Sound] player limits above 64 clips / 9 streams, and CustomRoadBehaviour's
undocumented UTurnTolerance (ignored). Many hacks have only been tried with small test mods, not in full
playthroughs.

Tested so far: the Futurama demo (level 1), Fully Connected Map (level 1) and SHAR Remastered (boots). If a mod
misbehaves, report the mod's name and version along with `run/logs/last-session.log`.

## Platforms

| Platform | Status |
|---|---|
| Linux x86-64 (default) | **Main and side missions fully playtested.** Game completed to 100%: all story missions, bonus missions, street races, collector cards and cosmetics. |
| Linux x86 (32-bit, legacy) | **Playable.** Tested through Level 1 - 7 |
| Vulkan renderer | **Default.** Tested on NVIDIA and Mesa's software driver (lavapipe); AMD / Intel hardware not yet. OpenGL is the fallback |
| Windows x64 | Planned |
| macOS (Apple silicon / Intel) | **In progress.** Builds and runs on an Apple M2 (Vulkan through MoltenVK) and on Intel with Big Sur (OpenGL). No full play session yet |
| Android / handhelds | Possible later: the renderer and input layers already allow it |

## Extra docs

- [Shaders](shaders/README.md): shader packs, and writing your own post-processing and material shaders

## Credits

- [Donut Team](https://donutteam.com/), for Lucas' Simpsons Hit & Run Mod Launcher and their years of work on
  modding tools, documentation and research into the game. The mod support in this port follows the format and
  hacks their launcher defined. [Support them on Ko-fi](https://ko-fi.com/donutteam).
- [The Mod Bakery](https://modbakery.donutteam.com/), and the modders who publish there, for hosting and keeping
  the game's mods available over the years.
