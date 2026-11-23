# DshanPI A1 CM5 板级配置

本分支配合百问网发布的 `100ask-rk3576_SDK` 使用，适用于 DshanPI A1 CM5 底板、Buildroot 和 Linux 6.1.75。板级配置包含设备树、三路 OV13850 相机配置、YT8521 以太网 PHY 模块、板载无线初始化脚本，以及开机自启的产测程序和源码。

## 1. 获取板级配置

先将配套 SDK 解压到 `~/100ask-rk3576_SDK`。在 Ubuntu 编译主机打开终端，下载本分支。

```bash
cd ~
git clone --single-branch --branch codex/cm5-board-20261123 https://github.com/dshanpi/DshanPi-A1_CM5-BuildrootSDK.git cm5-board
```

## 2. 应用配置

脚本会校验文件，并在 SDK 的 `.cm5-board-backup/` 下保存首次覆盖前的板级文件。

```bash
python3 ~/cm5-board/scripts/board-config.py apply ~/100ask-rk3576_SDK
```

## 3. 编译固件

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
python3 ~/cm5-board/scripts/board-config.py restore ~/100ask-rk3576_SDK
```

`overlay/` 内的路径对应 SDK 根目录。`manifest.json` 记录每个发布文件的 SHA-256 和安装权限。产测程序使用配套 SDK 的运行库，预编译 PHY 模块对应内核 6.1.75。更换内核配置或版本时，请使用配套内核重新编译该模块。
