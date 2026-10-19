#!/bin/bash
# Build the asteroids xnu shim and wrap it in a boot.img v4 like the stock
# boot_b. The SWEETXNU payload (tools/mkpayload.py) is appended to the Image:
# ABL loads the whole kernel blob, but ignores a v4 boot ramdisk on this phone
# (the initrd is vendor_boot + flashed init_boot), see asteroids/NOTES.md.
#   asteroids/shim/build.sh PAYLOAD [OUT]    default OUT: out/shim-boot.img
set -euo pipefail
cd "$(dirname "$0")"
LOS=${LOS:-/home/saalim/os/los24}
LLVM=$LOS/prebuilts/clang/host/linux-x86/clang-r596125/bin
MKBOOTIMG=$LOS/system/tools/mkbootimg/mkbootimg.py
CC="$LLVM/clang --target=aarch64-none-elf"
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -fno-jump-tables -Wall -Wextra"
payload=$(realpath "${1:?usage: build.sh PAYLOAD [OUT]}")
out=$(realpath -m "${2:-out/shim-boot.img}")
SHIM=../../shim
PROBE=../probe
mkdir -p out
$CC $CFLAGS -c $SHIM/start.S -o out/start.o
objs=
for f in main.c loader.c $PROBE/common.c $PROBE/fbcon.c $PROBE/ufs.c $SHIM/fdt.c; do
	o=out/$(basename ${f%.*}).o
	$CC $CFLAGS -c $f -o $o
	objs="$objs $o"
done
# The image runs wherever ABL puts it with no relocation pass: only the
# header's image_size (start.o, linked at 0) may be an absolute address.
abs=$($LLVM/llvm-readelf -r $objs | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
$LLVM/ld.lld -static -T $SHIM/shim.ld --build-id=none -N out/start.o $objs -o out/shim.elf
[ -z "$($LLVM/llvm-nm -u out/shim.elf)" ] || { $LLVM/llvm-nm -u out/shim.elf; exit 1; }
$LLVM/llvm-objcopy -O binary out/shim.elf out/Image
# ABL copies the file; pad to image_size so .bss/stacks exist as zeroed bytes.
size=$($LLVM/llvm-nm out/shim.elf | awk '$3=="_end"{print strtonum("0x"$1)}')
truncate -s "$size" out/Image
# loader.c finds the payload at _end (page aligned, right after the padding)
cat out/Image "$payload" > out/Image+payload
# arm64 header image_size (offset 16) must cover the payload too, so ABL keeps it free
python3 -I -c 'import struct,sys; f=open(sys.argv[1],"r+b"); n=(len(f.read())+0xfff)&~0xfff; f.seek(16); f.write(struct.pack("<Q",n))' out/Image+payload
python3 $MKBOOTIMG --header_version 4 --os_version 17.0.0 --os_patch_level 2026-10 \
	--kernel out/Image+payload --cmdline "" -o "$out"
ls -l out/Image "$payload" "$out"
