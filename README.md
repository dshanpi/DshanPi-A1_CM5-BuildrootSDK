# DshanPi A1 CM5 Buildroot SDK 差分包

本仓库保存相对于 Rockchip RK3576 Linux 6.1 Buildroot SDK 的 DshanPi A1
CM5 底板适配，不包含完整 SDK 和约 1 GB 的 `update.img`。

当前分支 `cm5-bringup-20260920` 汇总了：

- 三路 OV13850、ISP39 IQ 和三路并行采集拓扑；
- 1024×768 MIPI DSI 屏与 I2C9 GT911 触摸；
- USB-C DisplayPort SBU GPIO 修正；
- ES8388 路由、耳机检测和 `SAI2_SDO` 引脚冲突修复；
- PCIe0/PCIe1 双控制器配置，以及 PCIe1/USB3 Host1 共用 COMBPHY1 的模式切换；
- TF 卡 SPL/SDMMC0 供电修复及 Rockchip MiniLoader 构建选择；
- DDR/SPL、U-Boot 和 Linux 调试串口统一为 1,500,000 baud；
- AIC8800 Wi-Fi/蓝牙驱动、固件和 Buildroot 集成；
- 原理图、位号图、开发记录以及可复用的 Codex bring-up skill。

## 基线

- SDK：`rk3576_linux6.1_release_v1.0.0_20240620`
- Kernel：Linux 6.1.75
- 主 DTS：`rk3576-100ask-dshanpi-a1-linux.dts`
- 板级构建配置：`rockchip_rk3576_dshanpi-a1_defconfig`

## 应用方法

在完整 RK3576 SDK 之外克隆本仓库，然后运行：

```bash
./scripts/apply-cm5-diff.sh /absolute/path/to/rk3576_linux6.1_release_v1.0.0_20240620
```

调试专用的 regmap/RK628/V4L2 日志修改默认不会应用。如确实需要：

```bash
./scripts/apply-cm5-diff.sh /absolute/path/to/sdk --debug
```

随后在 SDK 根目录构建：

```bash
./build.sh uboot
./build.sh kernel
./build.sh buildroot
./build.sh updateimg
```

应用前请保持目标 SDK 工作区干净，脚本遇到无法安全应用的 patch 会停止，不会
强行覆盖冲突。详细硬件映射、调试过程和当前验证状态见
[`docs/bringup`](docs/bringup/A1-CM5-Base_3cam_touch_dp_es8388_debug_2026-09-20.md)。

## 当前状态

最新整板结果见 [2026-09-21 全功能回归](docs/bringup/CM5_full_regression_20260921.md)：
当前镜像三摄能并行出帧，但画面偏暗、AIQ 统计异常；双网口、Wi-Fi 和录音仍有待解决问题。
下表为历史 bring-up 进展，不是当前镜像的全功能验收结论。

2026-09-21：默认改用 USB2 OTG1 Host，PCIe1 改为单独 overlay；
两种模式通过构建时选择，详见 [构建及切换说明](docs/bringup/CM5_USB2_PCIE1_overlay.md)。
下表双 PCIe 为此前实测结果，不代表与 OTG1 Hub 同时可用。

| 子系统 | 状态 |
|---|---|
| 三路 OV13850 枚举/采集/预览 | 已验证，ISP 仍可继续做 LSC 和分路 AE 标定 |
| MIPI DSI 显示、GT911 触摸 | 已验证 |
| ES8388 耳机、双喇叭播放 | 已验证 |
| ES8388 板载麦克风 | 差分路径已确认，录音电平仍低，继续调试 |
| USB-C DP | 已识别显示器并完成 4 Lane HBR2 链路训练；当前继续排查屏端无画面/刷新问题 |
| PCIe0/PCIe1 | 两路均已以 Gen2 x1 Link Up，并各枚举一张 Axera 1f4b:0650 |
| TF 卡启动 | 已切换 vendor MiniLoader，仍需最终冷启动复测 |
| AIC8800 Wi-Fi | 模块和固件已验证；联网服务尚未固化 |

不要把开发机账号、Wi-Fi 密码、板端密码、私钥、token、串口设备名或固定 IP
提交到本仓库。镜像通过 Release、制品服务器或 Lynx 工作台单独分发。
