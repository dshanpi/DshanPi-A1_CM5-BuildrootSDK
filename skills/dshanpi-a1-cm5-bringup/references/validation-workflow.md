# Build and validation workflow

## Build

Use the board defconfig and Rockchip vendor MiniLoader selection from this package. A normal
full build sequence is:

```bash
./build.sh uboot
./build.sh kernel
./build.sh buildroot
./build.sh updateimg
```

After building, decompile the final DTB and verify GPIO/PHY ownership before flashing. Hash
the resolved image target with `sha256sum` and record its byte size.

## Network

AIC8800 requires the BSP module before the fullmac module. Load them only if absent, bring
up `wlan0`, then use operator-provided credentials with `wpa_supplicant` and DHCP. Never
write those credentials into this repository or a generated report.

## Cameras

Confirm media topology and sensor subdevices before capture. Validate each path alone, then
all three concurrently for at least 300 frames. A successful process exit is insufficient:
inspect frames for black output, Bayer order, color cast, exposure stability, and kernel
CSI/ISP errors.

## Audio

Confirm card enumeration, pinmux ownership, PCM state, DAPM routes, codec power registers,
and finally audible/captured content. GPIO1_D0 must show `sai2m0-sdo`, with no regulator GPIO
consumer. For microphone tests compute AC RMS after subtracting DC; isolated peaks can be
switching or jack transients rather than speech.

## PCIe and TF

For PCIe1, check the physical R30/R31 population before link conclusions, then inspect reset,
PHY mode, LTSSM, and `lspci`. For TF boot, distinguish BootROM reads from SPL reinitialization;
Linux recognizing the card does not prove the SPL power/pinctrl handoff works.

## Status language

Use `implemented`, `enumerated`, `data-path verified`, and `physical output verified` as
separate states. Keep unresolved DP output, low microphone level, PCIe link, and TF cold boot
explicit until tested on the intended CM5 board.
