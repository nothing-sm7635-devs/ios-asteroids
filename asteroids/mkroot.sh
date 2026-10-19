#!/bin/bash
# Build the iOS 16.7.16 root image for the asteroids userdata partition from
# the iPhone X IPSW: the sweet pipeline (README "Build the iOS root image") in
# one go. Run as your user in a terminal; sudo asks for the password (loop and
# FUSE mounts, root-owned files). Writes nothing to the phone.
#
#   asteroids/mkroot.sh IPSW OUTDIR [SIZE]      SIZE default 16G
#   -> OUTDIR/ios16-root-20H392-a1.img (+ .simg for fastboot, + .sha256)
#
# Needs: apfs-fuse (built from source, in PATH), hfsprogs (mkfs/fsck.hfsplus),
# img2simg, rsync, unzip, python3.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
abspath() { python3 -I -c 'import os, sys; print(os.path.abspath(sys.argv[1]))' "$1"; }
ipsw=$(abspath "${1:?usage: mkroot.sh IPSW OUTDIR [SIZE]}")
out=$(abspath "${2:?usage: mkroot.sh IPSW OUTDIR [SIZE]}")
size=${3:-16G}
img=$out/ios16-root-20H392-a1.img
for t in apfs-fuse mkfs.hfsplus fsck.hfsplus img2simg rsync unzip; do
	command -v $t > /dev/null || { echo "missing: $t" >&2; exit 1; }
done
[ ! -e "$img" ] || { echo "refusing to overwrite $img" >&2; exit 1; }
mkdir -p "$out"

# The three APFS images for iPhone10,3 (d22ap, erase restore), by BuildManifest role.
read -r os sysos appos < <(python3 -I - "$ipsw" <<'EOF'
import plistlib, sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
bm = plistlib.loads(z.read('BuildManifest.plist'))
for bi in bm['BuildIdentities']:
    info = bi['Info']
    if info.get('DeviceClass') == 'd22ap' and info.get('RestoreBehavior') == 'Erase':
        m = bi['Manifest']
        print(m['OS']['Info']['Path'], m['Cryptex1,SystemOS']['Info']['Path'], m['Cryptex1,AppOS']['Info']['Path'])
        break
else:
    sys.exit('no d22ap erase identity')
EOF
)
echo "OS $os, SystemOS $sysos, AppOS $appos"
unzip -o -j "$ipsw" "$os" "$sysos" "$appos" -d "$out"
"$repo/tools/mkiosroot.sh" "$out/$(basename "$os")" "$out/$(basename "$sysos")" "$out/$(basename "$appos")" "$img" "$size"

sudo fsck.hfsplus -fy "$img" || true
mnt=$(mktemp -d)
sudo mount -t hfsplus -o loop,force "$img" "$mnt"
trap 'sudo umount "$mnt" 2>/dev/null; rmdir "$mnt" 2>/dev/null' EXIT
sudo python3 -I "$repo/tools/sweet-root-tweaks.py" "$mnt"
sudo umount "$mnt"
trap - EXIT
rmdir "$mnt"
sudo fsck.hfsplus -f -n "$img" || true
sudo chown "$(id -u):$(id -g)" "$img"
img2simg "$img" "$img.simg"
sha256sum "$img" > "$img.sha256"
ls -l "$img" "$img.simg"
echo "done: flash with  fastboot -S 512M flash userdata $img.simg"
