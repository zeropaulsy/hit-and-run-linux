#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Resolve the backtrace in a game log (crash report from port/compat/shar_crash.cpp) to functions and source lines.
usage: port/crash_symbolize.py BINARY [LOG]     (LOG defaults to stdin; the log is printed with lines resolved)
Frames in BINARY's module get "-> function (file:line)" appended, inlined callers included; others keep the
dynamic symbol the game printed. Return addresses are looked up at offset-1 (the call instruction)."""
import os
import re
import subprocess
import sys

FRAME = re.compile(r"^\s+#\d+\s+(pc|ret)\s+0x[0-9a-fA-F]+\s+(\S+)\+0x([0-9a-fA-F]+)")


def resolve(binary, offsets):
    """offset -> list of 'function (file:line)', innermost first."""
    if not offsets:
        return {}
    try:
        out = subprocess.run(["addr2line", "-f", "-C", "-i", "-a", "-e", binary] + ["0x%x" % o for o in offsets],
                             capture_output=True, text=True, check=True).stdout.splitlines()
    except (OSError, subprocess.CalledProcessError) as e:
        sys.stderr.write("addr2line failed: %s\n" % e)
        return {}
    result, current, i = {}, None, 0
    while i < len(out):
        line = out[i]
        if line.startswith("0x"):
            current = int(line, 16)
            result[current] = []
            i += 1
            continue
        func = line
        loc = out[i + 1] if i + 1 < len(out) else "??:0"
        loc = re.sub(r" \(discriminator \d+\)", "", loc)
        root = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) + "/"
        result[current].append("%s (%s)" % (func, loc.replace(root, "")))
        i += 2
    return result


def main():
    binary = sys.argv[1]
    lines = (open(sys.argv[2], errors="replace") if len(sys.argv) > 2 else sys.stdin).read().splitlines()
    module = os.path.basename(binary)
    wanted = {}
    for n, line in enumerate(lines):
        m = FRAME.match(line)
        if m and m.group(2) == module:
            off = int(m.group(3), 16)
            wanted[n] = off - 1 if m.group(1) == "ret" else off
    names = resolve(binary, sorted(set(wanted.values())))
    for n, line in enumerate(lines):
        print(line)
        for k, name in enumerate(names.get(wanted.get(n, -1), [])):
            print("        %s %s" % ("->" if k == 0 else "   inlined into", name))


if __name__ == "__main__":
    main()
