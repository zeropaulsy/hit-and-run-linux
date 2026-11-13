# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""64-bit survey: g++ -fsyntax-only on every source with its exact flags from build-linux/compile_commands.json
(configure it first: cmake -B build-linux -G Ninja), with the pointer-size diagnostics on.
Lists each site once (headers are reported by every source that includes them), grouped by kind and library.
Also counts plain `long` (32-bit under MSVC, 64-bit on Linux x86-64), which no diagnostic catches.
usage: python3 port/survey64.py [--sites] -> summary; port/survey64.json has every site"""
import collections, json, os, re, shlex, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DB = os.path.join(ROOT, "build-linux", "compile_commands.json")

# kind -> pattern on a diagnostic line
KINDS = [
    ("ptr->int", re.compile(r"cast from .* to .* loses precision")),
    ("int->ptr", re.compile(r"cast to pointer from integer of different size")),
    ("narrowing", re.compile(r"narrowing conversion")),
    ("asm", re.compile(r"\basm\b|inline assembl|impossible constraint|invalid operand")),
    ("static size check", re.compile(r"static assert|size of array .* is negative|static_assert")),
]
DIAG = re.compile(r"^(?P<file>[^:\s][^:]*):(?P<line>\d+):(?P<col>\d+): (?P<sev>warning|error|fatal error): (?P<msg>.*)$")


def command(entry):
    args = shlex.split(entry["command"]) if "command" in entry else list(entry["arguments"])
    out, skip = [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if a in ("-c", "-w", "-MD", "-MMD") or a.startswith("-W"):
            continue
        out.append(a)
    return out + ["-fsyntax-only", "-Wint-to-pointer-cast", "-Wpointer-to-int-cast", "-fmax-errors=50"]


def library(path):
    rel = os.path.relpath(path, ROOT)
    parts = rel.split(os.sep)
    if parts[0] == "libs" and len(parts) > 2:
        return "libs/" + parts[1] + ("/" + parts[2] if parts[1] == "pure3d" else "")
    return "/".join(parts[:2]) if len(parts) > 1 else rel


def run(entry):
    r = subprocess.run(command(entry), cwd=entry["directory"], capture_output=True, text=True, errors="replace")
    found, errors = [], []
    for line in r.stderr.splitlines():
        m = DIAG.match(line)
        if not m:
            continue
        path = os.path.normpath(os.path.join(entry["directory"], m["file"]))
        kind = next((k for k, p in KINDS if p.search(m["msg"])), None)
        if kind:
            found.append((kind, path, int(m["line"]), m["msg"]))
        elif m["sev"] != "warning":
            errors.append((path, int(m["line"]), m["msg"]))
    return entry["file"], r.returncode == 0, found, errors


def count_long():
    """Plain long / unsigned long (not long long, not double) in the sources built on Linux, per library."""
    pat = re.compile(r"(?<!long )\b(unsigned\s+|signed\s+)?long\b(?!\s+(long|double)\b)")
    counts = collections.Counter()
    for top in ("code", "libs"):
        for d, dirs, files in os.walk(os.path.join(ROOT, top)):
            dirs[:] = [x for x in dirs if x.lower() not in ("win32", "dx8", "ps2")]   # not built on Linux
            for f in files:
                if f.endswith((".cpp", ".c", ".h", ".hpp", ".inl")):
                    p = os.path.join(d, f)
                    with open(p, errors="replace") as fh:
                        text = re.sub(r"//[^\n]*", "", re.sub(r"/\*.*?\*/", "", fh.read(), flags=re.S))
                    n = len(pat.findall(text))
                    if n:
                        counts[library(p)] += n
    return counts


def main():
    if not os.path.exists(DB):
        sys.exit(f"{DB} missing: cmake -B build-linux -G Ninja")
    entries = json.load(open(DB))
    with ThreadPoolExecutor(os.cpu_count() or 4) as ex:
        results = list(ex.map(run, entries))

    sites = {}            # (kind, path, line) -> message
    errors = {}           # (path, line) -> message (first per site)
    failed = 0
    for _, ok, found, errs in results:
        failed += not ok
        for kind, path, line, msg in found:
            sites.setdefault((kind, path, line), msg)
        for path, line, msg in errs:
            errors.setdefault((path, line), msg)

    print(f"{len(results)} sources, {failed} failed to compile -m64")
    by_kind = collections.Counter(k for k, _, _ in sites)
    print("\nsites (each counted once):")
    for kind, n in by_kind.most_common():
        print(f"  {kind:18} {n:5}")
    print("\nper library:")
    by_lib = collections.defaultdict(collections.Counter)
    for kind, path, _ in sites:
        by_lib[library(path)][kind] += 1
    for lib, c in sorted(by_lib.items(), key=lambda x: -sum(x[1].values())):
        print(f"  {lib:32} {sum(c.values()):4}   " + ", ".join(f"{k} {n}" for k, n in c.most_common()))

    print(f"\nother errors ({len(errors)} sites), most common:")
    common = collections.Counter(re.sub(r"'[^']*'|‘[^’]*’", "'_'", m)[:100] for m in errors.values())
    for msg, n in common.most_common(12):
        print(f"  {n:5}  {msg}")

    longs = count_long()
    print(f"\nplain `long` in sources (MSVC 32-bit, Linux 64-bit): {sum(longs.values())}")
    for lib, n in longs.most_common(12):
        print(f"  {lib:32} {n:5}")

    if "--sites" in sys.argv:
        print("\nall sites:")
        for (kind, path, line), msg in sorted(sites.items(), key=lambda x: (x[0][1], x[0][2])):
            print(f"  {os.path.relpath(path, ROOT)}:{line}  [{kind}] {msg}")

    json.dump({"sites": [dict(kind=k, file=os.path.relpath(p, ROOT), line=l, msg=m) for (k, p, l), m in sorted(sites.items())],
               "errors": [dict(file=os.path.relpath(p, ROOT), line=l, msg=m) for (p, l), m in sorted(errors.items())],
               "long": dict(longs)},
              open(os.path.join(ROOT, "port", "survey64.json"), "w"), indent=1)


if __name__ == "__main__":
    main()
