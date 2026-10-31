"""Compile Vulkan GLSL (#version 450) post passes to SPIR-V for the Vulkan renderer, with the game's own glslang
(libshar_glslang.so, so no glslangValidator/glslc needed): python3 port/spirv.py FILE.frag... [-o OUT.spv]
Writes FILE.spv beside each source. The interfaces: shaders/README.md (Vulkan passes, Vulkan material shaders).
#include "file" (relative to the source) is expanded here; errors in it show as source string 1 ("1:LINE")."""
import ctypes, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

def library():
    for build in (os.environ.get("SHAR_BUILD"), "build-linux", "build-linux-debug", "build-linux32"):
        if build:
            path = os.path.join(ROOT, build, "libshar_glslang.so")
            if os.path.exists(path):
                try:
                    lib = ctypes.CDLL(path)
                    lib.shar_spirv_compile
                    return lib
                except (OSError, AttributeError):
                    pass
    sys.exit("spirv.py: no libshar_glslang.so with shar_spirv_compile (build first: ninja -C build-linux shar_glslang)")

def expand(path):
    out = []
    for n, line in enumerate(open(path, encoding="utf-8").read().splitlines(), 1):
        m = re.match(r'\s*#\s*include\s+"([^"]+)"', line)
        if m:
            out += ["#line 1 1", open(os.path.join(os.path.dirname(path), m.group(1)), encoding="utf-8").read(),
                    f"#line {n + 1} 0"]
        else:
            out.append(line)
    return ("\n".join(out) + "\n").encode()

def compile_file(lib, source, out):
    words, count, log = ctypes.POINTER(ctypes.c_uint)(), ctypes.c_size_t(), ctypes.c_char_p()
    text = expand(source)
    stage = 0 if source.endswith(".vert") else 1
    lib.shar_spirv_compile.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_void_p,
                                       ctypes.c_void_p, ctypes.c_void_p]
    lib.shar_spirv_free.argtypes = [ctypes.c_void_p]
    ok = lib.shar_spirv_compile(source.encode(), stage, text, ctypes.byref(words), ctypes.byref(count),
                                ctypes.byref(log))
    if log.value:
        sys.stderr.write(log.value.decode(errors="replace"))
        lib.shar_spirv_free(ctypes.cast(log, ctypes.c_void_p))
    if not ok:
        return False
    with open(out, "wb") as f:
        f.write(ctypes.string_at(words, count.value * 4))
    lib.shar_spirv_free(words)
    print(f"{out}: {count.value * 4} bytes")
    return True

def main():
    args = sys.argv[1:]
    out = None
    if "-o" in args:
        i = args.index("-o")
        out = args[i + 1]
        del args[i:i + 2]
    if not args or (out and len(args) > 1):
        sys.exit(__doc__)
    lib = library()
    ok = all([compile_file(lib, f, out or os.path.splitext(f)[0] + ".spv") for f in args])
    sys.exit(0 if ok else 1)

if __name__ == "__main__":
    main()
