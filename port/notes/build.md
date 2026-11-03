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
- Deps: amd64 -dev SDL2, GL, OpenAL, FFmpeg. Legacy 32-bit: gcc-multilib + the `:i386` ones (Bink:
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

## macOS arm64 (planned, not started; nothing below is built or tested yet)
Blockers found by reading the code. Toolchain rules go in `gen_cmake.py`.
- Target: Apple clang + libc++, Mach-O, SDL2 and FFmpeg from Homebrew. `SHAR_ARCH` (-m64/-m32) -> `-arch arm64`. Clang
  is stricter than GCC: expect source fixes.
- Linker: no `--version-script`, `--exclude-libs`, `--start-group`, `-static-libgcc`, `-static-libstdc++`. Exports:
  `-exported_symbols_list`.
- Allocator isolation (above) is ELF-specific: no `objcopy` on pddigl, Mach-O prefixes `_` (`__Znwm`), and weak binding
  can route libc++ and dylibs to the game's operator new. Redesign, then check with `nm -gU`, not `nm -D`.
- radscript `InvokeVf64` (`linuxtypeinfovfcall.cpp`, sorted in `TypeInfoMethod.cpp`) is SysV: rewrite. Apple arm64:
  `this` + 7 ints in x0-x7, 8 floats in v0-v7, stack args packed to natural size, not 8-byte slots, so the
  one-signature call can't cover stack args as it stands. Vtable slots (Itanium, as GCC): verify.
- x86 instructions: `int3` in `raddebug/debug.cpp`, `scrooby/strings/pcstring.cpp` -> `__builtin_debugtrap()`.
  `shar_crash.cpp` PC: `uc_mcontext->__ss.__pc`. No SSE intrinsic headers in the tree.
- Linux APIs: `/proc/self/exe` (`win32main.cpp`) -> `_NSGetExecutablePath`. Fibers (`win32_threads.cpp`) use ucontext:
  deprecated on macOS, test on arm64 or replace.
- Vulkan via MoltenVK: `vkcore.cpp` requires 1.3 (check the installed one reports it) and needs
  `VK_KHR_portability_enumeration` + `VK_KHR_portability_subset`, not requested yet. No ray query -> `rayQuery = false`
  path: verify sun shadows there.
- GL fallback: 3.3 core is available but deprecated on macOS; surfaceless worker contexts are Wayland-only (platform.md).
- Hardware differences that can expose bugs: weak memory order (x86 forgave unsynchronised flag handoffs between
  threads); out-of-range float -> int saturates (x86: INT_MIN). `char` stays signed on Apple arm64.
- Tools to replace: valgrind `memcheck.sh` (use ASan), `play.sh`, `package.sh`/`--noble` (needs an .app, signing).
- Filesystem is case-insensitive by default: no two tracked paths may differ only in case.

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
