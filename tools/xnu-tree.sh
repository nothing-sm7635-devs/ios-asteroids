#!/bin/bash
# Recreate the patched xnu source tree the CI builds, for writing and checking
# the next patch locally (no build):
#   tools/xnu-tree.sh DIR
# -> DIR/xnu: xnu-8796.141.3 + stage scripts + xnu/patches/*.patch, as git
#    tags base / staged / patched. Write a new patch as `git diff patched`.
# CI (blacktop/darwin-xnu-build) silently SKIPS a patch that fails
# `git apply --check`, so this script stops on any patch that does not apply.
# On Linux the stage scripts stop at their xcrun step after staging; that is
# fine for editing (the staged sources are already in place).
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
dir=${1:?usage: xnu-tree.sh DIR}
mkdir -p "$dir"
cd "$dir"
[ -d xnu ] || git clone -q --depth 1 --branch xnu-8796.141.3 https://github.com/apple-oss-distributions/xnu.git xnu
cd xnu
git config user.email xnu-tree@localhost
git config user.name xnu-tree
git tag base 2>/dev/null || true
for s in "$repo"/xnu/patches/*.sh; do
	MACHINE_CONFIG=SWEET MACOS_VERSION=13.5 bash "$s" > /dev/null 2>&1 || echo "note: $(basename "$s") stopped early (xcrun on Linux?)"
done
git add -A && git commit -qm staged && git tag staged
for p in "$repo"/xnu/patches/*.patch; do
	git apply --check "$p" || { echo "error: $(basename "$p") does not apply" >&2; exit 1; }
	git apply --whitespace=nowarn "$p"
done
git add -A && git commit -qm patched && git tag patched
echo "$dir/xnu: $(ls "$repo"/xnu/patches/*.patch | wc -l | tr -d ' ') patches applied (tag: patched)"
