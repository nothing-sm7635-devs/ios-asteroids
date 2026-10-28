#!/bin/sh
# Static APFS-closure audit of our kernel with UniXNU-zeta's gates (copies in this directory).
#   tools/apfs/run-audit.sh KERNEL OUTDIR
# Inputs (git-ignored, see NOTES.md): research/ios26/kdk/kdk-25F84/root-KDK (KDK 26.5.2 25F84, apfs 2811.121.1,
# the kexts UniXNU-zeta pinned), research/ios26/kdk/thin-25F84 (arm64e slices, thin-arm64e.py),
# research/ios26/apfs-src/IOStorageFamily (apple-oss-distributions IOStorageFamily-337.100.1, 7edb88f).
# XNU=path to the xnu tree (headers for the class-hierarchy check). Only parses files; nothing is executed.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
repo=$(cd "$here/../.." && pwd)
kernel=${1:?usage: run-audit.sh KERNEL OUTDIR}
out=${2:?usage: run-audit.sh KERNEL OUTDIR}
xnu=${XNU:-$HOME/os/ios/b12377/xnu}
kdk=$repo/research/ios26/kdk/kdk-25F84/root-KDK/System/Library
thin=$repo/research/ios26/kdk/thin-25F84
storage=$repo/research/ios26/apfs-src/IOStorageFamily
mkdir -p "$out"
for m in apfs AppleMobileFileIntegrity AppleFSCompressionTypeZlib CoreTrust AppleImage4 AppleEffaceableStorage \
	CoreAnalyticsFamily IOStorageFamily corecrypto; do
	[ -f "$thin/$m" ] || python3 -I "$here/thin-arm64e.py" "$kdk/Extensions/$m.kext/Contents/MacOS/$m" "$thin/$m"
done
# IOCryptoAcceleratorFamily ships thin arm64e already.
[ -f "$thin/IOCryptoAcceleratorFamily" ] || cp "$kdk/Extensions/IOCryptoAcceleratorFamily.kext/Contents/MacOS/IOCryptoAcceleratorFamily" "$thin/"
roots="--root com.apple.filesystems.apfs --root com.apple.driver.AppleMobileFileIntegrity --root com.apple.AppleFSCompression.AppleFSCompressionTypeZlib"
python3 "$here/audit-zeta-apfs.py" --candidate "$thin/apfs" --native "$kernel" --output "$out/imports-apfs.json" || true
python3 "$here/audit-zeta-apfs-closure.py" --native "$kernel" --kdk-extensions "$kdk/Extensions" --output "$out/closure-apfs.json" || true
python3 "$here/audit-zeta-apfs-closure.py" --native "$kernel" --kdk-extensions "$kdk/Extensions" $roots --output "$out/closure-union.json" || true
python3 "$here/audit-zeta-apfs-closure.py" --native "$kernel" --kdk-extensions "$kdk/Extensions" $roots --kdk-binaries --output "$out/closure-union-kdkbin.json" || true
python3 "$here/audit-zeta-apfs-vtables.py" --native "$kernel" --kdk-kernel "$kdk/Kernels/kernel.development.vmapple" \
	--kdk-storage "$thin/IOStorageFamily" --apfs "$thin/apfs" --output "$out/vtables.json" || true
providers="--kdk-provider $kdk/Kernels/kernel.development.vmapple --kdk-provider $thin/IOStorageFamily
	--kdk-provider $thin/AppleEffaceableStorage --kdk-provider $thin/CoreAnalyticsFamily --kdk-provider $thin/IOCryptoAcceleratorFamily"
python3 "$here/audit-zeta-apfs-extra-vtables.py" --native "$kernel" $providers --apfs "$thin/apfs" --apfs "$thin/AppleMobileFileIntegrity" \
	--apfs "$thin/AppleFSCompressionTypeZlib" --xnu-source "$xnu" --storage-source "$storage" --record-instance-sizes \
	--output "$out/extra-union.json" || true
all=""
for m in apfs AppleMobileFileIntegrity AppleFSCompressionTypeZlib IOStorageFamily corecrypto CoreTrust AppleImage4 \
	IOCryptoAcceleratorFamily AppleEffaceableStorage CoreAnalyticsFamily; do all="$all --apfs $thin/$m"; done
python3 "$here/audit-zeta-apfs-extra-vtables.py" --native "$kernel" $providers $all --xnu-source "$xnu" \
	--storage-source "$storage" --record-instance-sizes --output "$out/extra-kdkbin.json" || true
for m in apfs AppleMobileFileIntegrity AppleFSCompressionTypeZlib CoreTrust AppleImage4 AppleEffaceableStorage \
	CoreAnalyticsFamily IOCryptoAcceleratorFamily IOStorageFamily corecrypto; do
	python3 "$here/audit-zeta-apfs-pauth.py" --apfs "$thin/$m" --output "$out/pauth-$m.json" >/dev/null || true
done
echo "reports in $out"
