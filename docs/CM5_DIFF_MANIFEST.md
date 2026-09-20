# CM5 差分清单

## 直接覆盖文件

- `kernel/arch/arm64/boot/dts/rockchip/rk3576-100ask-dshanpi-a1-linux.dts`
- `kernel/arch/arm64/boot/dts/rockchip/rk3576-100ask-dshanpi-a1-common.dtsi`
- `kernel/arch/arm64/boot/dts/rockchip/rk3576-100ask-a1-cm5-base-v1.dtsi`
- `kernel/arch/arm64/boot/dts/rockchip/rk3576-100ask-1024-768-mipi.dtsi`
- `kernel/arch/arm64/boot/dts/rockchip/rk3576-100ask-ov13850-3cam.dtsi`
- `external/camera_engine_rkaiq/rkaiq/iqfiles/isp39/ov13850_ZC-OV13850R2A-V1_Largan-50064B31.json`
- `device/rockchip/.chips/rk3576/rockchip_rk3576_dshanpi-a1_defconfig`
- `device/rockchip/common/post-hooks/89-aic8800-modules.sh`

## Patch

- `0001-kernel-cm5-config-and-integration.patch`：内核配置、AIC8800 集成。
- `0002-uboot-cm5-sdmmc-power.patch`：SDMMC0 PWREN、SPL GPIO 和上电延时。
- `0003-buildroot-cm5-tools.patch`：CM5 调试所需的块设备和 GPIO 工具。
- `9001-debug-only-regmap-rk628-v4l2.patch`：危险/高日志量调试项，仅在
  `--debug` 模式应用，发布镜像不应默认启用。

## 未包含内容

- `update.img`、`boot.img`、`rootfs.img` 等大体积构建产物；
- Wi-Fi SSID/密码、SSH 密码、固定板端 IP、开发机用户名和 token；
- 本地 `/tmp` 中的录音、抓图和串口日志；
- 与 CM5 最终硬件路径无关的 FocalTech/旧 GT9xx 实验性修改。

## 关键硬件不变量

- GPIO1_D0 必须属于 `SAI2_SDO_M0`，不能再被 `vcc_pcie_pwr` 占用。
- Type-C SBU1/SBU2 DC 控制为 GPIO4_C4/C5。
- GT911 位于 I2C9，IRQ=GPIO3_D0，RESET=GPIO0_C1。
- 板载 MIC2 接 LIN2/RIN2，软件应使用 Differential + Line2。
- PCIe1 与 USB3 Host1 共用 COMBPHY1；当前选择 PCIe1，Host1 只保留 USB2。
- SDMMC0 电源开关 U29 的 EN 为高有效 GPIO0_B6。
