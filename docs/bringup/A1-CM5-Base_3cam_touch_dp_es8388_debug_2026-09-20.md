# A1-CM5 底板三摄、触摸、USB-C DP 与 ES8388 联调记录

日期：2026-09-20

## 1. 硬件和参考资料

- 计算模块：DshanPi A1 / RK3576。
- 底板：`SCH_A1-CM5-Base_Schematic1_2026-09-18.pdf`。
- 模块原理图：`DshanPi-A1-RK3576-SCH_V1.1.pdf`。
- 摄像头模组：DshanPI CAM-OV13850-V1，22Pin 接口只有 PWDN，没有 RESET；Pin 4 为 GND，Pin 5 为 NC，Pin 6 为 PWDN。
- 参考内核：`dshanpi/dshanpi-a1-linux-kernel` 的 `rk-6.1` 分支。

参考仓库用于确认 RK3576 的控制器、端点和驱动写法，GPIO 最终以两份原理图和实板探测为准。参考仓库的 `rk3576-100ask` 配置仍包含从 EVB 继承的错误：USB-DP SBU DC GPIO 写为 GPIO2_A6/A7，Type-C VBUS/IRQ 的节点属性与 pinctrl 定义也不一致。

## 2. 三路 OV13850 调试过程

### 2.1 拓扑与数据格式

| 摄像头 | 传感器子设备 | ISP 输出 | 接口 |
|---|---|---|---|
| CAM0 | `/dev/v4l-subdev2` | `/dev/video33` | CSI DCPHY0，2 Lane |
| CAM2 | `/dev/v4l-subdev7` | `/dev/video42` | CSI DPHY1，2 Lane |
| CAM3 | `/dev/v4l-subdev12` | `/dev/video51` | CSI DPHY4，2 Lane |

调试时先通过 OV13850 内部测试图验证三路 RAW10 彩条顺序一致，确认 Bayer 顺序为 BGGR；随后验证 NV12 U/V 顺序，排除了 Bayer 和色度平面解释错误。

### 2.2 ISP39 调整记录

1. 以 Rockchip ISP39 模板为框架，仅移植 OV13850 的传感器时序、增益信息和模块标识，避免把 ISP21 JSON 直接交给 ISP39 解析器。
2. 将四个 Bayer 通道的 BLC 统一为 256，消除高增益下两个绿色通道黑电平不一致造成的暗部偏绿。
3. 从旧 IQ 移植 AWB 光源锚点、CCM 和 LSC 数据；实拍确认旧自动 CCM 与旧镜头 LSC 会产生明显绿色覆盖，因此停用旧自动 CCM 和 LSC。
4. AWB 保持自动，首帧种子为 R/Gr/Gb/B=`4.0/1.0/1.0/1.7`；白天稳定值约为 `2.15/1.0/1.0/1.43`。
5. 使用手动 CCM：

   ```text
   0.75  0.25  0.00
   0.15  0.70  0.15
   0.00  0.25  0.75
   ```

6. DRC 使用 tone curve scale 16、K=0.2、最大 tone gain=2.0，提升室内暗部。
7. 三路同时启动时 CAM2/CAM3 的自动曝光曾停在低增益，最终暂用统一手动曝光：40 ms、模拟增益 9.5x、ISP 增益 1.0x。
8. 测试过 GStreamer 软件 gamma/videobalance，出现灰阶断层和近似反相的画面，因此撤销，所有亮度和颜色调整保留在 ISP IQ 中。

最终 IQ：

```text
external/camera_engine_rkaiq/rkaiq/iqfiles/isp39/ov13850_ZC-OV13850R2A-V1_Largan-50064B31.json
SHA-256: 621351159a62df7823f292319d3b4d728347028880ea1b4771ebd0321c625430
```

三路曾同时完成 300 帧采集，返回值均为 0，约 24.99 fps，未出现 kernel Oops、BUG 或 ISP/CSI 错误。当前限制是 LSC 尚未按三颗实际镜头做平场标定，固定曝光也需要后续改成可靠的分路自动曝光。

