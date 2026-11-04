# The Simpsons Hit & Run: Linux port

<img width="2538" height="777" alt="Screenshot From 2026-11-02 18-21-11" src="https://github.com/user-attachments/assets/d17480cd-92df-433c-88dd-12667ec562f6" />

A Linux 64-Bit build of The Simpsons Hit & Run (2003) using SDL2 and Vulkan 1.3, with OpenGL 3.3 as a fallback. It
runs on X11 and Wayland, with widescreen & ultrawide, high resolutions, anti-aliasing, anisotropic filtering,
controllers, dynamic sun shadows and optional post-processing shaders.

**Bring your own game.** This release has no game data. You need your own copy of the PC game, installed or copied
from the disc: the folder holding `art/`, `scripts/` and the `.rcf` files.

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
sudo apt install cmake ninja-build python3 build-essential \
    libsdl2-dev libgl-dev libvulkan-dev libopenal-dev \
    libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libavutil-dev
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
```
## Developers

Build options, debug switches and the repository layout are in the README on the
[`dev` branch](https://github.com/zeropaulsy/hit-and-run-linux/tree/dev), which is also where development happens.

## Graphics settings

Options > Graphics (front end or pause menu) holds the port's own settings, saved in `run/simpsons.ini`:

| Setting | Values | ini key |
|---|---|---|
| Renderer | Vulkan (default), OpenGL; takes effect at the next start | `renderer=vulkan\|opengl` |
| Anti-aliasing | Off, 2x, 4x (default), 8x | `antialiasing=` |
| Shaders | The post-processing and material shaders in `shaders/` on or off | `shaders=` |
| Shadows | Off, Low (default), High; Vulkan only | `shadows=off\|low\|high` |

**Sun shadows** come from the sun (or moon) painted in each level's sky, so they fall the way the sky suggests; the
light warms as the sun gets low and turns moon-blue on the night levels. Everything casts: buildings, trees and
fences through their cut-out textures, cars, and characters, which also darken as they walk into shade. With the
sun on, the game's blob shadows and the trees' painted shadows are not drawn; interiors and the front end keep the
original look.

- **Low** bakes the world's shadow into a map and works on any Vulkan GPU. Cars and characters keep the game's blob
  shadows. Costs about half a millisecond a frame on an RTX 5060.
- **High** ray traces cars and characters into the sun's shadow (no blobs), refines every shadow edge with rays,
  and takes the sun off places under cover (tunnels, the power plant). About 5 ms a frame on an RTX 5060 at 2560x1600
  with 8x MSAA.
- **F9** cycles Off, Low and High in game, the easiest way to compare them.

The OpenGL renderer has no sun shadows: it draws the original blob shadows. Compiled Vulkan pipelines are kept in
`run/vkpipelines.cache`, so later starts don't pause to build them.

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
- Hover Car Refraction (the Xbox's see-through look for Professor Frink's hover car as it speeds up) is on when a mod
  needs it. To turn it on for the normal game too, add `[HoverCarRefraction]` with `Enabled=1` to
  `run/ModSettings.ini`. `PerCarScreenCapture=0` there captures the screen once per frame, not once per hover car.
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
| macOS (Apple silicon / Intel) | **In development.** See the [`dev` branch](https://github.com/zeropaulsy/hit-and-run-linux/tree/dev) |
| Android / handhelds | Possible later: the renderer and input layers already allow it |

## Credits

- [Donut Team](https://donutteam.com/), for Lucas' Simpsons Hit & Run Mod Launcher and their years of work on
  modding tools, documentation and research into the game. The mod support in this port follows the format and
  hacks their launcher defined. [Support them on Ko-fi](https://ko-fi.com/donutteam).
- [The Mod Bakery](https://modbakery.donutteam.com/), and the modders who publish there, for hosting and keeping
  the game's mods available over the years.
