"""Generate CMakeLists.txt for the Linux build (x86-64; legacy 32-bit x86) from the original VS2013 Win32 projects.
Re-run after changing the mapping rules below; the generated file is not hand-edited."""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(__file__))
import vcx  # noqa: E402

# Windows-only defines dropped; Linux platform defines added (as Pure3D's own Linux Makefiles did)
DROP_DEFINES = {"WIN32", "_WINDOWS", "_USRDLL", "_LIB", "WIN32_EXTRA_LEAN", "WIN32_LEAN_AND_MEAN",
                "DEBUGWATCH"}   # the watch window talks to PC dev tools over the (stubbed) debug link
# Linux is a PC platform: RAD_WIN32 stays defined so every "PC version" code path is taken unchanged
# (162 game files use it, only 9 call Windows APIs); RAD_LINUX marks where the OS API differs.
# LINUX is what Pure3D's old Linux platform headers check for.
PLATFORM_DEFINES = ["RAD_WIN32", "RAD_LINUX", "LINUX", "RAD_PC", "PDDI_NO_ASM"]

# The DX8 renderer is replaced by Pure3D's existing OpenGL pddi (sources from build/linux/pddi/Makefile), ported to
# an OpenGL 3.3 core context (glpipeline.cpp; user post-processing shaders: glpost.cpp; user material shaders:
# glcustom.cpp)
PDDI_GL = {
    "name": "pddigl", "kind": "StaticLibrary",
    "sources": ["libs/pure3d/pddi/base/basecontext.cpp", "libs/pure3d/pddi/base/baseshader.cpp",
                "libs/pure3d/pddi/base/font.cpp", "libs/pure3d/pddi/base/pddiobj.cpp",
                "libs/pure3d/pddi/gl/glcon.cpp", "libs/pure3d/pddi/gl/gldev.cpp", "libs/pure3d/pddi/gl/glmat.cpp",
                "libs/pure3d/pddi/gl/gltex.cpp", "libs/pure3d/pddi/gl/glpipeline.cpp", "libs/pure3d/pddi/gl/glpost.cpp",
                "libs/pure3d/pddi/gl/glcustom.cpp", "libs/pure3d/pddi/gl/glshaders.cpp",
                "libs/pure3d/pddi/gl/display_linux/gldisplay.cpp"],
    # libpng/zlib: images for post-processing passes (glpost.cpp)
    "includes": ["libs", "libs/radmath", "libs/pure3d", "libs/pure3d/lib/libpng", "libs/pure3d/lib/zlib"],
    "defines": ["RAD_RELEASE", "NDEBUG"],
}
PDDI_GL_DEBUG_DEFINES = ["RAD_DEBUG", "_DEBUG"]
REPLACE = {"pddidx8": PDDI_GL}

# Linux-only source changes per target: developer tooling that is not ported is left out (and stubbed)
LINUX_EXCLUDE = (
    "libs/radcore/src/raddebugcommunication/",     # Winsock/WSAAsyncSelect link to PC dev tools -> stub
    "libs/radcore/src/rad1394/",                   # FireWire debug link
    "libs/radcore/src/raddebugfileserver/",        # host-side file server for the remote drive
    "libs/radcore/src/radcontroller/directinputcontroller.cpp",   # replaced by linux/sdlcontroller.cpp
    "libs/radmovie/src/common/binkradfile.cpp",                   # Bink SDK file IO; linux/binkffmpeg.cpp opens files
)
# Linux-only include directories per target (searched before the original ones)
LINUX_INCLUDES = {
    "radmovie": ["libs/radmovie/src/linux"],                     # bink.h: FFmpeg-backed stand-in for the SDK header
}
LINUX_EXTRA = {
    "radcore": ["libs/radcore/src/raddebugcommunication/linux/dbgcomstub.cpp",
                "libs/radcore/src/radcontroller/linux/sdlcontroller.cpp"],
    "srr2": ["code/main/linuxplatform_sdl.cpp",
             "code/mission/modscript.cpp",                          # mods' script commands (AdditionalScriptFunctionality)
             "code/mission/objectives/modobjectives.cpp",
             "code/mission/conditions/modconditions.cpp",
             "code/mission/charactersheet/modstatstotals.cpp",      # CustomStatsTotals
             "code/mission/rewards/modrewardquests.cpp",            # CustomRewardQuestSupport
             "code/meta/modtriggeractions.cpp",                     # CustomTriggerActions
             "code/presentation/gui/frontend/guiscreengraphics.cpp"],   # Options > Graphics
    "radmovie": ["libs/radmovie/src/linux/binkffmpeg.cpp",      # the Bink SDK subset radmovie uses, on FFmpeg
                 "libs/radmovie/src/linux/binkvideotexture.cpp"], # mods' VideoTextureSupport
    "radsound": ["libs/radsound/src/hal/linux/linuxhal.cpp",
                 "libs/radsound/src/hal/win32/bufferloader.cpp",          # platform-neutral, reused as is
                 "libs/radsound/src/hal/common/softwarelistener.cpp",     # software 3D (as PS2/GameCube)
                 "libs/radsound/src/hal/common/softwarepositionalgroup.cpp",
                 "libs/radsound/src/radsound/linux/oggdecoder.cpp"],      # mods' Ogg Vorbis files (FFmpeg)
}

