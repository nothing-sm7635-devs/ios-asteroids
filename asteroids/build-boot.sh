#!/bin/bash
# Kernel (from the xnu CI artifact) -> asteroids ADT -> SWEETXNU payload -> boot.img v4.
#   asteroids/build-boot.sh KERNEL NAME [BOOT-ARGS...]
#   -> asteroids/out/NAME.adt, NAME.payload, NAME-boot.img
#
# Default boot-args: no rd=, so xnu never starts the UFS root (sweet_ufs_init only
# runs for rd=ufsroot0) and nothing touches storage; it stops waiting for root.
# Do not add rd=ufsroot0 ufsrw=1 while userdata still holds Android: the root
# filesystem and the persistent log (last 4 MiB of userdata) would write to it.
set -euo pipefail
kernel=$(python3 -I -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "${1:?usage: build-boot.sh KERNEL NAME [BOOT-ARGS...]}")
cd "$(dirname "$0")"
name=${2:?usage: build-boot.sh KERNEL NAME [BOOT-ARGS...]}
shift 2
args=${*:-debug=0xc serial=3 cpus=1 keepsyms=1}
case " $args " in
*" rd=ufsroot0 "*" ufsrw=1 "*|*" ufsrw=1 "*" rd=ufsroot0 "*)
	[ "${ASTEROIDS_ALLOW_UFS_WRITES:-}" = yes ] || {
		echo "refusing rd=ufsroot0 ufsrw=1 (writes userdata); set ASTEROIDS_ALLOW_UFS_WRITES=yes once iOS owns it" >&2
		exit 1
	} ;;
esac
mkdir -p out
python3 ../tools/mkadt.py out/$name.adt --board asteroids
python3 ../tools/mkpayload.py "$kernel" out/$name.adt out/$name.payload --cmdline "$args"
shim/build.sh "$PWD/out/$name.payload" "$PWD/out/$name-boot.img"
echo "boot-args: $args"
