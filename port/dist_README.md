# The Simpsons: Hit & Run: Linux port, 2026-10-26

A 64-bit Linux build of *The Simpsons: Hit & Run* (2003). It is not an emulator and it does not use Wine.

> **Bring your own game.** No game data is included. You need your own copy of the PC game: the folder that holds `art/`, `scripts/` and the `.rcf` files.

**Requirements:** A 64-bit (x86-64) Linux as new as Ubuntu 24.04 / Linux Mint 22 (glibc 2.38 or newer).

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
Nearly every launcher hack is implemented, but many have only been tried with small test mods, and some mission
script commands are still missing. Mods can break missions or crash the game, so back up any saves you care about.

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
Built with SDL2, FFmpeg, Lua 5.4 and libpng.