## 3. 本次设备树修改

### 3.1 OV13850

- 从 CAM0 删除 `reset-gpios = <&gpio4 RK_PC6 ...>`。
- 保留三路真实存在的 PWDN、MCLK、电源和 CSI 端点。
- 驱动将 reset GPIO 当作可缺省资源；CAM2/CAM3 原本就没有 reset 属性并已正常工作。

### 3.2 GT911 触摸

- 旧显示屏 include 中的 I2C4/GPIO4_C6 GT911 节点保持禁用。
- 按 CM5 底板原理图新增 I2C9 `touchscreen@5d`：
  - IRQ：GPIO3_D0，下降沿；
  - RESET：GPIO0_C1，低有效；
  - I2C：I2C9_M1，100 kHz。
- 修改前实板 `i2cdetect -y 9` 已在 `0x5d` 检测到触摸芯片，内核已启用 `CONFIG_TOUCHSCREEN_GOODIX=y`。

### 3.3 USB-C / DisplayPort

- 两份原理图均把 `TYPEC0_SBU1_DC/SBU2_DC` 接到 GPIO4_C4/C5。
- 将 `usbdp_phy` 的 `sbu1-dc-gpios/sbu2-dc-gpios` 从 GPIO2_A6/A7 修正为 GPIO4_C4/C5。
- 保留本地已正确配置的 HUSB311 IRQ=GPIO1_C0、Type-C VBUS enable=GPIO1_C1、USB role switch 和 DP Alt Mode 端点图。

### 3.4 ES8388

- I2C4 地址 `0x11`、SAI2、12.288 MHz MCLK 不变。
- LOUT1/ROUT1 接耳机，LOUT2/ROUT2 接两路板载功放。
- MIC1P/N 配置为 Headset Mic，MIC2P/N 配置为板载 Main Mic。
- 耳机检测从旧主板 GPIO0_A2 改为 CM5 底板的 `RK628D_PLUGIN_DET`，即 GPIO4_B5。实板插入耳机时该脚为高电平，因此最终改为高有效。

### 3.5 PCIe1 / USB3 Host1

- RK3576 的 PCIe1 与 USB3 Host1 共用 `COMBPHY1`，不能同时工作在两种模式。
- 按 CM5 底板第二个 M.2 插槽的接线启用 `pcie1`，PHY 使用 `combphy1_psu PHY_TYPE_PCIE`，链路上限设为 Gen2。
- M.2 `PERST#` 由 `RK628D_RESET_H`（GPIO4_A5）控制，设备树使用 `GPIO_ACTIVE_HIGH`。Rockchip PCIe 驱动以逻辑 0 拉低 PERST#、逻辑 1 拉高释放，正好符合低有效的 PCIe 复位信号。
- GPIO4_A5 同时也是 RK3576 的 `pcie1_clkreq_m2` 复用脚，无法既做 PERST# 又做 CLKREQ#。当前优先保证必需的 PERST#，不选择 `pcie1m2_pins`；驱动会进入不支持 CLKREQ 的模式并禁用 L1SS，基本枚举和链路训练不依赖 CLKREQ#。
- USB Host1 的 DWC3 节点删除 `combphy1_psu` USB3 PHY，只保留独立的 `u2phy1_otg`，并设置 `maximum-speed = "high-speed"`。因此该口仍可用 USB2，但不再占用 USB3/PCIe 共用高速 PHY。

## 4. 修改文件

- `kernel-6.1/arch/arm64/boot/dts/rockchip/rk3576-100ask-dshanpi-a1-linux.dts`
- `kernel-6.1/arch/arm64/boot/dts/rockchip/rk3576-100ask-a1-cm5-base-v1.dtsi`
- `kernel-6.1/arch/arm64/boot/dts/rockchip/rk3576-100ask-ov13850-3cam.dtsi`