WIN_SDK_RE = re.compile(r"sdks/(dx8|dx9|win32|eax\w*|mfc|XBox|GameCube)", re.I)

COMPAT_DIR = "port/compat"
LUA_DIR = "port/third_party/lua"
LUA_SOURCES = sorted(f"{LUA_DIR}/{f}" for f in os.listdir(os.path.join(vcx.ROOT, LUA_DIR)) if f.endswith(".c"))
GLSLANG_DIR = "port/third_party/glslang"
GLSLANG_SOURCES = sorted(os.path.relpath(os.path.join(d, f), vcx.ROOT)
                         for sub in ("glslang", "SPIRV")
                         for d, _, files in os.walk(os.path.join(vcx.ROOT, GLSLANG_DIR, sub))
                         for f in files if f.endswith(".cpp")) + [f"{GLSLANG_DIR}/shar_glslang.cpp"]
COMPAT_HEADER = "port/compat/msvc_compat.h"
PLATFORM_DIRS = ("/win32/", "/dx8/")      # sources here need a Linux implementation; excluded for now


def target_name(p):
    return {"SRR2": "srr2", "ScroobyLib": "scrooby", "Pure3D": "pure3d"}.get(p["name"], p["name"].lower())


def target_flags(p):
    """(include dirs, defines) for one project, shared by the CMake output and port/syntax_survey.py."""
    # the bundled Windows SDKs (DirectX 8, Bink, EAX, MFC) must never shadow the Linux stand-ins
    incs = ["libs"] + [i for i in p["includes"] if i != "libs" and not WIN_SDK_RE.search(i)]
    for s in p["sources"][:1]:
        parts = s.replace(os.sep, "/").split("/")
        if parts[0] == "libs":
            pch = f"libs/{parts[1]}/src/pch"
            if os.path.isdir(os.path.join(vcx.ROOT, pch)) and pch not in incs:
                incs.append(pch)
    defs = [d for d in p["defines"] if d not in DROP_DEFINES]
    return incs, defs


def forced_includes(p):
    """MSVC /FI files (e.g. pchsrr2.h for the game), resolved against the target's include dirs."""
    out = []
    for f in p.get("forced", []):
        src_dirs = sorted({os.path.dirname(s) for s in p["sources"]})
        for d in target_flags(p)[0] + src_dirs:      # e.g. pchsrr2.h lives next to pchsrr2.cpp in code/main
            real = vcx.resolve(os.path.join(d, f))
            if real:
                out.append(real)
                break
    return out


