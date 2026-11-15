# The Simpsons Hit & Run: Linux/macOS port

<img width="2538" height="777" alt="Screenshot From 2026-11-02 18-21-11" src="https://github.com/user-attachments/assets/d17480cd-92df-433c-88dd-12667ec562f6" />

A native 64-bit Linux and macOS build of The Simpsons Hit & Run (2003): SDL2 and Vulkan 1.3, with OpenGL as a
fallback. X11 and Wayland, any resolution and aspect ratio, anti-aliasing, controllers, dynamic sun shadows and
optional post-processing shaders.

**Bring your own game.** No game data is included. You need your own copy of the PC game, the folder holding `art/`,
`scripts/` and the `.rcf` files.

> **AI disclaimer.** Aspects of this port are written with the help of an AI coding
> assistant (Claude). Builds are always manually playtested for bugs and crashes.

| Original (2003) | This project |
|---|---|
| Direct3D 8, fixed-function | Vulkan 1.3 (default) or OpenGL 3.3 core, shaders matching Direct3D 8's output |
| Blob shadows | Dynamic sun shadows (Vulkan): ray traced on RTX-class GPUs, shadow map on any other |
| DirectInput | SDL2: keyboard, mouse, any modern gamepad, rumble |
| DirectSound + EAX hardware reverb | Software 3D mixer with EAX-style reverb |
| Bink video | FFmpeg |
| 640x480 to 1600x1200, 4:3 | Any resolution, any aspect ratio, resizable window |

## Running

