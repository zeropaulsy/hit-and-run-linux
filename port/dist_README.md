# The Simpsons: Hit & Run: Linux port, 2026-11-01

A 64-bit Linux build of *The Simpsons: Hit & Run* (2003). It is not an emulator and it does not use Wine.

> **Bring your own game.** No game data is included. You need your own copy of the PC game: the folder that holds `art/`, `scripts/` and the `.rcf` files.

**Requirements:** A 64-bit (x86-64) Linux as new as Ubuntu 24.04 / Linux Mint 22 (glibc 2.38 or newer), and a
graphics driver with Vulkan 1.3 (NVIDIA, or Mesa for AMD / Intel). Without Vulkan the game starts on OpenGL 3.3.

## What's new

**Graphics**
- New Vulkan 1.3 renderer, now the default. OpenGL stays as the fallback and is used automatically when Vulkan is
  missing; switch in Options > Graphics > Renderer (takes effect at the next start).
- Dynamic sun shadows (Vulkan only): the sun or moon painted in each level's sky casts shadows from buildings, trees,
  fences, cars and characters, with light that warms as the sun gets low and turns moon-blue on night levels.
  Options > Graphics > Shadows: Off, Low (default, any GPU) or High (ray traced, needs a ray-tracing GPU: adds car and
  character shadows and takes the sun off places under cover). F9 cycles them in game.
- Options > Graphics page: renderer, anti-aliasing, shaders on/off and shadows, from the front end or the pause menu.
- Compiled Vulkan pipelines are cached in `vkpipelines.cache`, so later starts don't pause to build them.
- Example shaders: SSAO with a full-resolution blur, bloom on by default at a lower strength, gentler tone mapping.

**Fixes**
- Credits roll matches the retail PC game (missing sections shown, the last lines no longer drawn over the first).
- Switching from exclusive fullscreen to borderless on Wayland no longer leaves the picture in the top-left corner.
- Held keys are no longer delayed on KDE Plasma.
- A noisy strip at the edge of movies is gone.
- The phone booth no longer shows the previous car's picture for a car without one.

**Developer console** (start with `./run.sh console`, toggle with the ` key, type `help`): cheats, coins,
teleporting, debug camera, cards, outfits, language switching, saving and loading, and the game's script functions.

**Mods:** almost every remaining Mod Launcher hack is now in: all of AdditionalScriptFunctionality (stage vehicle and
passenger settings, character models and forces, parked cars, CON file commands), the unlock and "no ..." hacks,
custom audio, bonus missions, trigger actions, skin previews, collector cards, headlights, camera hacks, raised reward
limits, Ogg/FLAC, video textures, the PS2 refraction shader, Frink's hover car refraction, skid marks on fake wheels
and material shaders from every mod.

## Running

```bash
./run.sh "/path/to/The Simpsons - Hit & Run"
```

After the first run, `./run.sh` alone is enough. It sets up a `run/` folder next to itself, with links to your game
data and its own copies of `simpsons.ini` and the saves, so your original install is never written to.

Alternatively, extract the release's contents straight into the game folder (next to `art/` and `scripts/`) and run
`./run.sh` there with no path. The game then runs in place: no `run/` folder is made, and `simpsons.ini`, the saves
and `logs/` are written in the game folder.

## Mods (experimental)

Lucas' Mod Launcher mods load without the launcher, either as a .lmlm package or as an extracted mod folder:
```
./run.sh --mod ~/mods/SomeMod.lmlm --mod ~/mods/AnotherModFolder
```
**Working:** Lua scripting, Ogg and FLAC audio, HD movies, custom text, car and skin shops, custom cars, characters
and dialogue, mission script extensions, trigger actions, bonus missions, interiors, raised engine limits and traffic.
Nearly every launcher hack is implemented (the [Sound] limits are the main gap), but many have only been tried with
small test mods. Mods can break missions or crash the game, so back up any saves you care about.

## Reporting a problem

Attach `run/logs/last-session.log`, or the crash log from `run/crashes/`, and say what you were doing. For mod issues,
include the mod's name and version.

## Credits

- [Donut Team](https://donutteam.com/), for Lucas' Simpsons Hit & Run Mod Launcher and their years of work on
  modding tools, documentation and research into the game. The mod support in this port follows the format and
  hacks their launcher defined.
- [The Mod Bakery](https://modbakery.donutteam.com/), and the modders who publish there, for hosting and keeping
  the game's mods available over the years.

---

The Simpsons: Hit & Run was developed by Radical Entertainment and published by Vivendi Universal Games. This project is not affiliated with or endorsed by them.
Built with SDL2, Vulkan, glslang, FFmpeg, Lua 5.4, libpng, stb_truetype and the DejaVu Sans Mono font.
