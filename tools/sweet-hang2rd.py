#!/usr/bin/env python3
"""Turn the "SWEET HANG" lines of an asteroids kernel log (xnu patch 2014) into a Mesa rd file for cffdump:

  tools/sweet-hang2rd.py LOG OUT.rd
  cffdump --verbose OUT.rd      (Mesa's src/freedreno/decode, built natively)

Every dumped IB becomes a buffer; the BR IB1 is the command stream cffdump starts from (it follows CP_INDIRECT_BUFFER into
the dumped IB2/IB3 by address). Chip id 0x44010000 (Adreno 810).
"""
import re, struct, sys

RD_GPUADDR, RD_CMDSTREAM_ADDR, RD_BUFFER_CONTENTS, RD_CHIP_ID = 3, 6, 12, 14

head = re.compile(r'SWEET HANG (\S+) (0x[0-9a-f]+): (\d+) dwords in handle')
words = re.compile(r'SWEET HANG (\S+) (0x[0-9a-f]+): ((?:[0-9a-f]{8} ?){8})$')


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    bufs = {}           # name -> [start iova, {iova: word}, dwords]
    for line in open(sys.argv[1], errors='replace'):
        line = line.rstrip()
        m = head.search(line)
        if m:
            bufs[m.group(1)] = [int(m.group(2), 16), {}, int(m.group(3))]
            continue
        m = words.search(line)
        if m and m.group(1) in bufs:
            at = int(m.group(2), 16)
            for k, w in enumerate(m.group(3).split()):
                bufs[m.group(1)][1][at + 4 * k] = int(w, 16)
    if not bufs:
        sys.exit('no SWEET HANG dump in ' + sys.argv[1])
    out = open(sys.argv[2], 'wb')

    def sect(t, data):
        out.write(struct.pack('<II', t, len(data)) + data)

    sect(RD_CHIP_ID, struct.pack('<Q', 0x44010000))
    for name, (start, w, n) in bufs.items():
        data = b''.join(struct.pack('<I', w.get(start + 4 * k, 0)) for k in range(n))
        sect(RD_GPUADDR, struct.pack('<III', start & 0xffffffff, len(data), start >> 32))
        sect(RD_BUFFER_CONTENTS, data)
        print(f'{name}: {start:#x}, {n} dwords')
    for first in ('BR-IB1', 'BV-IB1'):
        if first in bufs:
            start, _, n = bufs[first]
            sect(RD_CMDSTREAM_ADDR, struct.pack('<III', start & 0xffffffff, n, start >> 32))
    out.close()


if __name__ == '__main__':
    main()
