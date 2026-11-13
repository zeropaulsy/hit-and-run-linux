# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Syntax-check one or more sources with their CMake target's exact flags: python3 port/check.py FILE... [-n LINES]"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(__file__))
import vcx, gen_cmake as g
EXTRA = [a for a in sys.argv[1:] if a.startswith("-W") or a.startswith("-f")]
args = [a for a in sys.argv[1:] if not a.startswith("-")]
lines = int(next((a[2:] for a in sys.argv[1:] if a.startswith("-n")), "40"))
owner = {}
for p in [g.REPLACE.get(p["name"], p) for p in vcx.projects()]:
    for s in p["sources"] + g.LINUX_EXTRA.get(g.target_name(p), []):
        owner[s] = p
        owner[s.replace("/win32/", "/linux/")] = p
        d, f = os.path.split(s.replace("/win32/", "/linux/")); owner[os.path.join(d, f.replace("win32", "linux"))] = p
for f in args:
    f = os.path.relpath(os.path.abspath(f), vcx.ROOT)
    p = owner.get(f) or owner[next(k for k in owner if os.path.dirname(k) == os.path.dirname(f))]
    incs, defs = g.target_flags(p)
    lang = ["-x", "c", "-std=gnu99"] if f.endswith(".c") else ["-std=gnu++98", "-fpermissive"]
    cmd = (["g++", "-m64", "-fsyntax-only", "-w", "-fmax-errors=10"] + lang + EXTRA + ["-include", g.COMPAT_HEADER]
           + [x for h in g.forced_includes(p) for x in ("-include", h)] + [f"-I{i}" for i in incs]
           + [f"-idirafter{g.COMPAT_DIR}"] + [f"-D{d}" for d in g.PLATFORM_DEFINES + defs] + [f])
    r = subprocess.run(cmd, cwd=vcx.ROOT, capture_output=True, text=True, errors="replace")
    print(f"== {f} [{g.target_name(p)}]: {'OK' if r.returncode == 0 else 'FAIL'}")
    print("\n".join(r.stderr.splitlines()[:lines]))
