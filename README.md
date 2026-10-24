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

**Requirements:** a Linux distro with OpenGL 3.3 and glibc 2.43 or newer (eg. Ubuntu 26.04).

## Running

```
tar xzf shar-linux-x86_64-20261023.tar.gz
cd shar-linux-x86_64-20261023
./run.sh "/path/to/The Simpsons - Hit & Run"
```

The first start checks your system for the libraries and lists any that are missing. After that, `./run.sh` alone is enough. The game keeps its settings and saves in a run/ folder next to itself and never writes to your original install.

## Mods (experimental)

Lucas' Mod Launcher mods load without the launcher, either as a .lmlm package or as an extracted mod folder:
```
./run.sh --mod ~/mods/SomeMod.lmlm --mod ~/mods/AnotherModFolder
```
These work: Lua scripting, Ogg audio, HD movies, custom text, car and skin shops, custom cars, new dialogue characters, mission script extensions, interiors, raised engine limits and traffic. These don't work yet: custom character animation sets, custom audio, custom bonus missions and a few other hacks. Mods can break missions or crash the game, so back up any saves you care about.

Reporting a problem

Attach run/logs/last-session.log, or the crash log from run/crashes/, and say what you were doing. For mod issues, include the mod's name and version.

---

The Simpsons: Hit & Run was developed by Radical Entertainment and published by Vivendi Universal Games. This project is not affiliated with or endorsed by them. Built with SDL2, FFmpeg, Lua 5.4 and libpng.