最终 DTB 必须确认：I2C4 的旧 GT911 为 disabled；I2C9 的新 GT911 为 okay；三路 OV13850 均无 reset-gpios；SBU DC 为 GPIO4_C4/C5；ES8388 路由和 GPIO4_B5 耳机检测生效。

## 5. 构建与实机测试结果

### 5.1 构建结果

- Kernel/DTB/FIT 编译成功。
- DTB SHA-256：`05184aad0622cad5375e2c316b7b7778f8c2ce014803ced7f593dc66d1478452`。
- 强制清理并重建 `camera-engine-rkaiq`，随后重新生成 Buildroot rootfs。
- 从最终 `rootfs.img` 中反向提取 OV13850 IQ，SHA-256 仍为 `621351159a62df7823f292319d3b4d728347028880ea1b4771ebd0321c625430`。
- 最终完整镜像：`output/update/Image/update.img`，大小 993798787 字节。
- 最终镜像 SHA-256：`ec5c30b68b7e79666621d44368a58d926db8301beee87c12892f678fa6a8fc50`。
- 烧录前回滚镜像：`output/backup/update-pre-cm5-touch-dp-20260920.img`，SHA-256 `1220047849cea85e88abf3d15499ade1075cfd377dfa288d45cfbb49968fed24`。

DTB 展开检查确认：

- I2C4 旧 `gt911@5d` 为 `disabled`；
- I2C9 新 `touchscreen@5d` 为 `okay`，IRQ=GPIO3_D0、RESET=GPIO0_C1；
- `usbdp_phy` 的 SBU DC 为 GPIO4_C4/C5；
- 三颗 OV13850 节点均无 `reset-gpios`；
- ES8388 使用 GPIO4_B5 耳机检测以及 MIC1=耳机麦克风、MIC2=板载麦克风路由。

### 5.2 实机结果

等待板子进入 Rockchip Loader/Maskrom 模式后，使用 `lynx-a1-cm5-sdk` 的 `openrockcli` 完成全擦除、校验烧录和实机测试。

### 5.3 ES8388 无声排查（当前板上旧镜像）

- DP 拔除后，PulseAudio 仍同时创建 ES8388、DP、HDMI sink；已明确指定 `alsa_output.0.HiFi__hw_rockchipes8388__sink`，排除音频误送到 DP。
- 使用 GStreamer 向 ES8388 sink 连续输出 1 kHz、48 kHz、双声道测试音，ALSA PCM 状态为 `RUNNING`，SAI2 MCLK 为 12.288 MHz。
- DAPM 的 Headphone、Speaker、OUT1、OUT2 路径均为 On；codec 寄存器 `DACPOWER=0x3c`，左右 DAC 以及 OUT1/OUT2 均已上电，播放混音开关已开，数字静音未使能。
- 原理图对比：CM5 底板的 LOUT1/ROUT1 接耳机，LOUT2/ROUT2 分别接两颗 AW8010；参考 A1 主板只用 ROUT2 驱动一颗 AW8010。CM5 底板两颗 AW8010 的 `SHUTDOWN#` 均通过 10 kΩ 上拉到 3.3 V，不需要软件功放使能 GPIO。
- 若持续测试音仍然耳机和喇叭同时完全无声，软件数字链路已基本排除。先量 ES8388 的 HPVDD(16)、AVDD(17)、PVDD(3) 和 DVDD(2)，尤其是只给数字电源而模拟/耳放电源缺失时，I2C 和寄存器会完全正常但四路模拟输出均无声；再量 LOUT1(12)、ROUT1(11)、LOUT2(15)、ROUT2(14) 是否有 1 kHz 波形。AW8010 还要确认 PVDD 为 5 V、SHUTDOWN# 约 3.3 V。喇叭输出为 BTL，必须跨接 SPK+ 与 SPK-，任一端都不能接地。
- CM5 底板耳机输出相比参考 A1 多了 Q29/Q30（WNM2016）及 470 Ω 网络。如果 codec 的 LOUT1/ROUT1 有波形而耳机座无波形，重点检查 Q29/Q30 的型号、方向、焊接和导通状态。
- 进一步逐网确认：Q29/Q30 是串联在两个耳机声道中的 NMOS，两颗 Gate 共接，经 R397 300 kΩ 连接 `RK628D_INT_H`，并由 C744 100 nF 接地。该信号在参考 A1 上是 RK628D 中断而不是音频使能，这是明确的设计风险。设备树现将 GPIO4_B4 配成输入上拉，避免 RK628D 未拉低时 Gate 漂浮；若 RK628D 将 INT 持续拉低，仍需硬件把 Gate 改接稳定 3.3 V 上拉或专用静音控制。
- U111 的 DVDD/PVDD 由 `VCCIO_CODEC` 供电，HPVDD/AVDD 由 `VCCA3V3_CODEC` 供电。后者经 U112 磁珠从 `VCC_3V3` 引入，是“耳机和喇叭四路同时无模拟输出、但 I2C 正常”的第一测量点。

