#!/bin/bash
# Chlorophyll (Metal on Turnip, Mesa 26.2.5) for asteroids, built on LINUX: no Xcode, no xcrun, no brew.
#   asteroids/build-chlorophyll.sh WORK      -> WORK/out-metal/{bgmtl-inject.dylib,libchlorophyll.dylib,libdrm.2.dylib,bgl-launch}
# then the root: RENEGADE_CHLOROPHYLL=WORK/out-metal tools/renegade/mkroot-full.sh ... (root_tweaks18.chlorophyll).
#
# Toolchain: clang 18 (-target arm64-apple-ios18.0), Theos's ld64/ldid/otool and iPhoneOS SDK (THEOS, default ~/theos;
# THEOS_SDK to use another SDK, e.g. one from `xtool sdk install`), llvm-ar/llvm-ranlib 18, meson >= 1.4 (a venv in WORK),
# mako, pyyaml, bison, glslangValidator. Plain arm64, not arm64e: the asteroids kernel runs userland with the PAC keys off
# and tells dyld ptrauth_disabled=1, so dyld loads arm64 code into arm64e processes; clang 18 cannot emit Apple's versioned
# arm64e ABI anyway. Chlorophyll's Metal test shaders (xcrun metal) are skipped: only the libraries are built.
# With the iOS 16.5 SDK, bgmtl.m needs declarations of newer Metal API: bin/ios-sdk-compat.h (generated below).
set -euo pipefail
WORK=$(realpath -m "${1:?usage: build-chlorophyll.sh WORK}")
HERE="$(cd "$(dirname "$0")/.." && pwd)"
THEOS=${THEOS:-$HOME/theos}
SDK=${THEOS_SDK:-$THEOS/sdks/iPhoneOS16.5.sdk}
TB=$THEOS/toolchain/linux/iphone/bin
CHL_REF=${CHL_REF:-ios26}
mkdir -p "$WORK/bin"
cd "$WORK"
[ -d Chlorophyll ] || git clone -q --depth 1 -b "$CHL_REF" https://github.com/ry-diffusion/Chlorophyll.git Chlorophyll
[ -d mesa-src-26.2.5 ] || git clone -q --depth 1 --branch mesa-26.2.5 https://gitlab.freedesktop.org/mesa/mesa.git mesa-src-26.2.5
[ -d libdrm-src ] || git clone -q --depth 1 --branch libdrm-2.4.124 https://gitlab.freedesktop.org/mesa/drm.git libdrm-src
[ -x venv/bin/meson ] || { python3 -m venv venv && venv/bin/pip -q install meson mako pyyaml packaging; }
export PATH="$WORK/venv/bin:$PATH"

# the cross tools
for n in cc:clang-18 cxx:clang++-18; do
	cat > "bin/ios-${n%%:*}" <<EOF
