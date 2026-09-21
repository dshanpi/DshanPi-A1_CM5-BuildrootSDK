# CM5 USB2 OTG1 / PCIe1 启动配置

默认沿用主 DTS rk3576-100ask-dshanpi-a1-linux：PCIe1 关闭，
USB2_OTG1_DP/DM（底板网络 USB2_HOST1_DP/DM）作为 Host 连接 Hub。
DWC3 引用 U2PHY1 和 COMBPHY1，速度限制为 high-speed。
这是本板根据原理图约束和用户开关 PCIe1 对照现象采用的互斥配置；
不再声称独立 D+/D− 引脚可保证并行使用。

PCIe1 使用独立 /plugin/ overlay：
rk3576-100ask-a1-cm5-pcie1-overlay.dts。
overlay 关闭 usb_drd1_dwc3、u2phy1_otg、u2phy1，
开启 PCIe1、COMBPHY1，PERST#=GPIO4_C7，CLKREQ#=pcie1m2，
Gen2 上限。GPIO_ACTIVE_HIGH 沿用本版 Rockchip PCIe 驱动约定。
PCIe0 和 OTG0/DP 不变。

## 构建

在已完成完整 SDK 构建的根目录执行（需要主机 fdtget）：

```sh
# 默认 USB2 Host
bash device/rockchip/common/scripts/build-cm5-mode.sh
# 显式选用 PCIe1
bash device/rockchip/common/scripts/build-cm5-mode.sh pcie1
```

脚本保留现有 loader、U-Boot、根文件系统和 recovery，增量编译内核，
使用当前 SDK 打包工具生成完整 update 镜像。产物保存在
output/cm5-variants/<mode>-<timestamp>-<suffix>/，
文件名 update-cm5-usb2.img 或 update-cm5-pcie1.img，附 SHA256SUMS。
恢复分区保留现有镜像，不随主系统 overlay 切换。
此脚本不支持签名安全启动，也不要与其他 SDK 编译/打包命令并行运行。

只编译组合设备树时，使用 SDK 相同交叉编译器：
```sh
make -C kernel ARCH=arm64 CROSS_COMPILE=/path/to/aarch64-none-linux-gnu- \
  rockchip/rk3576-100ask-a1-cm5-pcie1.dtb
```

Kbuild 自动以基础 DTB 和 DTBO 运行 fdtoverlay。基础 DTB 带符号，
PCIe1 组合 DTB 被打入 FIT，同时 resource.img 使用相同设备树。
不需要也不支持 Linux 运行时卸载 USB 后热切换；切换需要使用对应
启动镜像并重启。默认 SDK defconfig 不修改。PCIe1 打包结束会恢复
默认 kernel/boot.img 和 resource.img；待烧录镜像以独立产物目录为准。

## 验收

- 默认版：PCIe0 正常，PCIe1 禁用，Hub 枚举，USB 高速设备传输及各口热插拔正常。
- PCIe1 版：两路 PCIe 枚举，OTG1 Host 不注册，OTG0 保持原配置。
- 每版三次冷启动及三次热重启，无 xHCI 命令超时、HC died 或 PCIe 掉线。
- 复测串口 1.5M、触摸、音频及摄像头。

编译和离线 DTB 校验不能代替上述实板验证；新镜像未烧写前不标记硬件验收通过。

## 2026-09-21 构建结果

两个模式均已完成 kernel/FIT/update 打包。已从两个 FIT 提取 FDT，
与各自目标 DTB 逐字节比较通过；状态检查确认模式互斥、PCIe0 保持开启，
PCIe1 reset-gpios 解码为 GPIO4_C7、GPIO_ACTIVE_HIGH。
USB 默认 DTB 的 COMBPHY1 PHY 类型为 USB3 (4)，PCIe1 引用类型为 PCIe (2)。
overlay 合并成功，脚本语法及补丁反向应用检查通过。
构建仍报告已有 sdio-pwrseq 时钟引用警告，本次未修改该项。

完整镜像 SHA256：

- USB2：`4a52d54f1bfd8ee916f4bf91b4fb60a254c3853430cebc567324011c8d409382`
- PCIe1：`17c3176794d77486ed30b512ff1b9eee41e5bcc4b4d3e0588fd44c940486b050`

SDK 产物目录：

- `output/cm5-variants/usb2-20260921-071534-EQI8OE/`
- `output/cm5-variants/pcie1-20260921-071356-mTQOHj/`
- 修改前镜像及设备树备份：`output/backup/cm5-before-usb-pcie-overlay-OsFxy7/`

这两版尚未烧写，冷启动、热重启、USB 热插拔及整板回归待实测。
