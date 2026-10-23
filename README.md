# The Simpsons Hit & Run: native Linux port (binary release)

A native Linux 64-Bit build of The Simpsons Hit & Run (2003) using SDL2 and OpenGL 3.3. It runs on X11 and Wayland, with
widescreen, high resolutions, anti-aliasing, anisotropic filtering, controllers and optional post-processing shaders.

**Bring your own game.** This release has no game data. You need your own copy of the PC game, installed or copied
from the disc: the folder holding `art/`, `scripts/` and the `.rcf` files.

## Requirements

A 64-bit (x86-64) Linux with glibc 2.43 or newer (Ubuntu 26.04 or a distribution of the same age) and a desktop. The
graphics driver and X11 / Wayland libraries come from your system; everything else is included.

## Running

```bash
./run.sh "/path/to/The Simpsons - Hit & Run"
```

The first start runs `setup.sh`, which unpacks the movie player's libraries (`ffmpeg-libs.tar.xz`) into `lib/` 
You can also run `./setup.sh` yourself first.

After the first run, just use, `./run.sh`. It sets up a `run/` folder next to itself, with links to your game
data and its own copies of `simpsons.ini` and the saves, so your original install is never written to.

- Settings: `run/simpsons.ini` (`#System` section: display mode, resolution, anti-aliasing, mipmaps, anisotropy)
- Shaders: `shaders/` (see `shaders/README.md`) press M in the game to turn them off and on, or edit `shaders/post.ini` and `shaders/materials.ini`.
- Options: `./run.sh skipfe` skips the front end and goes straight into level 1
- Mods: `./run.sh --mod /path/to/SomeMod.lmlm` (work in progress, see [Mods](#mods-work-in-progress))

## Mods (work in progress)

> **Mod support is a work in progress and is here for testing.** Many mods load and play, but some launcher
> features are missing or only partly done, and mods may show wrong textures, break missions or crash the game. Don't
> rely on it for a full playthrough yet, and keep backups of any saves you care about.

Mods made for Lucas' Simpsons Hit & Run Mod Launcher load without the launcher. Give `run.sh` the mod with `--mod`:
either a `.lmlm` package (read in place, no need to unpack it) or an extracted mod folder (the one holding
`Meta.ini`):

```bash
./run.sh "/path/to/The Simpsons - Hit & Run" --mod "/path/to/SomeMod.lmlm"
./run.sh --mod ~/mods/SomeMod.lmlm --mod ~/mods/AnotherModFolder     # several mods; the game path is remembered
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

## What this is

*The Simpsons: Hit & Run* shipped in 2003 for Windows XP, Direct3D 8 and 32-bit x86. The original PC
release survives through compatibility modes, community patches and Wine. This project removes those layers and is compiled for modern operating systems,
with everything Windows-specific under it rewritten:

| Original (2003) | This project |
|---|---|
| Direct3D 8, fixed-function | OpenGL 3.3 core, GLSL shaders matching Direct3D 8's output |
| DirectInput | SDL2: keyboard, mouse, any modern gamepad, rumble |
| DirectSound + EAX hardware reverb | Software 3D mixer with EAX-style reverb in software |
| Bink video | FFmpeg |
| Win32 threads, files, windowing | Portable platform layer |
| 640x480 to 1600x1200, 4:3 | Any resolution, any aspect ratio, resizable window |

## Platforms

| Platform | Status |
|---|---|
| Linux x86 (32-bit) | **Playable.** Tested through Level 7 |
| Linux x86-64 | **Boots and plays** (tested: movies, front end, levels, races, interiors, the bonus game, mods); long play sessions in progress |
| Windows x64 | Planned |
| macOS (Apple silicon / Intel) | Planned |
| Android / handhelds | Possible later: the renderer and input layers already allow it |
