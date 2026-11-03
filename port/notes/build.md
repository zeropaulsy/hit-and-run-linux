# Build, toolchain, 64-bit, macOS arm64

## Tools (port/)
- `vcx.py` reads the VS2013 .vcxproj files; `gen_cmake.py` -> CMakeLists.txt. Rules: `win32/` source -> `linux/`
  sibling or excluded; `LINUX_EXCLUDE`/`LINUX_EXTRA` per target; DX8 pddi -> GL pddi; bundled Windows SDK includes dropped.
- `fix_include_case.py` (applied), `gen_dinput.py` (compat/dinput.h), `syntax_survey.py`, `check.py`,
  `survey64.py [--sites]` (pointer-size casts by kind/library -> port/survey64.json; none left).
- `crash_symbolize.py BIN [LOG]` (addr2line on module+offset frames). `play.sh` copies `shaders/` into run/shaders (cp -n).
- compat/: msvc_compat.h (forced), types-only windows.h, crtdbg.h, dinput.h, win32_threads.* (threads/sync/fibers/timers),
  win32_misc.* (MessageBox), shar_crash.* (signal -> report on stderr; `SHAR_CRASH_HANDLER=0` off), pddi_heap.cpp,
  valgrind/ headers, srr2.map, shar_mod*.cpp.
- Audit build: grep -Wuninitialized/-Wmaybe-uninitialized; 2 known false positives (actordsg.cpp, opticlensflare.cpp).
- Deps: amd64 -dev SDL2, GL, OpenAL, FFmpeg, Vulkan (`libvulkan-dev`: headers only, the library loads at run time). Legacy 32-bit: gcc-multilib + the `:i386` ones (Bink:
  `libav{format,codec,util}-dev:i386 libsw{scale,resample}-dev:i386`).

## Allocator isolation
- Game replaces global operator new; on ELF that leaks into every shared lib. Fix: `-static-libstdc++ -static-libgcc
  -Wl,--exclude-libs,ALL`, `SHAR_LOCAL` on the game's operators, pddigl's new/delete objcopy-renamed to `pddi_heap_*`
  (symbol `_Znw<j|m>` per arch, `SHAR_NEW_SYM`).
- GCC ignores visibility("hidden") on replaceable operator new: `srr2.map` keeps them local (else libavcodec's static
  init on amd64 allocated from game heaps, freed into glibc). Check `nm -D --defined-only srr2 | grep _Zn` = empty.
- Failed POST_BUILD objcopy is not redone by ninja: delete the library.
- Compat layer runs inside the game's lazy heap init: malloc only, never C++ new.
- glslang (C++17, STL-heavy) is `libshar_glslang.so` with its own static libstdc++ and a 2-symbol version script
  (shar_glslang.map): linked statically it would allocate from the game's heaps.

## Compiler/runtime differences
- CreateThread stack size is a commit size (Windows reserves 1 MB): pthreads get >= 1 MB.
- GCC 15 libstdc++ asserts (`vector[]` past end, e.g. EventManager) off outside debug (`-D_GLIBCXX_NO_ASSERTIONS`).
- radscript: script classes by `typeid().name()` (MSVC format; `radtypeinfo.hpp` demangles); vtable slots in
  `srrtypes.typ` are MSVC's, match GCC only without virtual dtors/overloads in scripted interfaces. `InvokeVf` in C++.
- radoptions.hpp debug comm/watcher/memory monitor: off on Linux. Missing return = SIGILL under GCC (one found,
  GetTotalMemoryUsed; -Wreturn-type scan clean).
- Line endings: CRLF sources; Python `newline=''` or bytes (`Path.read_text()` converts to LF).
- `RAD_RELEASE` needed for boot movies (`SHOW_MOVIES`, bootupcontext.cpp).

## 64-bit (default, `SHAR_64BIT` ON; legacy 32-bit: OFF, `build-linux32`; tag `32bit-baseline` = last 32-bit-only)
- play.sh / package.sh reconfigure a build dir whose cached `SHAR_64BIT` is the other arch (old 32-bit `build-linux`).
- Changes (identical on 32-bit): GUI params `GuiParam` (uintptr_t), Locator data, radMemoryRound*, `shar_PointerKey()`;
  dlmalloc `long` not redefined on LP64, alignment 16; heaps x2 (`CreateHeap`); animation blocks x2 + 8-byte aligned;
  radscript args sorted into SysV registers (`InvokeVf64`); on-disk 32-bit fields kept: RCF header pointer, DDS
  `lpSurface`, type info strings; 32-bit masks on addresses fixed (`RoundUpAlignment`, movie negative pitch).
