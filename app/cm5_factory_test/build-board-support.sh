#!/bin/bash
set -euo pipefail
project=$(cd "$(dirname "$0")" && pwd)
sdk=$(realpath "${1:-$project/../..}")
kernel=$(realpath "$sdk/kernel")
cross=${KERNEL_CROSS_COMPILE:-$sdk/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-}
test -s "$kernel/.config"
test -s "$kernel/Module.symvers"
make -C "$kernel" ARCH=arm64 CROSS_COMPILE="$cross" M="$project/board-support" modules
mkdir -p "$project/build"
cp "$project/board-support/cm5_yt8521.ko" "$project/build/cm5_yt8521.ko"
"$sdk/buildroot/output/rockchip_rk3576/host/bin/aarch64-buildroot-linux-gnu-strip" --strip-unneeded "$project/build/cm5_yt8521.ko"
make -s -C "$kernel" ARCH=arm64 CROSS_COMPILE="$cross" kernelrelease > "$project/build/phy-kernel-release"
echo "PHY module built for $(cat "$project/build/phy-kernel-release")"
