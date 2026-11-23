# DshanPI A1 CM5 Buildroot SDK 差分包

本分支配合百问网发布的 `100ask-rk3576_SDK` 使用，适用于 DshanPI A1 CM5 底板、Buildroot 和 Linux 6.1.75。板级配置包含设备树、三路 OV13850 相机配置、YT8521 以太网 PHY 模块、板载无线初始化脚本，以及开机自启的产测程序和源码。

## 1. 获取板级配置

先将配套 SDK 解压到 `~/100ask-rk3576_SDK`。在 Ubuntu 编译主机打开终端，下载本分支。

```bash
cd ~
git clone --single-branch --branch cm5-board-20261123 https://github.com/dshanpi/DshanPi-A1_CM5-BuildrootSDK.git cm5-board
```

## 2. 应用配置

脚本会校验文件，并在 SDK 的 `.cm5-board-backup/` 下保存首次覆盖前的板级文件。

```bash
~/cm5-board/scripts/apply-cm5-diff.sh ~/100ask-rk3576_SDK
```

## 3. 编译固件

使用配套的 Ubuntu 编译环境。U-Boot 的构建脚本需要主机上有 `python2`，可先运行 `python2 --version` 确认。

进入 SDK，选择 DshanPI A1 CM5 配置。

```bash
cd ~/100ask-rk3576_SDK
./build.sh defconfig:rockchip_rk3576_dshanpi-a1_cm5_defconfig
```

运行完整编译。

```bash
./build.sh
```

打包整机镜像。

```bash
./build.sh updateimg
```

编译完成后，查看用于整机烧录的镜像。

```bash
ls -lhL output/firmware/update.img
```

## 4. 启动与产测

使用 MIPI 小屏时，先接 12 V 电源，待小屏显示后再接电脑 USB-C 数据线。系统进入桌面后会自动打开产测界面。产测报告保存在板上的 `/userdata/cm5-factory-reports/`。

## 恢复原有板级配置

运行以下命令可恢复首次应用前的板级文件，并移除脚本新增的文件。恢复范围以备份清单为准。随后重新选择原配置并编译，已有镜像需重新生成。

```bash
~/cm5-board/scripts/restore-cm5-diff.sh ~/100ask-rk3576_SDK
```

## 恢复整套 SDK 到发布基线

需要重新开始开发时，将发布资料中的 `100ask-rk3576_SDK-cm5-baseline-20261123.tar.gz` 和 [SHA-256 校验文件](docs/baselines/100ask-rk3576_SDK-cm5-baseline-20261123.tar.gz.sha256) 放在同一目录。基线包约 8.33 GiB，解压约需 15 GiB，已包含本版 DshanPI A1 CM5 板级配置、交叉工具链和 Buildroot 下载缓存。

在 Ubuntu 上运行以下命令。示例假设下载文件放在 `~/Downloads/`，恢复目标 `~/cm5-sdk-restored` 尚不存在。

```bash
~/cm5-board/scripts/restore-sdk-baseline.sh ~/Downloads/100ask-rk3576_SDK-cm5-baseline-20261123.tar.gz ~/cm5-sdk-restored
```

脚本会先核对 SHA-256，再解压到新目录，原工作目录中的修改会保留。进入 `~/cm5-sdk-restored` 后，按上文“编译固件”选择配置并编译。

差分文件按 SDK 路径存放在仓库根目录。各目录的用途见[差分清单](docs/CM5_DIFF_MANIFEST.md)。
