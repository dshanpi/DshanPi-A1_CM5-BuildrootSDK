# CM5 hardware map

Read the schematics in `docs/hardware/` when a net or polarity is disputed.

| Subsystem | Verified mapping |
|---|---|
| GT911 | I2C9 M1; IRQ GPIO3_D0 falling edge; RESET GPIO0_C1 active low |
| Type-C DP | SBU1_DC GPIO4_C4; SBU2_DC GPIO4_C5 |
| ES8388 playback | SAI2 M0; SDO GPIO1_D0; LOUT1/ROUT1 headset; LOUT2/ROUT2 speakers |
| ES8388 capture | MIC1P/N headset on LIN1/RIN1; MIC2P/N board mic on LIN2/RIN2 |
| Headset detect | GPIO4_B5, observed high while inserted |
| Headset MOSFET gate | GPIO4_B4 should not float; board uses RK628D_INT_H through 300 kOhm |
| PCIe0 | PERST# GPIO1_B7; verified Gen2 x1 |
| PCIe1 | COMBPHY1; reworked PERST# GPIO4_C7; R36 remains native CLKREQ#; verified Gen2 x1 |
| USB Host1 | USB2 remains available; USB3 shares COMBPHY1 with PCIe1 |
| TF power | U29 MT9700, active-high EN GPIO0_B6 (`SDMMC0_PWREN_H`) |
| OV13850 CAM0 | I2C2, CSI DCPHY0, PWDN GPIO3_C5, ISP vir0 |
| OV13850 CAM1 | I2C3, CSI DPHY1, PWDN GPIO3_C7, ISP vir1 |
| OV13850 CAM2 | I2C5, CSI DPHY4, PWDN GPIO3_C6, ISP vir3 |

The OV13850 connector pin described as reset on older carrier configurations is NC/GND on
the actual camera module. Keep reset GPIO optional and omit it from the three sensor nodes.

For the board microphone use both PGA muxes in Differential mode and select Differential
Line 2. A plain Line1 capture was all zero; single-ended Line2 produced switching
transients. Differential Line2 produced continuous but low-level data and still needs bias
and ALC validation.
