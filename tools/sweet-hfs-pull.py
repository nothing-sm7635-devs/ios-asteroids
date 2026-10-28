#!/usr/bin/env python3
"""Read files of the iOS root (HFS+ on userdata) from Android recovery over adb, without pulling the partition:
hfsx.py's reader on top of `adb exec-out dd` (read only, 1 MiB blocks cached).

  tools/sweet-hfs-pull.py -s SERIAL [--dev /dev/block/by-name/userdata] OUTDIR /private/var/tmp/bgmtl.log ...
  tools/sweet-hfs-pull.py -s SERIAL --ls OUTDIR DIR ...            list directories (names, d/f)
  tools/sweet-hfs-pull.py -s SERIAL --containers OUTDIR            every container's identifier (its metadata plist), so a
                                                                   missing app/extension container shows
"""
import argparse, os, plistlib, subprocess, sys
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
    ap.add_argument('--ls', action='store_true')
    ap.add_argument('--containers', action='store_true')
    ap.add_argument('outdir')
    ap.add_argument('paths', nargs='*')
    a = ap.parse_args()
    vol = hfsx.Vol(Adb(a.serial, a.dev))
    os.makedirs(a.outdir, exist_ok=True)
    if a.ls:
        for p in a.paths:
            e = vol.lookup(p)
            if not e or e[0] != 1:
                print(f'{p}: not a directory')
                continue
            for nm, (rt, cnid, d) in sorted(vol.children.get(e[1], {}).items()):
                print(f'{p.rstrip("/")}/{nm}', 'd' if rt == 1 else 'f')
        return
    if a.containers:
        out = []
        for base in ('/private/var/mobile/Containers/Data', '/private/var/mobile/Containers/Shared/AppGroup',
                     '/private/var/containers/Data/System', '/private/var/containers/Shared/SystemGroup',
                     '/private/var/containers/Bundle/Application'):
            bases = [base]
            if base.endswith('/Data'):
                e = vol.lookup(base)
                bases = [f'{base}/{n}' for n in sorted(vol.children.get(e[1], {}))] if e and e[0] == 1 else []
            for b in bases:
                e = vol.lookup(b)
                if not e or e[0] != 1:
                    continue
                for nm in sorted(vol.children.get(e[1], {})):
                    m = vol.lookup(f'{b}/{nm}/.com.apple.mobile_container_manager.metadata.plist')
                    ident = '?'
                    if m and m[0] == 2:
                        try:
                            ident = plistlib.loads(vol.read_file(*m)).get('MCMMetadataIdentifier', '?')
                        except Exception as x:
                            ident = f'(unreadable: {x})'
                    out.append(f'{b}/{nm}\t{ident}')
        open(os.path.join(a.outdir, 'containers.tsv'), 'w').write('\n'.join(out) + '\n')
        print(f'{len(out)} containers -> {a.outdir}/containers.tsv')
        return
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
