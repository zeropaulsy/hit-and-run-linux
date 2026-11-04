# Contributing

Bug reports, testing and pull requests are all welcome.

## Branches

| Branch | Use |
|---|---|
| [`dev`](https://github.com/zeropaulsy/hit-and-run-linux/tree/dev) | Active development. **Open pull requests against this one.** |
| [`macOS`](https://github.com/zeropaulsy/hit-and-run-linux/tree/macOS) | macOS port work. |
| [`main`](https://github.com/zeropaulsy/hit-and-run-linux/tree/main) | The version for players. Changes reach it from `dev`. |

## Reporting a bug

Include:

- What you did and what you expected to happen.
- Your distro, GPU, and renderer (Vulkan or OpenGL).
- `run/logs/last-session.log`, plus the report from `run/crashes/` if the game crashed.
- For mods, the mod's name and version.

Never attach or commit game data.

## Building

Build steps are in the [`dev` README](https://github.com/zeropaulsy/hit-and-run-linux/tree/dev#building-from-source).
You need your own copy of the PC game to run it. Use a debug build to get asserts:

```bash
cmake -B build-linux-debug -G Ninja -DSHAR_RAD_DEBUG=ON
ninja -C build-linux-debug
port/play.sh --data DIR --debug
```

Teardown asserts in debug builds come from the original game and can be ignored.

## Code rules

The full notes are in [`CLAUDE.md`](https://github.com/zeropaulsy/hit-and-run-linux/blob/dev/CLAUDE.md) and
[`port/notes/`](https://github.com/zeropaulsy/hit-and-run-linux/tree/dev/port/notes). The ones that catch people out:

- **Platform code:** Linux counts as a PC platform, so `RAD_WIN32` stays defined. Use `RAD_LINUX` for OS differences.
- **Build files:** `CMakeLists.txt` is generated. Change `port/gen_cmake.py` and rerun it, never the generated file.
- **Compat layer:** use `malloc` only, never C++ `new`. `nm -D --defined-only srr2 | grep _Zn` must print nothing.
- **Uninitialised reads:** these were zero under MSVC. Zero-initialise and mark the fix `// uninit fix (Linux port)`.
- **Line endings:** many sources are CRLF. Keep them that way, so diffs show only your change.
- **32-bit:** it's legacy, but it must keep building (`-DSHAR_64BIT=OFF`).

## Pull requests

- Keep each pull request to one change, and rebase it on `dev`.
- Say what you tested and what you couldn't test. Run the game as well as building it: list the levels or missions,
  renderer, and any mods used.
- If you learnt something about a subsystem, add it to the matching file in `port/notes/`. Keep it short: rules,
  symptoms and file or symbol names.
