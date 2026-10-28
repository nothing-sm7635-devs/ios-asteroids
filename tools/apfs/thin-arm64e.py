#!/usr/bin/env python3
# Bergamota tool (not from UniXNU).
"""Write the arm64e slice of a fat Mach-O (parse only; nothing is executed).

  thin-arm64e.py FAT OUT
"""
import struct, sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, 'rb').read()
if data[:4] != b'\xca\xfe\xba\xbe':
    sys.exit(f'{src}: not a fat Mach-O')
count, = struct.unpack_from('>I', data, 4)
for i in range(count):
    cpu, sub, off, size, _ = struct.unpack_from('>5I', data, 8 + 20 * i)
    if cpu == 0x100000c and sub & 0xffffff == 2:
        open(dst, 'wb').write(data[off:off + size])
        break
else:
    sys.exit(f'{src}: no arm64e slice')
