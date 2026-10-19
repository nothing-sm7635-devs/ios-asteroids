#!/bin/bash
# Build a Nothing Phone (3a) probe and wrap it in a boot.img v4 that mirrors
# the stock boot_b header (no ramdisk; ABL takes the DTB from vendor_boot).
# Toolchain: LineageOS prebuilt clang + lld (no GNU cross gcc on this host).
#   asteroids/probe/build.sh N [-z]   N: probe number; -z: gzip the Image
#   -> out/probeN-boot.img
set -euo pipefail
cd "$(dirname "$0")"
LOS=${LOS:-/home/saalim/os/los24}
LLVM=$LOS/prebuilts/clang/host/linux-x86/clang-r596125/bin
MKBOOTIMG=$LOS/system/tools/mkbootimg/mkbootimg.py
CC="$LLVM/clang --target=aarch64-none-elf"
CFLAGS="-O2 -ffreestanding -nostdlib -fno-pic -mgeneral-regs-only -mstrict-align -fno-stack-protector -fno-jump-tables -Wall -Wextra"
SHIM=../../shim
n=${1:?usage: build.sh N [-z]}
case $n in
1) srcs="probe.c mmu.c" ;;
2) srcs="probe2.c ufs.c" ;;
*) echo "no probe $n" >&2; exit 1 ;;
esac
srcs="$srcs common.c fbcon.c"
mkdir -p out/$n
$CC $CFLAGS -c $SHIM/start.S -o out/$n/start.o
objs=
for f in $srcs; do
	$CC $CFLAGS -c $f -o out/$n/${f%.*}.o
	objs="$objs out/$n/${f%.*}.o"
done
# The image runs wherever ABL puts it with no relocation pass: only the
# header's image_size (start.o, linked at 0) may be an absolute address.
abs=$($LLVM/llvm-readelf -r $objs | grep -c ABS64 || true)
[ "$abs" = 0 ] || { echo "error: $abs absolute relocations in C objects" >&2; exit 1; }
$LLVM/ld.lld -static -T $SHIM/shim.ld --build-id=none -N out/$n/start.o $objs -o out/$n/probe.elf
$LLVM/llvm-objcopy -O binary out/$n/probe.elf out/$n/Image
# ABL copies the file; pad to image_size so .bss/stacks exist as zeroed bytes.
size=$($LLVM/llvm-nm out/$n/probe.elf | awk '$3=="_end"{print strtonum("0x"$1)}')
truncate -s "$size" out/$n/Image
kernel=out/$n/Image
if [ "${2:-}" = -z ]; then
	gzip -9nc out/$n/Image > out/$n/Image.gz
	kernel=out/$n/Image.gz
fi
python3 $MKBOOTIMG --header_version 4 --os_version 17.0.0 --os_patch_level 2026-10 \
	--kernel $kernel --cmdline "" -o out/probe$n-boot.img
ls -l out/$n/Image out/probe$n-boot.img
