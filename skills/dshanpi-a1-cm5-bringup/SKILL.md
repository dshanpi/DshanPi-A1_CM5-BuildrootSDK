---
name: dshanpi-a1-cm5-bringup
description: Diagnose, configure, build, and validate the DshanPi A1 RK3576 CM5 carrier board, including OV13850 cameras, MIPI/GT911 display, USB-C DP, ES8388 audio, PCIe/USB PHY sharing, TF boot, and AIC8800 networking. Use for this carrier board only, not generic RK3576 boards.
---

# DshanPi A1 CM5 bring-up

Work from the RK3576 Linux 6.1 SDK root and preserve unrelated local changes. Treat the
schematic as authoritative for GPIO ownership; use reference DTS files only for controller
and endpoint structure.

Before changing a subsystem, read the matching section in
[`references/hardware-map.md`](references/hardware-map.md). For build, flashing, network,
and validation commands, read
[`references/validation-workflow.md`](references/validation-workflow.md).

Keep these invariants:

- GPIO1_D0 is `SAI2_SDO_M0`; never assign it to a PCIe regulator.
- GT911 is on I2C9 with GPIO3_D0 IRQ and GPIO0_C1 reset.
- Type-C SBU DC controls are GPIO4_C4/C5.
- PCIe1 and USB3 Host1 cannot both own COMBPHY1.
- CAM modules expose PWDN but no usable reset signal.
- Do not commit Wi-Fi, login, token, host path, serial-port, or fixed-IP secrets.

Validate one layer at a time: DTB content, kernel enumeration, data path, then user-visible
output. Do not infer success from an ALSA/V4L2 process merely being `RUNNING`; verify the
physical signal or captured content. Record exact image hashes and distinguish temporary
runtime controls from changes actually included in the image.
