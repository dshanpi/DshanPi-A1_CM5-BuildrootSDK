#!/bin/bash
set -euo pipefail

usage() {
	printf 'Usage: %s /absolute/path/to/rk3576-sdk [--debug]\n' "$0"
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
	usage >&2
	exit 2
fi

sdk_root=$(realpath "$1")
debug_mode=${2:-}
repo_root=$(cd "$(dirname "$0")/.." && pwd)

if [ ! -x "$sdk_root/build.sh" ] || [ ! -d "$sdk_root/kernel-6.1" ]; then
	printf 'Not an RK3576 Linux 6.1 SDK root: %s\n' "$sdk_root" >&2
	exit 1
fi

apply_once() {
	local target=$1
	local patch=$2
	if git -C "$target" apply --check "$patch" 2>/dev/null; then
		git -C "$target" apply "$patch"
		printf 'Applied %s\n' "$(basename "$patch")"
	elif git -C "$target" apply --reverse --check "$patch" 2>/dev/null; then
		printf 'Already applied: %s\n' "$(basename "$patch")"
	else
		printf 'Patch conflicts with target tree: %s\n' "$patch" >&2
		exit 1
	fi
}

install_file() {
	local relative=$1
	install -D -m 0644 "$repo_root/$relative" "$sdk_root/$relative"
}

install_kernel_file() {
	local relative=$1
	install -D -m 0644 \
		"$repo_root/kernel/$relative" \
		"$sdk_root/kernel-6.1/$relative"
}

apply_once "$sdk_root/kernel-6.1" "$repo_root/patches/0001-kernel-cm5-config-and-integration.patch"
apply_once "$sdk_root/u-boot" "$repo_root/patches/0002-uboot-cm5-sdmmc-power.patch"
apply_once "$sdk_root/buildroot" "$repo_root/patches/0003-buildroot-cm5-tools.patch"
apply_once "$sdk_root/kernel-6.1" "$repo_root/patches/0004-cm5-overlay-build.patch"

if [ "$debug_mode" = "--debug" ]; then
	apply_once "$sdk_root/kernel-6.1" "$repo_root/patches/9001-debug-only-regmap-rk628-v4l2.patch"
elif [ -n "$debug_mode" ]; then
	usage >&2
	exit 2
fi

for relative in \
	arch/arm64/boot/dts/rockchip/rk3576-100ask-dshanpi-a1-linux.dts \
	arch/arm64/boot/dts/rockchip/rk3576-100ask-dshanpi-a1-common.dtsi \
	arch/arm64/boot/dts/rockchip/rk3576-100ask-a1-cm5-base-v1.dtsi \
	arch/arm64/boot/dts/rockchip/rk3576-100ask-a1-cm5-pcie1-overlay.dts \
	arch/arm64/boot/dts/rockchip/cm5-overlays.mk \
	arch/arm64/boot/dts/rockchip/rk3576-100ask-1024-768-mipi.dtsi \
	arch/arm64/boot/dts/rockchip/rk3576-100ask-ov13850-3cam.dtsi; do
	install_kernel_file "$relative"
done

for relative in \
	external/camera_engine_rkaiq/rkaiq/iqfiles/isp39/ov13850_ZC-OV13850R2A-V1_Largan-50064B31.json \
	device/rockchip/.chips/rk3576/rockchip_rk3576_dshanpi-a1_defconfig \
	device/rockchip/common/scripts/build-cm5-mode.sh \
	device/rockchip/common/post-hooks/89-aic8800-modules.sh; do
	install_file "$relative"
done

rsync -a \
	--exclude='*.o' --exclude='*.a' --exclude='*.ko' --exclude='*.cmd' \
	--exclude='*.mod' --exclude='*.mod.c' --exclude='*.mod.o' \
	--exclude='modules.order' --exclude='Module.symvers' \
	"$repo_root/kernel/drivers/net/wireless/aic8800/" \
	"$sdk_root/kernel-6.1/drivers/net/wireless/aic8800/"

rsync -a \
	"$repo_root/buildroot/board/rockchip/rk3576/fs-overlay/usr/lib/firmware/aic8800d80/" \
	"$sdk_root/buildroot/board/rockchip/rk3576/fs-overlay/usr/lib/firmware/aic8800d80/"

install -D -m 0644 \
	"$repo_root/buildroot/package/bluez5_utils/0003-add-aic-hciattach-support.patch" \
	"$sdk_root/buildroot/package/bluez5_utils/0003-add-aic-hciattach-support.patch"

chmod 0755 "$sdk_root/device/rockchip/common/post-hooks/89-aic8800-modules.sh"
ln -sfn rockchip_rk3576_dshanpi-a1_defconfig \
	"$sdk_root/device/rockchip/.chips/rk3576/rockchip_defconfig"

printf 'CM5 differential package applied to %s\n' "$sdk_root"
printf 'Build order: ./build.sh uboot && ./build.sh kernel && ./build.sh buildroot && ./build.sh updateimg\n'
