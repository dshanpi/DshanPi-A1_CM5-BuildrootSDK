#!/bin/bash -e

POST_ROOTFS_ONLY=1

source "${RK_POST_HELPER:-$(dirname "$(realpath "$0")")/../post-hooks/post-helper}"

install_aic8800_modules()
{
	check_config RK_KERNEL || return 0

	AIC_DIR="$RK_SDK_DIR/kernel/drivers/net/wireless/aic8800"
	MOD_DST_DIR="$TARGET_DIR/lib/modules"
	COPIED=0

	[ -d "$AIC_DIR" ] || return 0

	mkdir -p "$MOD_DST_DIR"

	for rel in \
		aic8800_bsp/aic8800_bsp.ko \
		aic8800_fdrv/aic8800_fdrv.ko \
		aic8800_btlpm/aic8800_btlpm.ko; do
		src="$AIC_DIR/$rel"
		[ -f "$src" ] || continue

		install -m 0644 "$src" "$MOD_DST_DIR/"
		COPIED=1
	done

	if [ "$COPIED" -eq 1 ]; then
		message "Installed AIC8800 kernel modules to $MOD_DST_DIR"
	else
		warning "AIC8800 ko not found under $AIC_DIR (skip copy)"
	fi
}

install_aic8800_modules
