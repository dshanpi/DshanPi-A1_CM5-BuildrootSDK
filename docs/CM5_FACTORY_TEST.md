# CM5 产测集成

`cm5-factory-test` 分支基于 `cm5-integrated-20260921`，加入已在实板使用的产测程序和 TF 软件轮询配置。

## 本轮差异

| 路径 | 内容 |
| --- | --- |
| `app/cm5_factory_test/src/` | 14 项触摸界面、持续检测、自动抓图和录音、TF/NVMe 读取、JSON 报告 |
| `app/cm5_factory_test/board-support/cm5_yt8521.c` | YT8521 专用 PHY 驱动，按 RGMII 模式配置时钟延时 |
| `app/cm5_factory_test/S19cm5-phy` | 网络服务前加载 PHY 模块 |
| `app/cm5_factory_test/S98cm5-factory-test` | Weston 就绪后启动应用，保存独立报告 |
| `app/cm5_factory_test/Makefile`、`install-sdk.sh` | 交叉编译、安装 target 和持久 overlay，模块放入 `usr/lib/modules/` |
| `app/cm5_factory_test/tests/` | 后端、界面、开机服务及自动测试检查 |
| `kernel/arch/arm64/boot/dts/rockchip/rk3576-100ask-a1-cm5-base-v1.dtsi` | U29 卡槽电源绑定及延时，移除 CD pinctrl，使用 `broken-cd` 轮询 |
| `scripts/apply-cm5-diff.sh` | 将产测项目复制到目标 SDK 的 `app/cm5_factory_test/` |

GMAC0 配置已存在于基线的 `rk3576-100ask-dshanpi-a1-common.dtsi`。它采用 `rgmii-rxid`、SoC `tx_delay=0x1b`、GPIO0_C2 低有效复位和 MDIO 地址 0。本轮 PHY 模块使该配置生效，实板 ETH0 绑定 `YT8521 CM5 Ethernet` 后配置为 PHY RX 1900 ps、TX 0 ps。PHY 内部 TX 延时与 SoC 的 `tx_delay` 属于两个不同位置。

## 在完整 SDK 中构建

先从差分仓库运行应用脚本。

```bash
./scripts/apply-cm5-diff.sh /absolute/path/to/rk3576-sdk
```

然后进入完整 SDK 根目录。

```bash
./build.sh uboot
./build.sh kernel
./build.sh buildroot
make -C app/cm5_factory_test SDK_ROOT="$PWD" -j4
bash app/cm5_factory_test/install-sdk.sh "$PWD"
./build.sh buildroot-make:rootfs-ext2
./build.sh firmware
```

完整镜像由 SDK 写入 `output/firmware/update.img`。PHY 模块依赖当前 kernel 配置和符号表，内核更新后需重新构建、安装并打包。

## 当前验证范围

2026-11-12 的实板验证包含完整固件写入与设备校验、重启自启、14 项报告、PCIe1 隐藏、TF 卡自动只读 1 MiB 通过。测试音峰值为 1197，相对低音量版增加约 6 dB。

TF 采用 SDMMC0 软件轮询，Linux 运行期间持续尝试识别卡片。该配置不代表已经验证 TF 启动卡。此前样板的 TF 硬件故障已由用户处理，修复后原生 SDMMC0 卡槽可正常读取。

源代码仓库保留构建输入。整包镜像、编译生成的模块、板端报告和现场录音照片通过独立制品渠道分发。
