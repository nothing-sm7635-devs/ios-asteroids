#!/usr/bin/env python3
"""Read files of the iOS root (HFS+ on userdata) from Android recovery over adb, without pulling the partition:
hfsx.py's reader on top of `adb exec-out dd` (read only, 1 MiB blocks cached).

  tools/sweet-hfs-pull.py -s SERIAL [--dev /dev/block/by-name/userdata] OUTDIR /private/var/tmp/bgmtl.log ...
"""
import argparse, os, subprocess, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'renegade'))
import hfsx

BLK = 1 << 20


class Adb:
    def __init__(s, serial, dev):
        s.serial, s.dev, s.cache = serial, dev, {}

    def block(s, i):
        if i not in s.cache:
            out = subprocess.run(['adb', '-s', s.serial, 'exec-out', f'dd if={s.dev} bs={BLK} skip={i} count=1 2>/dev/null'],
                                 capture_output=True, check=True).stdout
            if len(out) != BLK:
                sys.exit(f'short read at block {i}: {len(out)} bytes')
            s.cache[i] = out
        return s.cache[i]

    def read(s, off, n):
        out = bytearray()
        while n > 0:
            i, o = divmod(off, BLK)
            k = min(n, BLK - o)
            out += s.block(i)[o:o + k]
            off += k
            n -= k
        return bytes(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-s', dest='serial', required=True)
    ap.add_argument('--dev', default='/dev/block/by-name/userdata')
    ap.add_argument('outdir')
    ap.add_argument('paths', nargs='+')
    a = ap.parse_args()
    vol = hfsx.Vol(Adb(a.serial, a.dev))
    os.makedirs(a.outdir, exist_ok=True)
    for p in a.paths:
        e = vol.lookup(p)
        if not e or e[0] != 2:
            print(f'{p}: not found')
            continue
        data = vol.read_file(*e)
        dst = os.path.join(a.outdir, os.path.basename(p))
        open(dst, 'wb').write(data)
        print(f'{p}: {len(data)} bytes -> {dst}')


if __name__ == '__main__':
    main()