- Saves layout-identical (CharacterSheet 7140, SaveGameInfoData 16).
- Verified: L1-7, races, interiors, bonus game, mods, ASan clean; `package.sh`. Not yet: long sessions.

## macOS arm64 (branch `mac-arm64`; in progress, steps 1-6 done, step 7 open items left, step 8 awaiting the user's play test)
Status: native on an Apple M2, Vulkan via MoltenVK by default, OpenGL 4.1 as fallback. Scripted runs only on Vulkan; no full play session yet.
- Build: `export PATH=/opt/homebrew/bin:$PATH; ninja -C build-macos -j8` (`cmake -S . -B build-macos -G Ninja` if missing).
- Run (`run/` holds symlinks to `game-files/` and a copy of `shaders/`; both ignored by git):
  `cd run && ../build-macos/srr2`. Add `skipmovie`, or `skipfe demo l1` to self-run. `SHAR_RENDERER=opengl` for GL, `SHAR_NO_AUDIO=1` for silence.
- Benchmark: `SHAR_VSYNC=0 SHAR_FPS_CAP=1000 SHAR_FRAME_LOG=1 SHAR_GL_STATS=1 SHAR_QUIT_AT=45000`, `skipfe demo l1`; a
  `[frames]` line covers 5 s. Uncapped, 1920x1080, 4x MSAA: unoptimised 66 fps; optimised 66 fps; optimised + buffer fix
  ~165 fps; with the example shaders ~85 fps. Vulkan with the example shaders ~167 fps (GL ~80 in the same run), + `SHAR_SUN=low`
  ~133 fps. A self-running demo, not real play: the user's own test is still to do.
- Next: the user plays on Vulkan, then step 9.
- Open: Apple GL logged `unit 0 GLD_TEXTURE_INDEX_2D is unloadable` once (a draw with an incomplete texture; cause not
  found). 30 scripted methods return float, `InvokeVf64` reads the integer register (as on x86-64). Linux x86-64 rechecked after these changes: builds with GCC 15, `gen_cmake.py` output unchanged, scripted runs pass on both renderers.
- Linux builds have no optimisation either: `gen_cmake.py` sets no build type there, and play.sh/package.sh pass none.
  GCC `-O2` needs `-fno-lifetime-dse`: without it `AnimatedIcon::operator new`'s `mAllocated = true` is dropped, a pool slot is
  handed out twice, SIGSEGV in `AnimatedIcon::Move` (`demo l1`, ~30 s). With the flag: `demo l1`-`l7` clean; ~24% less CPU, same fps on Iris Plus.
  Not the default yet: the user decides after a real play session on an optimised build; memcheck/ASan not run at `-O2`.

Order of events (each step needs the one before; Linux builds must keep working throughout):
1. Set up: `brew install cmake ninja pkgconf sdl2 ffmpeg openal-soft molten-vk vulkan-loader vulkan-headers vulkan-tools`;
   retail game data on the Mac (still to do). Homebrew is `/opt/homebrew`, not on the default search paths.
2. Configure: macOS branch in `gen_cmake.py` (arch, linker flags, deps), `build-macos/`.
3. Compile (done: everything compiles and links, nothing run yet). Apple-only `-fdelayed-template-parsing` stands in for
   MSVC-style late template parsing; `port/compat/macos/malloc.h` supplies `<malloc.h>`. Don't add `using namespace std` to
   headers: libc++'s `std::ref` clashes with radcore's `ref<>`. `SHAR_LOCAL` is empty on macOS until step 4.
4. Link (done). Reaches `main` and the no-game-data message; not yet run with game data.
5. Runtime foundations (done). Fibers stay on ucontext: a stress test passed on arm64 (20,000 round trips on main and worker
   threads, deep stacks, FP values kept). `InvokeVf64` has an arm64 version, tested against real signatures.
6. First pixels (done): `SHAR_RENDERER=opengl SHAR_NO_AUDIO=1` on an M2 runs legal screen, movies, new game, loading screen and
   L1 in game, with a clean window-close exit. Run from a directory of symlinks to the game data, so the ini stays out of it.
