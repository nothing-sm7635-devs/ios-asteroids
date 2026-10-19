#!/bin/bash
# Image a2 = a1 + os_log on stderr for the poster / extension daemons
# (PosterBoard, pkd, lsd, chronod, assetsd), to see why app-extension
# discovery finds no poster providers. Run in a terminal (sudo for the mount).
# Writes nothing to the phone.
#   asteroids/mkroot-a2.sh [DIR]     DIR default research/ipsw-x
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
dir=${1:-$repo/research/ipsw-x}
a1=$dir/ios16-root-20H392-a1.img
a2=$dir/ios16-root-20H392-a2.img
[ -f "$a1" ] || { echo "missing $a1" >&2; exit 1; }
[ ! -e "$a2" ] || { echo "refusing to overwrite $a2" >&2; exit 1; }
cp --sparse=always "$a1" "$a2"
mnt=$(mktemp -d)
sudo fsck.hfsplus -fy "$a2" || true
sudo mount -t hfsplus -o loop,force "$a2" "$mnt"
trap 'sudo umount "$mnt" 2>/dev/null; rmdir "$mnt" 2>/dev/null' EXIT
sudo python3 -I "$repo/tools/launchd-env.py" "$mnt" \
	com.apple.PosterBoard com.apple.pluginkit.pkd com.apple.lsd com.apple.lsd.system \
	com.apple.chronod com.apple.assetsd
sudo umount "$mnt"
trap - EXIT
rmdir "$mnt"
sudo fsck.hfsplus -f -n "$a2" || true
img2simg "$a2" "$a2.simg"
sha256sum "$a2" > "$a2.sha256"
ls -l "$a2" "$a2.simg"
echo "done: boot with sweetnull=PosterBoard,pkd,lsd,chronod,assetsd"