### 5.4 本次 PCIe1 镜像

- 最终 DTB 已反编译确认：`pcie@2a210000` 为 okay、PHY 指向 `combphy1_psu` 的 PCIe 模式；`usb@23400000` 只剩 USB2 PHY 且限制为 high-speed；GPIO4_A5 为 PCIe1 PERST#。
- Kernel/DTB/FIT 重新编译成功。
- `boot.img` SHA-256：`bdf31ce9372abd26ae88933337d2a9c20d798e4dcf79458404a329ae5a9b7b03`。
- `update.img` SHA-256：`469ff4b1ec9004251367111af9e62a80fe9f1882cd3f51b15a5c0370ceb1e83a`。

### 5.5 TF 卡 SPL 启动失败修复

故障日志在 BootROM 已加载 DDR 初始化程序和 SPL 后，由 SPL 第二次初始化 MMC 时出现：

```text
Trying to boot from MMC2
Card did not respond to voltage select!
mmc_init: -95
```

这说明 TF 卡和前部启动扇区可被 BootROM 读取，失败点是 SPL 接管后的供电/引脚初始化。CM5 底板第 1 页显示：`VCC_3V3` 经 U29（MT9700）生成 `VCC3V3_SD`，U29 EN 是高有效的 `SDMMC0_PWREN_H`。当前 U-Boot DTS 曾把 `sdmmc0_pwren` 从 pinctrl 中注释掉，SPL 无法可靠维持卡电源。

第一次处理：

- U-Boot SPL 的 SDMMC0 pinctrl 恢复 `sdmmc0_pwren`；
- 保留 CLK、CMD、DAT0～DAT3；
- 不依赖 card-detect，避免不同卡座的机械检测触点/极性影响早期启动；
- SPL DTB 反编译确认 `mmc@2a310000` 的 pinctrl 已引用 `sdmmc0-pwren`（GPIO0_B6，复用 1）；
- `CONFIG_MMC_DW_PWREN_VALUE=1`，与 U29 高有效 EN 一致。

实机复测表明，上述修改以及将 GPIO0_B6 强制拉高、增加 20 ms
上电稳定时间后，源码构建的 SPL 仍在 CMD55/CMD1 阶段超时。与此同时，
从 eMMC 启动 Linux 后同一张卡可稳定枚举为 `mmcblk1`，卡电源与
`VCCIO_SD_S0` 均为 3.3 V，因此排除了卡座、TF 卡和基本布线故障。

最终处理是关闭板级配置中的 `RK_UBOOT_SPL`，不再用 `--spl-new` 替换
Rockchip MiniLoader，恢复 rkbin 的 `rk3576_spl_v1.08.bin`。该版本发布
说明明确包含“添加 sdmmc pinctrl 支持”，同时保留源码编译的完整 U-Boot、
内核和 rootfs。后续执行 `./build.sh uboot` 也会继续采用该 MiniLoader。

