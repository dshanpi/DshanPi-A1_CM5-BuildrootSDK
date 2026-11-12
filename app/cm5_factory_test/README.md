# CM5 底板产测工具

适用于 RK3576 CM5 Base V1.2、Buildroot、Weston 和 1024×768 触摸屏。启动应用后自动检测，支持检测过程中接入网线、HDMI 和 USB。通过项显示绿框，本轮结果保留到操作员重新测试。

当前界面有 14 项。PCIe1、RTC、独立按键、耳机、LED、风扇和串口测试已隐藏。

| 项目 | 判定方式 |
| --- | --- |
| 双扬声器 | 左右声道播放，操作员听音后确认 |
| 板载麦克风 | 正常录满 5 秒，检查 PCM WAV 格式和数据长度 |
| 网口 A、B | 对应物理接口 Link UP |
| USB-A 上、下口 | 外设枚举完成并识别设备类型 |
| Type-C USB | 主机侧外设枚举或设备侧 USB configured |
| TF 卡 | 自动查找 SD 类型块设备，只读读取 1 MiB |
| M.2 / PCIe0 | 匹配对应控制器下的 NVMe，驱动就绪且只读读取 1 MiB |
| HDMI 输出 | DRM connector 显示 connected |
| 屏幕 / 触摸 | 触摸屏幕一次 |
| CSI / FPC4、FPC7、FPC6 | 丢弃 3 帧，检查随后 5 帧并保存有效图像 |

扬声器测试音峰值为 1197，约为 16 位满幅的 3.65%。相对峰值 600 的版本增加约 6 dB。测试期间 PCM 控件设为 170，两个输出控件设为 26，结束后恢复进入测试前的 mixer 设置。

## 编译和安装

项目放在完整 SDK 的 `app/cm5_factory_test/`。先完成 SDK kernel 和 Buildroot 构建，再在 SDK 根目录执行。

```bash
make -C app/cm5_factory_test SDK_ROOT="$PWD" -j4
bash app/cm5_factory_test/install-sdk.sh "$PWD"
./build.sh buildroot-make:rootfs-ext2
./build.sh firmware
```

安装脚本同步更新 Buildroot target 与芯片持久 overlay。内核模块安装到 `usr/lib/modules/`，保留 Buildroot 的 `/lib -> usr/lib` 布局。S19 服务在联网前加载 YT8521 驱动，S98 服务等待 Weston 后启动产测应用。

板上服务和报告位置如下。

```sh
/etc/init.d/S98cm5-factory-test status
/etc/init.d/S98cm5-factory-test restart
```

报告保存于 `/userdata/cm5-factory-reports/`，运行日志位于 `/var/log/cm5-factory-test.log`。设备时间尚未校准时，报告文件名可能包含 1970 年，启动 ID 用于区分轮次。

## 开发验证

在已有 SDK 工具链的 Ubuntu 环境中执行。

```bash
bash tests/verify.sh
```

测试覆盖设备识别、热插入、结果保留、界面点击和导航、自动测试生命周期、开机服务以及采集文件检查。

2026-11-12 已在实板整包烧录后确认应用自启、14 项报告、PCIe1 隐藏及 TF 自动读取通过。喇叭音量仍需操作员根据现场听感确认。
