# CLAUDE.md: The Simpsons Hit & Run, native Linux port

Port of the 2003 source to Linux (x86-64 default, 32-bit legacy), SDL2 + OpenGL 3.3 core. Original dump:
`~/Documents/Decomp/hitnrun_source/game` (never modify). Base game plays end to end as far as tested (L7); retail PC
data works; Mod Launcher mods partly. Missing: steering-wheel FFB. `README.md` user overview, `TODO.md` checklist, `DONE.md` completed checklist,
`RUST.md` possible Rust/Bevy rewrite.

## Notes: read only the file for the subsystem you touch
| file | covers |
|---|---|
| `port/notes/build.md` | tools, gen_cmake rules, allocator isolation, GCC/MSVC differences, radscript, 64-bit |
| `port/notes/renderer.md` | GL pipeline, D3D8 parity fixes, textures, MSAA, user post/material shaders, aspect/resolutions |
| `port/notes/platform.md` | window/Wayland/GPU driver quirks, threads, frame timing/loading, input, audio, movies, files |
| `port/notes/testing.md` | game option details, interior doors, memcheck recipes + coverage, shutdown/robustness |
| `port/notes/mods.md` | mod loading, every implemented hack and where it's wired, test mods |

Keep notes terse: rules, symptoms, file/symbol names. No history (git has it). Update the relevant file when a lesson is learnt.

## Layout
```
code/, libs/, build/   dump's sources + VS projects (git-tracked)
port/                  everything added: gen_cmake.py (-> CMakeLists.txt, never edit by hand), check.py, run.sh,
                       play.sh, memcheck.sh, package.sh, compat/ (shims, srr2.map, shar_mod*.cpp), third_party/lua
shaders/               example post/material shaders (shaders/README.md)
build-linux/ release   build-linux-debug/ asserts   build-linux-asan/   build-linux-audit/ all warnings   (x86-64)
build-linux32[-debug]/ legacy 32-bit (-DSHAR_64BIT=OFF; play.sh/package.sh --32)
run/                   run dir (ignored, user's ini/saves). Experiments: own run dir in scratchpad via SHAR_RUN
```

## Commands
```bash
python3 port/gen_cmake.py                 # after changing port rules
ninja -C build-linux-debug -j6
SHAR_BUILD=$PWD/build-linux-debug port/run.sh 120 [args]   # gdb, backtrace on crash (absolute path)
port/play.sh --data DIR [--debug --32 --no-build --mod X]  # logs run/logs, crashes run/crashes
SHAR_RUN=<scratch run dir> port/memcheck.sh 330 skipfe
```
Game options: `skipfe [lN mM]`, `race1-4`, `demo lN`, `incar`, `tpN`, `skipsunday`, `nofail`, `autopass[N]`, `coins`,
`bonus`, `l0`. Env: `SHAR_SHOT_EVERY`, `SHAR_QUIT_AT`, `SHAR_KEY_SCRIPT="dik@start-end,..."` (E=18, Enter=28,
1000+n mouse), `SHAR_KEYPRESS_SCRIPT`, `SHAR_RESIZE_SCRIPT`, `SHAR_MINIMIZE_SCRIPT`, `SHAR_TP_POS`,
`SHAR_TP_INTERIOR_POS`, `SHAR_MOUSE_SCRIPT="x,y@ms,..."`, `SHAR_MAX_FRAME_MS`, `SHAR_ASSERT_ABORT=1`, `SHAR_FILL_ALLOC`/`SHAR_FILL_PDDI`, `SHAR_FPS_CAP`,
`SHAR_VSYNC=0`, `SHAR_MSAA`, `SHAR_X11=1`, `SHAR_NO_AUDIO=1`, `SHAR_GL_DEBUG/STATS=1`,
`SHAR_{FRAME,INPUT,SOUND,DIALOG,RSD,REVERB,RUMBLE,MOD,TRAFFIC,MATERIAL}_LOG=1`. Details: testing.md.
Mesa GL errors: `__GLX_VENDOR_LIBRARY_NAME=mesa LIBGL_ALWAYS_SOFTWARE=1 MESA_DEBUG=1`. No speakers:
`SDL_AUDIODRIVER=disk`. Debug logs contain NULs: `grep -a`. `perf` not permitted.

## Platform model
Linux is a PC platform: `RAD_WIN32` stays defined, `RAD_LINUX` marks OS differences (also `LINUX`, `RAD_PC`,
`PDDI_NO_ASM`). Win32 sources -> `linux/` siblings via gen_cmake; DX8 pddi -> GL pddi.

## Rules that bite (detail in notes)
- Compat layer: malloc only, never C++ new. `nm -D --defined-only srr2 | grep _Zn` must be empty. Failed POST_BUILD
  objcopy: delete the library.
- Uninitialised reads were zero under MSVC: zero-init, mark `// uninit fix (Linux port)`; audit build + memcheck after
  porting new code.
- CRLF sources: edit with Python `newline=''` or bytes.
- Rendering wrong: compare with `libs/pure3d/pddi/dx8` in the dump. Game renders with culling off.
- Black/frozen window at large sizes: `nvidia-smi` for gnome-shell VRAM leak first.
- Boot movies only in release builds. Debug-build teardown asserts are the original's: ignore.

## Priorities
1. Verify base game: the user plays (Claude doesn't script gameplay it can't see). L1-6 done, on L7. Riskiest: bonus
   game with 2-4 pads, races, later levels, other languages.
2. Memcheck coverage (Claude, unattended): every mission done; cutscenes, bonus game with player driving.
3. Robustness: crashes/asserts from those runs, shutdown paths.
4. Mods: remaining hacks (mods.md), tested with the user. 5. Packaging. 6. Wheel FFB. 7. Renderer follow-ups.
8. 64-bit long sessions (32-bit is legacy: keep it building).