7. Into the game: L1, input, audio, movies, then the other levels and mods. Scripted, OpenGL, no crashes:
   - Passed: `demo l1`-`l7` screenshots right; main menu seen; `SHAR_KEY_SCRIPT` moves Homer in `skipfe`; CoreAudio opens at
     48 kHz; `disk` captures of game and boot movie audio have structured spectra, no NaN; 0.05% of game samples clip
     (no Linux capture to compare). Own test mod: CustomFiles override and Lua handler output (mkstemp) both served in L1.
   - User confirmed: sound, controls, speed, Cmd+Q. Still to do: a real mod; pads and a full play session not reported.
   - Test traps: `demo` loads `demo.mfk`/`demoi.mfk`, not `level.mfk`; `SHAR_SHOT_EVERY` is seconds; macOS has no `timeout`;
     L1 `skipfe` stops at a tutorial popup (context 10). Characters' idle animations include deep bends: not a skinning bug. Don't search `~` for test data: it triggers macOS folder prompts.
8. Vulkan via MoltenVK (scripted runs pass, the user's play test to do). `WantVulkan` already defaults to Vulkan, so no switch was needed.
   - Same as GL by screenshot: `skipfe lN m1 nofail` L1-L7, legal screen, boot movie, main menu (mean diff 0.47/255). Runs aren't
     deterministic, so frames only roughly line up: differences looked at were animation, traffic and camera timing, no overall colour shift.
     An early `SHAR_SHOT_EVERY` shot can be the loading screen: look at the shots, don't trust a low diff alone.
   - Menu: `skipmovie` goes straight there (also skips the front end's intro, `StartIntroMovie`).
   - Passed: MSAA 2x/4x (8x clamps to 4x), no post shaders, window resize and minimise (`display=window` only: borderless
     ignores `SHAR_RESIZE_SCRIPT`), no ini = Vulkan, GL fallback with `SDL_VULKAN_LIBRARY=/nonexistent` and `VK_DRIVER_FILES=/nonexistent.json`.
   - Sun: no ray queries, so Low only (`SHAR_SUN=1` runs Low): bridge girder shadows seen in `demo l1`.
   - User confirmed on Vulkan: Low shadows, gamma. Not reported: stutter on first use, Cmd+Q, a full play session.
   - Not checked: validation layer (`vulkan-validationlayers` not installed).
9. Hardening: ASan, TSan for memory-order bugs, long play sessions.
10. Ship: `.app` packaging and signing; update README and TODO.

Rules and remaining blockers. Toolchain rules go in `gen_cmake.py`.
- Apple GL: `glBufferSubData` on a buffer in use submits and waits for the GPU, even in an unused region (60% of the main
  thread). Use unsynchronised `glMapBufferRange` (`glpipeline.cpp EndStream`, `glcon.cpp Upload`). Find stalls with `sample`.
- Optimised by default on macOS (`RelWithDebInfo`, `gen_cmake.py`).
- Target: Apple clang + libc++, Mach-O, SDL2, OpenAL and FFmpeg from Homebrew; `GL` -> `-framework OpenGL`. `SHAR_ARCH`
  (-m64/-m32) is empty: the architecture comes from `CMAKE_OSX_ARCHITECTURES` (default arm64). Clang is stricter than GCC: expect source fixes. Homebrew's `sdl2` is sdl2-compat (SDL2
  API over SDL3): behaviour may differ from Linux's SDL2. `openal-soft` is keg-only: add its prefix to the search paths.
- Linker: no `--version-script`, `--exclude-libs`, `--start-group`, `-static-libgcc`, `-static-libstdc++`.
- Allocator isolation on Mach-O: dyld coalesces weak `operator new`, so an exported definition in the executable takes
  over every dylib's. `macos/srr2.unexported` hides them (a list: repeated `-unexported_symbol` flags kept only the last).
  `macos/pddi_new.h` renames pddigl's references with assembler labels instead of `objcopy`. Checks: `nm -gU srr2` has no
  `__Znwm __Znam __ZdlPv __ZdaPv`; `nm -u libpddigl.a` has only `_pddi_heap_*`; the executable imports no libc++ function
  that returns memory the game would free (`nm -u srr2 | c++filt`), so `std::string` etc. must stay out of game code.
- radscript `InvokeVf64` on Apple arm64: `this` + 7 ints in x0-x7, 8 floats in v0-v7. Stack args are packed to natural
  size, so one signature can't place them: `TypeInfoMethod.cpp` aborts on a method needing them. `srrtypes.typ` (454
  methods, at most 6 parameters) has none.
