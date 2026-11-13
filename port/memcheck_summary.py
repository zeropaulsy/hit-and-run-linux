#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. If a copy of the MPL
# was not distributed with this file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Group valgrind memcheck reports by the origin of the uninitialised value (port/memcheck.sh).

usage: memcheck_summary.py memcheck.log
Each group shows the origin stack (where the memory was allocated, or the stack frame) and the first few sites
that used the value. Many use sites usually trace back to one origin: fix the origin.
"""
import collections
import re
import sys

SOURCE = re.compile(r'\((\w[\w.]*\.(?:cpp|hpp|h|c|inl)):(\d+)\)')
# allocator frames: skip them to find the frame that asked for the memory
ALLOCATOR = re.compile(r'memorymanager\.cpp|srrmemory\.cpp|radobject\.hpp|memorypool\.cpp|object\.cpp:163|'
                       r'operator new')


def main(path):
    groups = collections.OrderedDict()
    for block in re.split(r'\n==\d+== \n', open(path, errors='replace').read()):
        lines = [re.sub(r'^==\d+== ', '', l).strip() for l in block.splitlines()]
        origin_at = next((i for i, l in enumerate(lines) if 'Uninitialised value was created' in l), None)
        if origin_at is None:
            continue
        origin = lines[origin_at:]
        frames = [l for l in origin[1:] if SOURCE.search(l) and not ALLOCATOR.search(l)]
        key = origin[0][:45] + ' | ' + (frames[0] if frames else (origin[1] if len(origin) > 1 else '?'))
        use = next((l for l in lines[1:origin_at] if SOURCE.search(l)), lines[1] if len(lines) > 1 else '?')
        g = groups.setdefault(key, {'n': 0, 'uses': [], 'origin': origin[1:10]})
        g['n'] += 1
        g['uses'].append(use)
    print(len(groups), 'origins')
    for key, g in sorted(groups.items(), key=lambda kv: -kv[1]['n']):
        print('\n### x%d %s' % (g['n'], key[:230]))
        for l in g['origin']:
            print('    from', l[:170])
        for l in list(dict.fromkeys(g['uses']))[:4]:
            print('    used', l[:170])


if __name__ == '__main__':
    main(sys.argv[1])
