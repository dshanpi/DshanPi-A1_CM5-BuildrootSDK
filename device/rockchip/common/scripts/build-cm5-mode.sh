#!/bin/bash
# Build a boot-time CM5 USB2/PCIe1 selection without changing SDK defconfig.
set -euo pipefail
mode=${1:-usb2}
case "$mode" in usb2|pcie1) ;; *) echo "Usage: $0 [usb2|pcie1]" >&2; exit 2;; esac
sdk_root=$(realpath "$(dirname "$0")/../../../..")
cd "$sdk_root"
exec 9>output/.cm5-mode-build.lock
flock -n 9 || { echo "Another CM5 mode build is running" >&2; exit 1; }
grep -qx 'RK_KERNEL_DTS_NAME="rk3576-100ask-dshanpi-a1-linux"' output/.config ||
	{ echo "Select the CM5 SDK defconfig first" >&2; exit 1; }
if grep -qx 'RK_SECURITY=y' output/.config; then
	echo "This helper does not implement secure-boot signing" >&2; exit 1
fi
for part in MiniLoaderAll.bin uboot.img rootfs.img recovery.img parameter.txt; do
	test -r "output/firmware/$part" ||
		{ echo "Missing $part: build the complete SDK first" >&2; exit 1; }
done
toolchain="$sdk_root/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-"
test -x "${toolchain}gcc"
command -v fdtget >/dev/null || { echo "Install device-tree-compiler (fdtget)" >&2; exit 1; }
mkdir -p output/cm5-variants
artifact_dir=$(mktemp -d "$sdk_root/output/cm5-variants/${mode}-$(date +%Y%m%d-%H%M%S)-XXXXXX")
dts_dir=kernel/arch/arm64/boot/dts/rockchip
base=rk3576-100ask-dshanpi-a1-linux
selected=$base
restore_default() {
	if [ -f "$artifact_dir/default-boot.img" ]; then
		cp "$artifact_dir/default-boot.img" kernel/boot.img
		cp "$artifact_dir/default-resource.img" kernel/resource.img
	fi
}
trap restore_default EXIT
# Incrementally rebuild the default kernel/FIT using the existing SDK config.
./build.sh kernel
if [ "$mode" = pcie1 ]; then
	cp kernel/boot.img "$artifact_dir/default-boot.img"
	cp kernel/resource.img "$artifact_dir/default-resource.img"
	selected=rk3576-100ask-a1-cm5-pcie1
	make -C kernel ARCH=arm64 CROSS_COMPILE="$toolchain" -j8 "$selected.img"
	bash device/rockchip/common/scripts/mk-fitimage.sh kernel/boot.img device/rockchip/.chips/rk3576/boot.its kernel/arch/arm64/boot/Image "$dts_dir/$selected.dtb" kernel/resource.img
	cp "$dts_dir/rk3576-100ask-a1-cm5-pcie1-overlay.dtbo" "$artifact_dir/"
fi
fdtget=fdtget
test "$("$fdtget" "$dts_dir/$selected.dtb" /pcie@2a200000 status)" = okay
if [ "$mode" = usb2 ]; then
	test "$("$fdtget" "$dts_dir/$selected.dtb" /pcie@2a210000 status)" = disabled
	test "$("$fdtget" "$dts_dir/$selected.dtb" /usb@23400000 status)" = okay
	test "$("$fdtget" "$dts_dir/$selected.dtb" /usb@23400000 maximum-speed)" = high-speed
else
	test "$("$fdtget" "$dts_dir/$selected.dtb" /pcie@2a210000 status)" = okay
	test "$("$fdtget" "$dts_dir/$selected.dtb" /usb@23400000 status)" = disabled
	test "$("$fdtget" "$dts_dir/$selected.dtb" /syscon@2602e000/usb2-phy@2000 status)" = disabled
fi
cp "$dts_dir/$selected.dtb" "$artifact_dir/"
cp kernel/boot.img "$artifact_dir/boot-cm5-$mode.img"
u-boot/tools/dumpimage -i "$artifact_dir/boot-cm5-$mode.img" -T flat_dt -p 0 -o "$artifact_dir/fit-extracted.dtb" fdt
cmp "$artifact_dir/fit-extracted.dtb" "$dts_dir/$selected.dtb"
./build.sh updateimg
cp --reflink=auto output/firmware/update.img "$artifact_dir/update-cm5-$mode.img"
(
	cd "$artifact_dir"
	shopt -s nullglob
	sha256sum ./*.img ./*.dtb ./*.dtbo > SHA256SUMS
)
printf 'CM5 %s artifacts: %s\n' "$mode" "$artifact_dir"
