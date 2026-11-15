#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

# Copy every non-system dylib the given Mach-O files need (recursively) into FRAMEWORKS and point all references
# at @rpath/<name>: the executable gets the rpath @executable_path/../Frameworks, each dylib @loader_path.
# Prints the highest minimum macOS version (LC_BUILD_VERSION minos) of everything bundled.
# usage: macos_bundle_libs.py FRAMEWORKS EXECUTABLE [EXTRA_DYLIB[=NAME]...]
#   EXTRA_DYLIB: a library loaded at run time (dlopen), bundled as NAME (default: its own file name)
import os
import re
import shutil
import subprocess
import sys

SYSTEM = ("/usr/lib/", "/System/")


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


def deps(path):
    lines = run("otool", "-L", path).splitlines()[1:]
    out = [l.strip().split(" (compatibility")[0] for l in lines]
    own = run("otool", "-D", path).splitlines()[1:]   # a dylib lists its own id first
    return [d for d in out if not own or d != own[0].strip()]


def rpaths(path):
    return re.findall(r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.*) \(offset", run("otool", "-l", path))


def minos(path):
    m = re.search(r"cmd LC_BUILD_VERSION\n\s+cmdsize \d+\n\s+platform \d+\n\s+minos ([\d.]+)", run("otool", "-l", path))
    return m.group(1) if m else "0"


def resolve(ref, origin, exe_dir):
    """Absolute path of reference ref made by the file at origin (its original location)."""
    loader = os.path.dirname(origin)
    if ref.startswith("@loader_path/"):
        return os.path.realpath(os.path.join(loader, ref[len("@loader_path/"):]))
    if ref.startswith("@executable_path/"):
        return os.path.realpath(os.path.join(exe_dir, ref[len("@executable_path/"):]))
    if ref.startswith("@rpath/"):
        for rp in rpaths(origin) + [exe_dir]:
            rp = rp.replace("@loader_path", loader).replace("@executable_path", exe_dir)
            cand = os.path.join(rp, ref[len("@rpath/"):])
            if os.path.exists(cand):
                return os.path.realpath(cand)
        sys.exit(f"cannot resolve {ref} (needed by {origin})")
    return os.path.realpath(ref)


def main():
    fw, exe = sys.argv[1], sys.argv[2]
    exe_dir = os.path.dirname(os.path.realpath(exe))
    os.makedirs(fw, exist_ok=True)
    bundled = {}            # name in Frameworks -> original path
    queue = []              # (file in bundle, its original path)
    for extra in sys.argv[3:]:
        src, _, name = extra.partition("=")
        name = name or os.path.basename(src)
        bundled[name] = os.path.realpath(src)
        queue.append((os.path.join(fw, name), os.path.realpath(src)))
    queue.insert(0, (exe, exe))   # the executable is already in place (copied by the caller)
    for name, src in bundled.items():
        shutil.copyfile(src, os.path.join(fw, name))
    vers = []
    while queue:
        dst, origin = queue.pop(0)
        os.chmod(dst, 0o755)
        vers.append(minos(dst))
        args = []
        for ref in deps(dst):
            if ref.startswith(SYSTEM):
                continue
            name = os.path.basename(ref)
            src = resolve(ref, origin, exe_dir)
            if name not in bundled:
                bundled[name] = src
                shutil.copyfile(src, os.path.join(fw, name))
                queue.append((os.path.join(fw, name), src))
            args.append(("-change", ref, "@rpath/" + name))
        for rp in rpaths(dst):
            args.append(("-delete_rpath", rp))
        if dst == exe:
            args.append(("-add_rpath", "@executable_path/../Frameworks"))
        else:
            args += [("-id", "@rpath/" + os.path.basename(dst)), ("-add_rpath", "@loader_path")]
        # a repeated rpath or reference makes install_name_tool fail: drop repeats
        flat = [a for op in dict.fromkeys(args) for a in op]
        subprocess.run(["install_name_tool", *flat, dst], check=True, capture_output=True)
    # anything still pointing outside the bundle is a bug
    for f in [exe] + [os.path.join(fw, n) for n in bundled]:
        for ref in deps(f):
            if not ref.startswith(SYSTEM + ("@rpath/",)):
                sys.exit(f"{f}: still references {ref}")
    print(max(vers, key=lambda v: tuple(int(x) for x in v.split("."))))


main()
