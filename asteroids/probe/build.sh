#!/bin/bash
# Build the Nothing Phone (3a) probe and wrap it in a boot.img v4 that mirrors
# the stock boot_b header (no ramdisk; ABL takes the DTB from vendor_boot).
# Toolchain: LineageOS prebuilt clang + lld (no GNU cross gcc on this host).
#   asteroids/probe/build.sh [-z]     -z: gzip the Image like the stock kernel
set -euo pipefail
cd "$(dirname "$0")"
LOS=${LOS:-/home/saalim/os/los24}
LLVM=$LOS/prebuilts/clang/host/linux-x86/clang-r596125/bin
MKBOOTIMG=$LOS/system/tools/mkbootimg/mkbootimg.py
CC="$LLVM/clang --target=aarch64-none-elf"
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -fno-jump-tables -Wall -Wextra"
SHIM=../../shim
mkdir -p out
$CC $CFLAGS -c $SHIM/start.S -o out/start.o
for f in probe.c fbcon.c mmu.c; do
	$CC $CFLAGS -c $f -o out/${f%.*}.o
done
# The image runs wherever ABL puts it with no relocation pass: only the
# header's image_size (start.o, linked at 0) may be an absolute address.
abs=$($LLVM/llvm-readelf -r out/probe.o out/fbcon.o out/mmu.o | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
$LLVM/ld.lld -static -T $SHIM/shim.ld --build-id=none -N \
	out/start.o out/probe.o out/fbcon.o out/mmu.o -o out/probe.elf
$LLVM/llvm-objcopy -O binary out/probe.elf out/Image
# ABL copies the file; pad to image_size so .bss/stacks exist as zeroed bytes.
size=$($LLVM/llvm-nm out/probe.elf | awk '$3=="_end"{print strtonum("0x"$1)}')
truncate -s "$size" out/Image
kernel=out/Image
if [ "${1:-}" = -z ]; then
	gzip -9nc out/Image > out/Image.gz
	kernel=out/Image.gz
fi
python3 $MKBOOTIMG --header_version 4 --os_version 17.0.0 --os_patch_level 2026-10 \
	--kernel $kernel --cmdline "" -o out/probe-boot.img
ls -l out/Image out/probe-boot.img
