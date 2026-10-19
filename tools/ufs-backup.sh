#!/bin/bash
# Back up a rooted Android phone's UFS over adb (adb root must already be active).
# Dumps whole LUNs, plus every partition of the excluded LUN except userdata,
# plus that LUN's primary and backup GPT, then verifies device-side SHA256 sums.
#
#   tools/ufs-backup.sh OUTDIR BIGLUN [LUN...]
#   tools/ufs-backup.sh asteroids/backup sdb sda sdc sdd sde sdf
set -euo pipefail
out=$1; big=$2; shift 2
mkdir -p "$out"
cd "$out"

dump() {  # dump DEVICE_PATH LOCAL_NAME [dd args]
	local dev=$1 name=$2; shift 2
	echo "== $name"
	adb exec-out "dd if=$dev bs=4194304 $* 2>/dev/null" < /dev/null > "$name"
}

for lun in "$@"; do
	dump /dev/block/$lun $lun.img
done

# partitions of the big LUN, named by their GPT label
adb shell 'cd /dev/block/by-name; for p in *; do d=$(readlink $p); echo "$p ${d##*/}"; done' |
	tr -d '\r' | awk -v b="$big" '$2 ~ "^"b"[0-9]+$"' > "$big-partitions.txt"
while read -r label dev; do
	[ "$label" = userdata ] && continue
	dump /dev/block/$dev "$big-$label.img"
done < "$big-partitions.txt"

# GPT: first 6 and last 5 4K blocks
blocks=$(( $(adb shell cat /sys/class/block/$big/size | tr -d '\r') / 8 ))
adb exec-out "dd if=/dev/block/$big bs=4096 count=6 2>/dev/null" > "$big-gpt-primary.bin"
adb exec-out "dd if=/dev/block/$big bs=4096 skip=$((blocks - 5)) count=5 2>/dev/null" > "$big-gpt-backup.bin"

adb exec-out cat /sys/firmware/fdt > live.dtb

# verify against the device
echo "== verifying"
: > device.sha256
for lun in "$@"; do
	echo "$(adb shell sha256sum /dev/block/$lun | tr -d '\r' | cut -d' ' -f1)  $lun.img" >> device.sha256
done
while read -r label dev; do
	[ "$label" = userdata ] && continue
	# adb shell would otherwise swallow the rest of the partition list
	echo "$(adb shell sha256sum /dev/block/$dev < /dev/null | tr -d '\r' | cut -d' ' -f1)  $big-$label.img" >> device.sha256
done < "$big-partitions.txt"
sha256sum -c device.sha256
sha256sum ./*.img ./*.bin live.dtb > SHA256SUMS
echo "backup complete: $(du -sh . | cut -f1)"