def main():
    projects = [REPLACE.get(p["name"], p) for p in vcx.projects()]
    debug_defs = {}
    for p in vcx.projects("Debug|Win32"):
        if p["name"] in REPLACE:
            q = dict(REPLACE[p["name"]]); q["defines"] = PDDI_GL_DEBUG_DEFINES
            debug_defs[q["name"]] = target_flags(q)[1]
        else:
            debug_defs[p["name"]] = target_flags(p)[1]
    excluded = {}
    out = [
        "# GENERATED by port/gen_cmake.py from build/win32/*.vcxproj -- do not edit by hand",
        "cmake_minimum_required(VERSION 3.16)",
        "project(shar_linux C CXX)",
        "option(SHAR_RAD_DEBUG \"Use the original Debug configuration (RAD_DEBUG: asserts, debug output)\" OFF)",
        "set(CMAKE_EXPORT_COMPILE_COMMANDS ON)",
        "# SHAR_UNINIT_AUDIT: warnings on and -O2 (GCC needs optimisation to track uninitialised values)",
        "option(SHAR_UNINIT_AUDIT \"Report uninitialised reads instead of silencing warnings\" OFF)",
        "if(SHAR_UNINIT_AUDIT)",
        "  set(SHAR_WARN \"-O2 -Wuninitialized -Wmaybe-uninitialized\")",
        "else()",
        "  set(SHAR_WARN \"-w\")",
        "endif()",
        "# GCC 15 enables libstdc++ bounds assertions in unoptimised builds; the MSVC release build had none.",
        "# Keep them in the debug build (SHAR_RAD_DEBUG) to find latent out-of-range reads.",
        "if(NOT SHAR_RAD_DEBUG)",
        "  set(SHAR_WARN \"${SHAR_WARN} -D_GLIBCXX_NO_ASSERTIONS\")",
        "endif()",
        "# SHAR_64BIT: x86-64 (the default); OFF = the legacy 32-bit x86 build (the game as shipped). Libraries are",
        "# linked by name, so -m64 / -m32 picks the amd64 / i386 packages (libsdl2-dev, libgl-dev, libopenal-dev, FFmpeg)",
        "option(SHAR_64BIT \"Build for x86-64 (OFF: legacy 32-bit x86)\" ON)",
        "if(SHAR_64BIT)",
        "  set(SHAR_ARCH -m64)",
        "else()",
        "  set(SHAR_ARCH -m32)",
        "  message(STATUS \"SHAR: legacy 32-bit build\")",
        "endif()",
        "set(CMAKE_C_FLAGS \"${CMAKE_C_FLAGS} ${SHAR_ARCH} -g -fno-strict-aliasing ${SHAR_WARN}\")",
        "# gnu++98 + permissive: closest to what 2003-era MSVC accepted",
        "set(CMAKE_CXX_FLAGS \"${CMAKE_CXX_FLAGS} ${SHAR_ARCH} -g -std=gnu++98 -fpermissive -fno-strict-aliasing ${SHAR_WARN}\")",
        "# __FILE__ (assert text) relative to the source root: no home directory in shipped binaries",
        "add_compile_options(\"-fmacro-prefix-map=${CMAKE_SOURCE_DIR}/=\")",
        "# static C++ runtime + no exported runtime symbols: the game's own operator new must not leak into",
        "# shared libraries (GPU drivers) the way it would by default on ELF (see SHAR_LOCAL in msvc_compat.h)",
        "set(CMAKE_EXE_LINKER_FLAGS \"${CMAKE_EXE_LINKER_FLAGS} ${SHAR_ARCH} -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL\")",
        "# the game's operator new/delete stay out of the dynamic symbol table (port/compat/srr2.map)",
        f"set(CMAKE_EXE_LINKER_FLAGS \"${{CMAKE_EXE_LINKER_FLAGS}} -Wl,--version-script=${{CMAKE_SOURCE_DIR}}/{COMPAT_DIR}/srr2.map\")",
        "# Lua for mods' scripts (port/third_party/lua, port/compat/shar_modlua.cpp): plain C, so declared before the",
        "# game-wide definitions and the force-included MSVC compat header below",
        f"add_library(lua STATIC {' '.join(LUA_SOURCES)})",
        "target_compile_options(lua PRIVATE -O2)",
        "target_compile_definitions(lua PRIVATE LUA_USE_POSIX)",
        "# glslang (GLSL -> SPIR-V for the Vulkan renderer): a shared library with its own C++ runtime and allocator, out",
        "# of reach of the game's operator new (port/third_party/glslang/shar_glslang.h)",
        f"add_library(shar_glslang SHARED {' '.join(GLSLANG_SOURCES)})",
        "target_compile_options(shar_glslang PRIVATE -std=c++17 -O2 -g0 -w -fvisibility=hidden)",
        f"target_include_directories(shar_glslang PRIVATE {GLSLANG_DIR} {GLSLANG_DIR}/generated)",
        "target_link_options(shar_glslang PRIVATE ${SHAR_ARCH} -static-libstdc++ -static-libgcc -Wl,--exclude-libs,ALL "
        f"-Wl,--version-script=${{CMAKE_SOURCE_DIR}}/{GLSLANG_DIR}/shar_glslang.map)",
        "target_link_libraries(shar_glslang PRIVATE pthread)",
        f"add_compile_definitions({' '.join(PLATFORM_DEFINES)})",
        "# MSVC built-ins/CRT names -> GCC/POSIX, and a types-only windows.h (searched after the source dirs)",
        f"add_compile_options(\"SHELL:-include ${{CMAKE_SOURCE_DIR}}/{COMPAT_HEADER}\")",
        f"include_directories(AFTER {COMPAT_DIR})",
        "",
    ]
    libs = []
    for p in projects:
        name = target_name(p)
        srcs = []
        for s in p["sources"]:
            if s.startswith(LINUX_EXCLUDE):
                excluded.setdefault(name, []).append(s)
                continue
            if any(d in "/" + s.replace(os.sep, "/").lower() for d in PLATFORM_DIRS):
                lx = s.replace("/win32/", "/linux/")                      # ported sibling, e.g. p3d/platform/linux
                d, f = os.path.split(lx)
                linux = vcx.resolve(lx) or vcx.resolve(os.path.join(d, f.replace("win32", "linux")))
                if linux and linux != s:
                    srcs.append(linux)
                else:
                    excluded.setdefault(name, []).append(s)
            else:
                srcs.append(s)
        srcs += LINUX_EXTRA.get(name, [])
        if not srcs:
            continue
        kind = "add_executable" if p["kind"] == "Application" else "add_library"
        extra = "" if kind == "add_executable" else " STATIC"
        out.append(f"{kind}({name}{extra}")
        out += [f"  {s}" for s in srcs]
        out.append(")")
        incs, defs = target_flags(p)
        incs = LINUX_INCLUDES.get(name, []) + incs
        dbg = debug_defs.get(p["name"], defs)
        out.append(f"target_include_directories({name} PRIVATE {' '.join(incs)})")
        if defs != dbg:   # original project configurations: Release (RAD_RELEASE) or Debug (RAD_DEBUG, asserts, logs)
            out.append(f"if(SHAR_RAD_DEBUG)")
            out.append(f"  target_compile_definitions({name} PRIVATE {' '.join(dbg)})")
            out.append(f"else()")
            out.append(f"  target_compile_definitions({name} PRIVATE {' '.join(defs)})")
            out.append(f"endif()")
        elif defs:
            out.append(f"target_compile_definitions({name} PRIVATE {' '.join(defs)})")
        for f in forced_includes(p):
            out.append(f"target_compile_options({name} PRIVATE \"SHELL:-include ${{CMAKE_SOURCE_DIR}}/{f}\")")
        out.append("")
        if kind == "add_library":
            libs.append(name)
    out.append("# Win32 API subset implemented on POSIX (threads/sync/timers); linked after the libraries using it")
    out.append(f"add_library(shar_compat STATIC {COMPAT_DIR}/win32_threads.cpp {COMPAT_DIR}/win32_misc.cpp "
               f"{COMPAT_DIR}/pddi_heap.cpp {COMPAT_DIR}/shar_mods.cpp {COMPAT_DIR}/shar_modlua.cpp "
               f"{COMPAT_DIR}/shar_modshops.cpp {COMPAT_DIR}/shar_modcars.cpp {COMPAT_DIR}/shar_modinteriors.cpp {COMPAT_DIR}/shar_modroads.cpp {COMPAT_DIR}/shar_modaudio.cpp {COMPAT_DIR}/shar_modbonus.cpp {COMPAT_DIR}/shar_modshaders.cpp {COMPAT_DIR}/shar_modcharacters.cpp {COMPAT_DIR}/shar_modtraffic.cpp {COMPAT_DIR}/shar_modcards.cpp {COMPAT_DIR}/shar_moddutch.cpp "
               f"{COMPAT_DIR}/shar_crash.cpp)")
    out.append("# the renderer allocates from its own heap, as pddidx8.dll did on Windows (port/compat/pddi_heap.cpp)")
    # operator new's mangled name carries its size type: unsigned int (j) on 32-bit, unsigned long (m) on 64-bit
    out.append("if(SHAR_64BIT)\n  set(SHAR_NEW_SYM m)\nelse()\n  set(SHAR_NEW_SYM j)\nendif()")
    out.append("add_custom_command(TARGET pddigl POST_BUILD COMMAND objcopy "
               "--redefine-sym _Znw${SHAR_NEW_SYM}=pddi_heap_new --redefine-sym _Zna${SHAR_NEW_SYM}=pddi_heap_new_array "
               "--redefine-sym _ZdlPv=pddi_heap_delete --redefine-sym _ZdaPv=pddi_heap_delete_array "
               "$<TARGET_FILE:pddigl> VERBATIM)")
    out.append(f"target_include_directories(shar_compat PRIVATE {COMPAT_DIR} {LUA_DIR})")
    # static libraries reference each other in both directions (pure3d <-> pddigl/libpng, radmusic -> radsound,
    # ...); MSVC resolves that automatically, GNU ld needs them in a group
    out.append(f"target_link_libraries(srr2 PRIVATE -Wl,--start-group {' '.join(l for l in libs)} shar_compat lua "
               f"-Wl,--end-group shar_glslang SDL2 GL openal avformat avcodec swscale swresample avutil pthread dl)")
    open(os.path.join(vcx.ROOT, "CMakeLists.txt"), "w").write("\n".join(out) + "\n")

    with open(os.path.join(vcx.ROOT, "port/excluded_platform_sources.txt"), "w") as f:
        for name, srcs in excluded.items():
            f.write(f"[{name}]\n" + "".join(f"  {s}\n" for s in srcs))
    total = sum(len(p["sources"]) for p in projects)
    print(f"CMakeLists.txt: {len(projects)} targets, {total} sources, "
          f"{sum(map(len, excluded.values()))} Win32/DX8 sources excluded (port/excluded_platform_sources.txt)")
    for name, srcs in excluded.items():
        print(f"  {name}: {len(srcs)} excluded")


if __name__ == "__main__":
    main()
