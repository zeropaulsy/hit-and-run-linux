# The Simpsons: Hit & Run for macOS (prerelease)

A native Apple silicon build of *The Simpsons: Hit & Run* (2003), made from the Linux port. It is not an emulator and does not
use Wine or Rosetta. It renders with Vulkan through MoltenVK (Metal); OpenGL 4.1 is the fallback.

> **Bring your own game.** No game data is included. You need your own copy of the PC game: the folder that holds `art/`,
> `scripts/` and the `.rcf` files.

> **Prerelease.** It runs end to end in testing, but long play sessions on the Mac are still to come. Please report
> crashes along with the log file (see below).

**Requirements:** a Mac with Apple silicon (M1 or newer) and the macOS version given in the release notes.

## Installing

1. Unzip and move **Simpsons Hit & Run** to Applications (or anywhere).
2. The app isn't signed with an Apple Developer ID, so the first start is blocked. Open it once, click **Done**, then go to
   **System Settings > Privacy & Security**, scroll down, and click **Open Anyway** next to the message about the app.
   (Or in Terminal: `xattr -dr com.apple.quarantine "/Applications/Simpsons Hit & Run.app"`.)
3. On the first start, choose your PC game folder.

## Where things go

Everything the game writes is in `~/Library/Application Support/Simpsons Hit & Run/`: `simpsons.ini` (settings), the
saves, `shaders/` (editable copies of the example shaders), `vkpipelines.cache` and `logs/last-session.log`. The game data
folder itself is left untouched; settings and saves already in it are copied in on the first start.

- To pick a different game folder, hold **Option** while starting the app.
- Game options can be passed from Terminal: `open -a "Simpsons Hit & Run" --args console` (the developer console; toggle
  it with the ` key and type `help`).
- Renderer, anti-aliasing, shaders and shadows: Options > Graphics. Shadows are Low only on the Mac (no ray tracing).

## Mods (experimental)

Lucas' Mod Launcher mods load without the launcher, as a `.lmlm` package or an extracted mod folder (the one holding
`Meta.ini`). Drag one or more onto the app's icon (in the Dock or the Finder) while the game isn't running, or
right-click a mod and choose **Open With > Simpsons Hit & Run**. From Terminal, give `--mod` the mod's full path:
```bash
open -a "Simpsons Hit & Run" --args --mod ~/mods/SomeMod.lmlm --mod ~/mods/AnotherModFolder
```
- Repeat `--mod` (or drop several) to load more than one. If two mods replace the same file, the one given first wins.
- Mods load only for that start. Start the app normally to play the unmodded game.
- Total-conversion mods keep their own saves in `ModSaves/` in the folder above.