#!/bin/sh
# iOS arm64 cross compiler: clang 18, the iPhoneOS SDK, Theos's ld64 (only when linking: meson's checks use -Werror=unused-command-line-argument)
link=yes
for a in "\$@"; do case "\$a" in -c|-S|-E|-M|-MM) link=no ;; esac; done
if [ \$link = yes ]; then
	exec ${n##*:} -target arm64-apple-ios18.0 -isysroot $SDK --ld-path=$TB/ld "\$@"
fi
exec ${n##*:} -target arm64-apple-ios18.0 -isysroot $SDK "\$@"
EOF
	chmod +x "bin/ios-${n%%:*}"
done
cat > bin/ios-ranlib <<'EOF'
#!/bin/sh
# llvm-ranlib without Apple ranlib's -c, which meson passes on Darwin hosts
args=""
for a in "$@"; do [ "$a" = -c ] || args="$args \"$a\""; done
eval exec /usr/bin/llvm-ranlib-18 $args
EOF
chmod +x bin/ios-ranlib
ln -sf /usr/bin/llvm-ar-18 bin/ios-ar
for t in strip otool ldid; do ln -sf "$TB/$t" "bin/ios-$t"; done
cat > bin/ios-sdk-compat.h <<'EOF'
/* Linux build with an iPhoneOS SDK older than 26: declarations of the newer Metal API that Chlorophyll uses. Interfaces
 * only: every use is an Objective-C message, nothing links against them. */
#pragma once
#import <Metal/Metal.h>
#if !defined(__IPHONE_26_0)
@interface MTLTextureViewDescriptor : NSObject <NSCopying>
@property (nonatomic) MTLPixelFormat pixelFormat;
@property (nonatomic) MTLTextureType textureType;
@property (nonatomic) NSRange levelRange;
@property (nonatomic) NSRange sliceRange;
@property (nonatomic) MTLTextureSwizzleChannels swizzle;
@end
#endif
EOF

# Mesa with Chlorophyll's patches (turnip/build-turnip.sh)
D="$WORK/mesa-turnip26"
mkdir -p "$D"
cd "$D"
if [ ! -d mesa ]; then
	git clone -q --shared "$WORK/mesa-src-26.2.5" mesa
	for p in air-frontend ir3-round-default ir3-ssbo-subdword ir3-context-error-return ir3-disk-cache-macho turnip-ios; do
		git -C mesa apply "$WORK/Chlorophyll/patches/mesa-26.2/$p.patch"
	done
	mkdir -p mesa/src/compiler/air
	for f in "$WORK"/Chlorophyll/air/*; do ln -sf "$f" "mesa/src/compiler/air/$(basename "$f")"; done
fi
mkdir -p drm-ios drm-ios-link pc-ios
"$WORK/bin/ios-cc" -O2 -g -Wall -Wextra -fvisibility=hidden -dynamiclib -install_name @rpath/libdrm.2.dylib -current_version 2.4.0 \
	-compatibility_version 2.0.0 "$HERE/gpu/freedreno-sweet/sweet_drm.c" -o drm-ios/libdrm.2.arm64.dylib -framework IOKit -framework CoreFoundation
cp drm-ios/libdrm.2.arm64.dylib drm-ios-link/libdrm.2.dylib
ln -sf libdrm.2.dylib drm-ios-link/libdrm.dylib
cat > pc-ios/libdrm.pc <<EOP
Name: libdrm
Description: Bergamota libdrm stand-in (gpu/freedreno-sweet)
Version: 2.4.124
Cflags: -I$WORK/libdrm-src/include/drm -I$WORK/libdrm-src
Libs: -L$D/drm-ios-link -ldrm
EOP
cat > ios.cross <<EOT
[binaries]
c = '$WORK/bin/ios-cc'
cpp = '$WORK/bin/ios-cxx'
objc = '$WORK/bin/ios-cc'
ar = '$WORK/bin/ios-ar'
strip = '$WORK/bin/ios-strip'
ranlib = '$WORK/bin/ios-ranlib'
pkg-config = '$(command -v pkg-config)'

[built-in options]
pkg_config_path = ['$D/pc-ios']

[host_machine]
system = 'darwin'
subsystem = 'ios'
cpu_family = 'aarch64'
cpu = 'arm64'
endian = 'little'

[properties]
pkg_config_libdir = ['$D/pc-ios']
needs_exe_wrapper = true
EOT
[ -d build-ios ] || meson setup build-ios mesa --cross-file ios.cross --buildtype=release -Dgallium-drivers= -Dvulkan-drivers=freedreno \
	-Dfreedreno-kmds=msm -Dvulkan-layers= -Dplatforms= -Dllvm=disabled -Dopengl=false -Dgles1=disabled -Dgles2=disabled -Dglx=disabled \
	-Degl=disabled -Dgbm=disabled -Dtools= -Dvalgrind=disabled -Dlibunwind=disabled -Dzstd=disabled -Dxmlconfig=disabled -Dexpat=disabled \
	-Dglvnd=disabled -Dlmsensors=disabled -Dbuild-tests=false -Dshader-cache=enabled -Dvideo-codecs= -Dperfetto=false -Dspirv-tools=disabled \
	-Ddisplay-info=disabled -Dprefix=/ > setup-ios.log
ninja -j"$(nproc)" -C build-ios src/freedreno/vulkan/libvulkan_freedreno.dylib src/freedreno/vulkan/libchlorophyll.dylib

# the Metal device and the injection (turnip/build-metal.sh, without the test probes and shaders), and bgl-launch
OUT="$WORK/out-metal"
mkdir -p "$OUT"
cd "$OUT"
CC="$WORK/bin/ios-cc"
OBJC=(-O1 -g -fno-objc-arc -Wall -Wno-deprecated-declarations -Wno-unguarded-availability-new -include "$WORK/bin/ios-sdk-compat.h")
cp -f "$D/build-ios/src/freedreno/vulkan/libchlorophyll.dylib" .
"$WORK/bin/ios-strip" -x libchlorophyll.dylib
cp -f "$D/drm-ios/libdrm.2.arm64.dylib" libdrm.2.dylib
"$CC" "${OBJC[@]}" -I"$WORK/Chlorophyll/air" -c "$WORK/Chlorophyll/src/bgmtl.m" -o bgmtl.o
"$CC" "${OBJC[@]}" -c "$WORK/Chlorophyll/inject/bgmtl_inject.m" -o bgmtl_inject.o
"$CC" -dynamiclib -o bgmtl-inject.dylib bgmtl.o bgmtl_inject.o libchlorophyll.dylib libdrm.2.dylib -framework Foundation -framework Metal \
	-framework IOSurface -install_name /usr/local/lib/bgmtl/bgmtl-inject.dylib -Wl,-rpath,@loader_path
"$CC" -O2 -Wall "$HERE/gpu/eagl/launch/bgl-launch.c" -o bgl-launch -framework IOKit -framework CoreFoundation
for f in libchlorophyll.dylib libdrm.2.dylib bgmtl-inject.dylib bgl-launch; do "$WORK/bin/ios-ldid" -S "$f"; done
ls -l libchlorophyll.dylib libdrm.2.dylib bgmtl-inject.dylib bgl-launch