包含该修复、耳机 Gate 上拉和 PCIe1 模式的镜像 SHA-256：
`c526b661c4a3ab831996ac6fbc51725cbdbf100eff0fa6e36c993a3a052dc01c`。

## 6. 当前合并版本与验证状态

### 6.1 ES8388 播放无声的最终根因

- `GPIO1_D0` 在 CM5 接口上是 `SAI2_SDO_M0`，即 RK3576 向 ES8388
  发送 PCM 数据的线路。
- 公共 A1 设备树又把同一引脚分配给始终开启的固定电源
  `vcc_pcie_pwr`，导致该脚被 GPIO 输出高电平占用；MCLK、BCLK、LRCLK
  和 ALSA/DAPM 状态均可正常，但 ES8388 收不到有效播放数据。
- CM5 底板的 PCIe 3.3 V 为直接供电，不存在由 GPIO1_D0 控制的 PCIe
  电源开关。因此在 CM5 覆盖设备树中禁用 `vcc_pcie_pwr`，释放该引脚给
  `sai2m0-sdo`。
- 新系统实机确认 pin 56 为 `2a620000.sai` 的 `sai2m0-sdo`，不再显示
  `vcc-pcie-pwr` GPIO 占用；使用 `JJ.wav` 测试时耳机和两路喇叭均已出声。

### 6.2 麦克风当前状态

- 原理图确认：耳机麦克风为 `MIC1P/MIC1N -> LIN1/RIN1`；板载模拟麦克风
  为 `MIC2P/MIC2N -> LIN2/RIN2`。
- 板载 MIC2 应使用 ES8388 的差分输入：左右 PGA 选择 `Differential`，
  Differential Mux 选择 `Line 2`。普通 Line1 录音全零，普通 Line2
  单端录音只有切换瞬态。
- 差分 Line2 已能录到连续模拟数据，但当前电平约为 -56 dBFS，直接回放
  基本听不到。下一步需继续验证 MIC2 偏置电压并测试 ES8388 ALC；尚不能
  标记为录音通过。上述 amixer 设置目前只在板端临时执行，未固化进镜像。

### 6.3 当前功能状态

| 功能 | 当前状态 |
|---|---|
| 三路 OV13850 枚举、采集和并行预览 | 已通过；ISP 参数仍是阶段性调校，LSC/分路 AE 可继续优化 |
| 1024x768 MIPI DSI 显示 | 已出图 |
| GT911 触摸（I2C9） | 已通过 |
| ES8388 耳机与双喇叭播放 | 已通过 |
| ES8388 板载麦克风录音 | 差分路径已确认，音量过低，继续调试 |
| USB-C DP | 显示器有反应但尚未稳定出图 |
| PCIe1（COMBPHY1） | 设备树已切换；等待 R30/R31 焊接完成后枚举测试 |
| USB Host1 | 设备树保留 USB2，USB3 让给 PCIe1 |
| TF 卡 SPL 启动 | 已切换 Rockchip MiniLoader 方案，仍需最终冷启动复测 |
| AIC8800 Wi-Fi | 模块、固件可用；已连接 `Programmers`，当前仍采用手动加载/联网流程 |

### 6.4 当前交付镜像

- Linux 路径：`output/firmware/update.img`（符号链接到
  `output/update/Image/update.img`）。
- Windows MCP 路径由 Lynx 工作台按本机用户目录管理，例如：
  `%LOCALAPPDATA%\com.dshanpi.lynx\firmware\<project>\update.img`。
- 大小：993833603 字节。
- SHA-256：`b4dc6f136fa953800fd92957e9ade18dd2f75da17032e99e03c5fa5c9f87686c`。
- 该镜像包含 GPIO1_D0/SAI2_SDO 音频修复；第 5 节所列哈希均为此前
  各阶段镜像，仅用于追溯。
