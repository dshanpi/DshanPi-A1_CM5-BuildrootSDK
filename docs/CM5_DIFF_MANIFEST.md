# DshanPI A1 CM5 差分清单

本分支以百问网发布的 `100ask-rk3576_SDK`（Buildroot、Linux 6.1.75）为基线。应用脚本按下列路径将板级文件写入 SDK。

| 目录或文件 | 用途 |
| --- | --- |
| [app/cm5_factory_test](../app/cm5_factory_test/) | 产测程序源码、启动脚本、桌面资源及 YT8521 PHY 模块源码 |
| [device/rockchip/.chips/rk3576](../device/rockchip/.chips/rk3576/) | DshanPI A1 CM5 编译配置，选择设备树及根文件系统覆盖目录 |
| [overlays/cm5-board](../device/rockchip/.chips/rk3576/overlays/cm5-board/) | 以太网 PHY、板载无线初始化、三路 OV13850 的 3A 服务和 IQ 参数 |
| [overlays/cm5-factory-test](../device/rockchip/.chips/rk3576/overlays/cm5-factory-test/) | 编入镜像的产测程序、开机自启脚本和音频配置 |
| [kernel-6.1/arch/arm64/boot/dts/rockchip](../kernel-6.1/arch/arm64/boot/dts/rockchip/) | DshanPI A1 CM5 主设备树、底板配置及三摄拓扑 |

逐文件路径、SHA-256 和安装权限见 [manifest.json](../manifest.json)。预编译 PHY 模块用于配套内核 6.1.75；更换内核版本或配置后，请用相应内核重新编译该模块。

应用及还原命令见[仓库首页](../README.md)。
