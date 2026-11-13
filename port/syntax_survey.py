# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Pre-toolchain survey: g++ -fsyntax-only on every source with the CMake target's flags; groups first errors."""
import os, re, subprocess, sys, collections, json
from concurrent.futures import ThreadPoolExecutor
sys.path.insert(0, os.path.dirname(__file__))
import vcx, gen_cmake as g
ROOT = vcx.ROOT
jobs = []
for p in [g.REPLACE.get(p["name"], p) for p in vcx.projects()]:
    incs, defs = g.target_flags(p)
    defs = g.PLATFORM_DEFINES + defs
    for s in [x for x in p["sources"] if not x.startswith(g.LINUX_EXCLUDE)] + g.LINUX_EXTRA.get(g.target_name(p), []):
        if any(d in "/" + s.lower() for d in g.PLATFORM_DIRS):
            lx = s.replace("/win32/", "/linux/"); d, f = os.path.split(lx)
            s = vcx.resolve(lx) or vcx.resolve(os.path.join(d, f.replace("win32", "linux"))) or ""
            if not s or "/win32/" in s: continue
        lang = ["-x", "c", "-std=gnu99"] if s.endswith(".c") else ["-std=gnu++98", "-fpermissive"]
        jobs.append((g.target_name(p), s, ["g++", "-m64", "-fsyntax-only", "-w", "-fmax-errors=5"] + lang
                     + ["-include", g.COMPAT_HEADER] + [x for f in g.forced_includes(p) for x in ("-include", f)] + [f"-I{i}" for i in incs] + [f"-idirafter{g.COMPAT_DIR}"]
                     + [f"-D{d}" for d in defs] + [s]))
def run(j):
    t, s, cmd = j
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
    errs = [l for l in r.stderr.splitlines() if ": error:" in l or "fatal error:" in l]
    return t, s, r.returncode == 0, errs
with ThreadPoolExecutor(8) as ex:
    res = list(ex.map(run, jobs))
per = collections.Counter(); ok = collections.Counter(); cat = collections.Counter(); ex1 = {}
for t, s, good, errs in res:
    per[t] += 1; ok[t] += good
    if errs:
        msg = re.sub(r"^.*?(fatal )?error: ", "", errs[0])
        msg = re.sub(r"'[^']*'", "'_'", msg)[:90]
        cat[msg] += 1; ex1.setdefault(msg, errs[0][:200])
print(f"{sum(ok.values())}/{len(res)} sources pass syntax check")
for t in per: print(f"  {t:10} {ok[t]:4}/{per[t]}")
print("\nfirst-error categories:")
for m, n in cat.most_common(25): print(f"{n:5}  {m}\n        e.g. {ex1[m]}")
json.dump([dict(t=t, s=s, ok=good, errs=errs) for t, s, good, errs in res], open(os.path.join(ROOT, "port/syntax_survey.json"), "w"), indent=1)
