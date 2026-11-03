#!/usr/bin/env python3
"""Extract a Lucas' Mod Launcher .lmlm ("LSPA") package.

Layout (worked out from v5 files): 512-byte sectors, one field per sector.
  sector 0          "LSPA", u32 version
  sector 2          root dir record: u16 child count @2, first child's type @4
  per entry:        type (0 file, 1 dir): byte @0 of its own sector, except the
                    first child of a directory, whose type sits in the parent's
                    count sector after the count
                    name sector: u16 flags @0, UTF-16LE NUL-terminated name @2
                      flag 0x02: the info sector starts with a 10-byte
                      timestamp (u16 year, month, day, hour, min, sec, u16 ms, ?)
                    info sector (fields start @2, after the timestamp if any):
                      file: u64 size, u64 offset
                      dir:  u16 child count, u8 first child's type
The whole directory is parsed before anything is written.
"""
import os, struct, sys

SECTOR = 512
FLAG_TIMESTAMP = 0x02

def parse(d):
    if d[:4] != b'LSPA':
        sys.exit('not an LSPA package')
    version, = struct.unpack_from('<I', d, 4)
    if version != 5:
        print(f'warning: version {version}, only 5 has been seen')
    pos = 2
    files = []  # (relative path, offset, size)
    dirs = []

    def sec(n):
        if n * SECTOR >= len(d):
            sys.exit(f'directory runs past end of file (sector {n})')
        return d[n * SECTOR:(n + 1) * SECTOR]

    def walk(path, count, first_type):
        nonlocal pos
        for i in range(count):
            if i == 0:
                typ = first_type
            else:
                typ = sec(pos)[0]
                pos += 1
            name_sec = sec(pos)
            pos += 1
            flags, = struct.unpack_from('<H', name_sec, 0)
            # only FLAG_TIMESTAMP changes the layout; other bits (seen: 0x13, 0x06) don't move anything and are
            # ignored. What they mean is not known.
            name = name_sec[2:].decode('utf-16le', 'replace').split('\0')[0]
            if not name or name in ('.', '..') or '/' in name or '\\' in name:
                sys.exit(f'bad name {name!r} at sector {pos - 1}')
            rel = os.path.join(path, name)
            info = sec(pos)
            pos += 1
            at = 2 + (10 if flags & FLAG_TIMESTAMP else 0)
            if typ == 1:
                n, = struct.unpack_from('<H', info, at)
                dirs.append(rel)
                walk(rel, n, info[at + 2])
            elif typ == 0:
                size, off = struct.unpack_from('<QQ', info, at)
                if off + size > len(d):
                    sys.exit(f'{rel}: data past end ({off:#x}+{size:#x})')
                files.append((rel, off, size))
            else:
                sys.exit(f'unknown type {typ} at {rel}')

    root = sec(pos)
    pos += 1
    n, = struct.unpack_from('<H', root, 2)
    walk('', n, root[4])
    return files, dirs, pos * SECTOR

def main(src, dst):
    d = open(src, 'rb').read()
    files, dirs, dir_end = parse(d)
    first_data = min((off for _, off, size in files if size), default=dir_end)
    if first_data != dir_end:
        print(f'warning: directory ends at {dir_end:#x}, data starts at {first_data:#x}')
    for rel in dirs:
        os.makedirs(os.path.join(dst, rel), exist_ok=True)
    os.makedirs(dst, exist_ok=True)
    for rel, off, size in files:
        with open(os.path.join(dst, rel), 'wb') as f:
            f.write(d[off:off + size])
    print(f'{len(files)} files in {len(dirs)} folders -> {dst}')

if __name__ == '__main__':
    if len(sys.argv) != 3:
        sys.exit('usage: lmlm_extract.py MOD.lmlm OUTPUT_DIR')
    main(sys.argv[1], sys.argv[2])
