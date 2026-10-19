#!/usr/bin/env python3
"""Wrap testpayload.S's flat binary in a SWEETXNU payload (tools/mkpayload.py's
format) with a real ADT, so the shim loads it like an xnu kernel.

  testpayload.py CODE.bin ADT OUT [--cmdline "..."]
"""
import argparse, os, struct, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '../../tools'))
import mkpayload  # noqa: E402

VIRT_BASE = 0xfffffe0007000000	# any 32MB-aligned kernel-like VA
TEXT_OFF = 0x4000

ap = argparse.ArgumentParser()
ap.add_argument('code'); ap.add_argument('adt'); ap.add_argument('out')
ap.add_argument('--cmdline', default='test payload')
a = ap.parse_args()
code = open(a.code, 'rb').read()
adt_blob = open(a.adt, 'rb').read()
phys_base, mem_size = mkpayload.adt_dram(adt_blob)
vmaddr = VIRT_BASE + TEXT_OFF
vmsize = (len(code) + mkpayload.PAGE - 1) & ~(mkpayload.PAGE - 1)

hdr = bytearray(mkpayload.SEG_TABLE + 32)
out = hdr + code + b'\0' * (-len(code) & 0xf)
adt_off = len(out)
out += adt_blob + b'\0' * (-len(adt_blob) & 0xf)
struct.pack_into('<8sII', out, 0, b'SWEETXNU', 1, 1)
struct.pack_into('<7Q', out, 0x10, VIRT_BASE, vmaddr, adt_off, len(adt_blob), vmaddr + vmsize,
                 phys_base, mem_size)
cl = a.cmdline.encode()[:mkpayload.BOOT_LINE_LENGTH - 1]
out[mkpayload.CMDLINE:mkpayload.CMDLINE + len(cl)] = cl
struct.pack_into('<4Q', out, mkpayload.SEG_TABLE, vmaddr, vmsize, len(hdr), len(code))
open(a.out, 'wb').write(out)
print(f'{a.out}: {len(out)} bytes, code at pa {phys_base + TEXT_OFF:#x}')
