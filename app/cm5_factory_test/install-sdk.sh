#!/bin/bash
# Install into current Buildroot target and the chip-specific persistent overlay.
set -euo pipefail
project=$(cd "$(dirname "$0")" && pwd)
sdk=${1:-$(cd "$project/../.." && pwd)}
sdk=$(realpath "$sdk")
target=$sdk/buildroot/output/rockchip_rk3576/target
chip=$(realpath "$sdk/device/rockchip/.chips/rk3576")
overlay=$chip/overlays/rootfs/cm5-factory-test
test -d "$target/etc/init.d"
test -d "$chip"
bash "$project/build-board-support.sh" "$sdk"
make -C "$project" SDK_ROOT="$sdk" install DESTDIR="$target"
make -C "$project" SDK_ROOT="$sdk" install DESTDIR="$overlay"
for destination in "$target" "$overlay"; do
    "$sdk/buildroot/output/rockchip_rk3576/host/bin/aarch64-buildroot-linux-gnu-strip" \
        --strip-unneeded "$destination/usr/bin/cm5-factory-test"
done
echo "Installed target: $target"
echo "Installed persistent overlay: $overlay"