**Download the latest release, [v0.3.0](https://github.com/zeropaulsy/hit-and-run-linux/releases/tag/v0.3.0)**: a
Linux tarball and AppImage, and a macOS app (prerelease). The release notes explain how to start it; older builds
are on the [Releases](https://github.com/zeropaulsy/hit-and-run-linux/releases) page.

- **Linux:** x86-64, Ubuntu 24.04 / Linux Mint 22 or newer (glibc 2.38+), a Vulkan 1.3 driver (NVIDIA, or Mesa for
  AMD / Intel) or OpenGL 3.3. High shadows need ray tracing (`VK_KHR_ray_query`).
- **macOS:** Apple silicon; Vulkan through MoltenVK, or OpenGL 4.1. Intel Macs: build from source.

From a source checkout:
```bash
port/play.sh --data "/path/to/The Simpsons - Hit & Run"
```
The first run builds the game, creates `run/` with links to your game data and its own `simpsons.ini` and saves,
then starts it. Your original install is never written to. Add `--no-build` to skip the build, `--mod X` to load a
mod, or game options such as `skipfe` (straight into Level 1), `bonus` (Bonus Game unlocked) or `console`
(developer console on the ` key; type `help`).

## Settings

Options > Graphics (front end or pause menu) holds the port's own settings, saved in `simpsons.ini`:

| Setting | Values | ini key |
|---|---|---|
| Renderer | Vulkan (default), OpenGL; applies at the next start | `renderer=vulkan\|opengl` |
| Anti-aliasing | Off, 2x, 4x (default), 8x | `antialiasing=` |
| Shaders | Off, or a pack from `shaders/`: `vanilla-pack`, `soft-pack`, `toon-pack` or [your own](shaders/README.md) | `shaders=` |
| Shadows | Off, Low (default), High; Vulkan only. **F9** cycles them in game | `shadows=off\|low\|high` |
| Wide HUD | Off (default), On: HUD at the window edges on wide and tall windows | `widehud=yes` |

Sun shadows follow the sun or moon painted in each level's sky. **Low** bakes the world's shadow into a map on any
Vulkan GPU, and cars and characters keep blob shadows. **High** ray traces cars and characters too, and takes the sun
off covered places such as tunnels. OpenGL draws the original blob shadows. On macOS, High runs as Low.

**Language:** `language=english` (default), `french`, `german` or `spanish` in `simpsons.ini` sets the text, loading
screens, speech and localized movies. Speech needs that language's voice file in your game folder (`dialogf.rcf`,
`dialogg.rcf` or `dialogs.rcf`); without it you get English voices with translated text.

## Building from source

**Linux** dependencies, Debian / Ubuntu:
```bash
sudo apt install cmake ninja-build python3 build-essential \
    libsdl2-dev libgl-dev libvulkan-dev libopenal-dev \
    libavformat-dev libavcodec-dev libswscale-dev libswresample-dev libavutil-dev
```
Arch:
```bash
sudo pacman -S --needed cmake ninja python base-devel \
    sdl2 libglvnd vulkan-headers vulkan-icd-loader openal ffmpeg
```
Fedora (`ffmpeg-free-devel`, or `ffmpeg-devel` from RPM Fusion; on RHEL enable EPEL and CRB first):
```bash
sudo dnf install cmake ninja-build python3 gcc-c++ \
    SDL2-devel libglvnd-devel vulkan-headers vulkan-loader-devel openal-soft-devel ffmpeg-free-devel
```

**macOS:** the Xcode Command Line Tools and [Homebrew](https://brew.sh/):
```bash
xcode-select --install
brew install cmake ninja pkgconf sdl2 ffmpeg openal-soft molten-vk vulkan-loader vulkan-headers vulkan-tools
```
On Intel Macs `play.sh` sets up an x86-64 build itself, using MacPorts if installed (tested on Big Sur), otherwise
Homebrew. Details in [`port/notes/macos.md`](port/notes/macos.md).

Then build and run:
```bash
cmake -B build-linux -G Ninja                              # release (build-macos on macOS)
cmake -B build-linux-debug -G Ninja -DSHAR_RAD_DEBUG=ON    # debug: asserts and Radical's debug output
ninja -C build-linux
port/play.sh --data DIR                                    # configures and builds if needed
```

Boot movies only play in release builds. Packages: `port/package.sh` makes a self-contained Linux tarball
(`--noble` builds it in an Ubuntu 24.04 container, `--appimage` adds an AppImage, `--32` the legacy 32-bit build);
`port/package_macos.sh` makes an ad hoc signed `.app`, a zip and a `.dmg`.

`CMakeLists.txt` is generated from the original Visual Studio projects: change `port/gen_cmake.py`, never the
generated file. Porting details and debug switches: [`CLAUDE.md`](CLAUDE.md) and `port/notes/`.

## Mods (work in progress)

Mods for Lucas' Simpsons Hit & Run Mod Launcher load without the launcher, as a `.lmlm` package or an extracted mod
folder (the one holding `Meta.ini`). Back up your saves first.

```bash
port/play.sh --data DIR --mod ~/mods/SomeMod.lmlm --mod ~/mods/AnotherModFolder
```
On macOS, drag mods onto the app's icon. Repeat `--mod` to load several; if two replace the same file, the first
wins. Mods load only for that run. Nearly every launcher hack works, including Lua scripting; the log
(`run/logs/last-session.log`) lists each mod loaded and any hack it uses that isn't supported yet. Details in
[`port/notes/mods.md`](port/notes/mods.md).

## Platforms

| Platform | Status |
|---|---|
| Linux x86-64 | **Fully playtested**: game completed to 100% |
| Linux x86 (32-bit, legacy) | Playable, tested through Levels 1-7 |
| macOS Apple silicon / Intel | **Prerelease.** Runs end to end on an Apple M2 (MoltenVK) and Intel Big Sur (OpenGL); no long play session yet |
| Windows x64 | Planned |

Vulkan is tested on NVIDIA and Mesa's lavapipe; AMD / Intel hardware not yet.

## Repository layout

| Path | Contents |
|---|---|
| `code/` | The game |
| `libs/` | Radical's libraries: Pure3D, radcore, radsound, radmovie, Scrooby and more |
| `build/` | The original Visual Studio projects (dev branch only) |
| `port/` | Everything added for the port: build generator, platform layer, mod support, scripts |
| `shaders/` | Shader packs and their documentation |
| `mod/tools/` | Modding tools (`.lmlm` extractor) |

## Credits

- [Donut Team](https://donutteam.com/), for Lucas' Simpsons Hit & Run Mod Launcher and their years of work on
  modding tools, documentation and research into the game. The mod support in this port follows the format and
  hacks their launcher defined. [Support them on Ko-fi](https://ko-fi.com/donutteam).
- [The Mod Bakery](https://modbakery.donutteam.com/), and the modders who publish there, for hosting and keeping
  the game's mods available over the years.

## License

The port's own work is licensed under the [Mozilla Public License 2.0](LICENSE): everything under `port/`
(except `port/third_party/`), `shaders/` and `mod/`, and the files the port adds elsewhere.

The MPL does not cover the original game. Its source in `code/`, `libs/` and `build/` is © Radical Entertainment
(see the notices in those files), and the port's changes to those files do not change their copyright. No game
data is included. Third-party code in `port/third_party/` (Lua, glslang, stb, DejaVu) keeps its own license, found
next to it.
