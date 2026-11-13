# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Read the VS2013 .vcxproj files of the original Win32 build (paths are case-insensitive on Windows)."""
import os
import re
import xml.etree.ElementTree as ET

ROOT = os.path.normpath(os.path.join(os.path.dirname(__file__), ".."))
M = "{http://schemas.microsoft.com/developer/msbuild/2003}"
SLN = os.path.join(ROOT, "build/win32/SRR2.sln")
CONFIG = "Release|Win32"

_index = None


def ci_index():
    """lowercase relative path -> real relative path, for every file and directory in the workspace."""
    global _index
    if _index is None:
        _index = {}
        for dp, dn, fn in os.walk(ROOT):
            if ".git" in dp.split(os.sep):
                continue
            for n in dn + fn:
                rel = os.path.relpath(os.path.join(dp, n), ROOT)
                _index[rel.lower()] = rel
    return _index


def resolve(path):
    """Workspace-relative path with the real on-disk case, or None."""
    rel = os.path.relpath(os.path.normpath(os.path.join(ROOT, path)), ROOT)
    if os.path.exists(os.path.join(ROOT, rel)):
        return rel
    return ci_index().get(rel.lower())


def _cond_ok(el):
    c = el.get("Condition")
    return c is None or CONFIG in c


def projects(config=None):
    """All projects; `config` ("Release|Win32" default, or "Debug|Win32") selects defines/includes."""
    global CONFIG
    saved = CONFIG
    if config:
        CONFIG = config
    try:
        sln = open(SLN, encoding="utf-8-sig", errors="replace").read()
        out = []
        for name, path in re.findall(r'Project\("[^"]+"\) = "([^"]+)", "([^"]+\.vcxproj)"', sln):
            p = resolve(os.path.join("build/win32", path.replace("\\", "/")))
            out.append(read_project(name, p))
        return out
    finally:
        CONFIG = saved


def read_project(name, vcx):
    r = ET.parse(os.path.join(ROOT, vcx)).getroot()
    d = os.path.dirname(vcx)
    kind = next((e.text for e in r.iter(M + "ConfigurationType") if True), "StaticLibrary")
    srcs, missing = [], []
    for e in r.iter(M + "ClCompile"):
        inc = e.get("Include")
        if not inc:
            continue
        if any(c.tag == M + "ExcludedFromBuild" and (c.text or "").lower() == "true" and _cond_ok(c) for c in e):
            continue
        real = resolve(os.path.join(d, inc.replace("\\", "/")))
        (srcs if real else missing).append(real or inc)
    incs, defs, forced = [], [], []
    for grp in r.iter(M + "ItemDefinitionGroup"):
        # forced includes (/FI) from ANY configuration: Debug/Tune force pchsrr2.h, Release relies on /Yu
        for e in grp.iter(M + "ForcedIncludeFiles"):
            for x in re.split(r"[;,]", e.text or ""):
                x = x.strip()
                if x and not x.startswith("%(") and x not in forced:
                    forced.append(x)
        if not _cond_ok(grp):
            continue
        for cl in grp.iter(M + "ClCompile"):
            for e in cl.iter(M + "AdditionalIncludeDirectories"):
                for x in re.split(r"[;,]", e.text or ""):
                    x = x.strip()
                    if x and not x.startswith("%("):
                        real = resolve(os.path.join(d, x.replace("\\", "/")))
                        if real and real not in incs:
                            incs.append(real)
            for e in cl.iter(M + "PreprocessorDefinitions"):
                for x in re.split(r"[;,]", e.text or ""):
                    x = x.strip()
                    if x and not x.startswith("%(") and x not in defs:
                        defs.append(x)
    return {"name": name, "vcxproj": vcx, "dir": d, "kind": kind, "sources": srcs, "missing": missing,
            "includes": incs, "defines": defs, "forced": forced}


if __name__ == "__main__":
    for p in projects():
        print(f"{p['name']:12} {p['kind']:15} {len(p['sources']):4} src  {len(p['includes']):2} inc  "
              f"missing={len(p['missing'])}  defs={p['defines']}")