- GL debug output (`SHAR_GL_DEBUG`) is compiled out: macOS OpenGL stops at 4.1.
- Vulkan via MoltenVK: 1.4.2 on an M2 reports Vulkan 1.4, so `vkcore.cpp`'s 1.3 check passes. `__APPLE__` code enables
  `VK_KHR_portability_enumeration` + `VK_KHR_portability_subset` (features as offered). `pointPolygons` is false: point fill draws solid
  (`pvkCore::pointPolygons`). No `samplerMipLodBias`: unused. No ray queries: Low shadows only. Depth is D32S8 (no D24S8).
- GL fallback: 3.3 core is available but deprecated on macOS; surfaceless worker contexts are Wayland-only (platform.md).
- Hardware differences that can expose bugs: weak memory order (x86 forgave unsynchronised flag handoffs between
  threads); out-of-range float -> int saturates (x86: INT_MIN). `char` stays signed on Apple arm64.
- Tools to replace: valgrind `memcheck.sh` (use ASan), `package.sh`/`--noble` (needs an .app, signing).
- Filesystem is case-insensitive by default: no two tracked paths may differ only in case.
- Keep the checkout out of iCloud-synced folders (`~/Documents`, `~/Desktop`): sync conflicts leave `name 2.a` copies in build output and could do the same in `.git`. Use `~/Developer`.
- Intel x86_64 Mac (Big Sur with MacPorts tested; Homebrew at `/usr/local` not): same build, `-DCMAKE_OSX_ARCHITECTURES=x86_64`.
  `play.sh` passes it on first configure, with SHAR_BREW `/opt/local` if MacPorts is installed, else `/usr/local`; an existing
  build dir is left alone. `shar_crash.cpp` has an `__APPLE__ && __x86_64__` branch (Darwin ucontext: `uc_mcontext->__ss.__rip`,
  not Linux's `gregs[REG_RIP]`). MoltenVK on an Iris Plus 640 exposes only Vulkan 1.2, below the 1.3 `vkcore.cpp` requires, so
  that GPU runs OpenGL 4.1.
- MacPorts works as an alternative to Homebrew: `-DSHAR_BREW=/opt/local`. If the Vulkan SDK is self-installed from
  LunarG, headers land in `/usr/local/include` (System Global Install), not the SHAR_BREW prefix: add
  `-DCMAKE_CXX_FLAGS="-isystem /usr/local/include"`. With MacPorts, don't add `-L/usr/local/lib` unless you know nothing
  older is there: an old FFmpeg 4.x in `/usr/local/lib` silently beat the current one in `/opt/local/lib` and the link
  failed on `swr_alloc_set_opts2`/`av_channel_layout_default` (FFmpeg 5.1+). `-DSHAR_BREW=/usr/local` searches that
  directory by design. The Vulkan loader is loaded at run time (`SDL_Vulkan_LoadLibrary`), so it needs no `-L` anyway.

## Shipped-binary strings
- `-fmacro-prefix-map` makes `__FILE__` relative. Release (`!RAD_DEBUG`) `rReleaseAssert`/`Warning` pass
  `RAD_RELEASE_FILE` (""); release `rTunePrintf` is a macro dropping the format (GCC never inlines variadics, so an
  empty inline keeps the string). Check: `strip -o x srr2; strings x | grep -i -E 'radical|\.cpp'` -> only RTTI names.

## Release packages (`package.sh [--32] --noble`, `dist.sh`)
- `--noble`: build + package in podman Ubuntu 24.04 (`port/noble.Containerfile`, `build-linux[32]-noble/`): needs glibc
  <= 2.39 (Mint 22). A host (26.04) build needs 2.43 (`asinf`/`acosf`/`atan2f`/`sqrtf` versions + every bundled lib).
- GCC 13 there: rejects what GCC 15 lets through (missing `typename`, undeclared names in uninstantiated templates).
- Packaged `run.sh` puts only bundled libs the system lacks (same soname per `ldconfig -p`, per arch) on the path
  (`run/.lib`): older bundled glib/librsvg over newer system ones aborted libdecor's GTK plugin on Wayland.
- Don't bundle glibc parts (`libmvec`).
- `dist.sh`: tarball for GitHub Releases on a separate binaries-only public repo (dist/release/); never commit
  binaries to a branch of this repo (a release tag here would expose the source).
