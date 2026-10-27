#!/bin/bash
# Nothing Phone (3a) "asteroids": iOS 26 boot image for `fastboot boot` (nothing is flashed to boot).
#   asteroids/build-boot.sh KERNEL NAME [BOOT-ARGS...]   -> asteroids/out/NAME.adt, NAME.payload, NAME-boot.img
#
# Kernel: the xnu-12377 CI artifact with xnu/patches-12377/2000-asteroids-board.patch. ADT: tools/mkadt.py
# --board asteroids, iPhone 11 identity (needs research/ios26/n104ap.adt for /product). Payload: tools/mkpayload.py
# version 1 (no --compress). Loader: the asteroids shim of the ios-sweet port (Nothing's ABL drops the boot v4
# ramdisk, so the shim carries the payload behind its own Image), ASTEROIDS_SHIM, default ../ios-sweet/asteroids/shim.
#
# rd=ufsroot0 ufsrw=1 writes userdata (the iOS root and the persistent log at its end): refused unless
# ASTEROIDS_ALLOW_UFS_WRITES=yes, i.e. once userdata holds the iOS 26 root.
set -euo pipefail
kernel=$(python3 -I -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "${1:?usage: build-boot.sh KERNEL NAME [BOOT-ARGS...]}")
cd "$(dirname "$0")/.."
name=${2:?usage: build-boot.sh KERNEL NAME [BOOT-ARGS...]}
shift 2
# tools/renegade/mkboot-full.sh's iOS 26 command line without the Poco F1 hardware (USB, battery, MDP scaler/flip)
args=${*:-"debug=0xc serial=3 cpus=8 keepsyms=1 -noprogress rd=ufsroot0 unentitled_ios_sim_launch=1 ignition_level=1 wdt_test=no_user_checkin ufsrw=1 rootrw=1 launchd_unsecure_cache=1 wdt=-1 sweetfb_noconsole=1 sweetfb_hz=30 sweetfb_scale=1 sweetarm64e=1 launchd_disable_all_timeouts=1"}
case " $args " in
*" ufsrw=1 "*)
	[ "${ASTEROIDS_ALLOW_UFS_WRITES:-}" = yes ] || {
		echo "refusing ufsrw=1 (writes userdata); set ASTEROIDS_ALLOW_UFS_WRITES=yes once userdata holds the iOS 26 root" >&2
		exit 1
	} ;;
esac
shim=${ASTEROIDS_SHIM:-$PWD/../ios-sweet/asteroids/shim}
[ -x "$shim/build.sh" ] || { echo "no asteroids shim at $shim (ASTEROIDS_SHIM)" >&2; exit 1; }
out=$PWD/asteroids/out
mkdir -p "$out"
python3 tools/mkadt.py "$out/$name.adt" --board asteroids --highmem --display-scale 2 --points 414x896 --identity n104ap
python3 tools/mkpayload.py "$kernel" "$out/$name.adt" "$out/$name.payload" --cmdline "$args"
"$shim/build.sh" "$out/$name.payload" "$out/$name-boot.img"
echo "boot-args: $args"
