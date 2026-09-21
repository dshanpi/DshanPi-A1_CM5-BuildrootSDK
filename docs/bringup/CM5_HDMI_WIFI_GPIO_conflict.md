# CM5 HDMI / Wi-Fi GPIO2_B0 冲突修复（2026-09-21）

实测 HDMI 接 DELL U2518D，输出 2560x1440@60Hz；
此时加载 AIC8800 Wi-Fi 驱动报 SDIO 发送超时 -110，wlan0 未创建。
板上 debugfs 显示 gpio-72 (GPIO2_B0) 被 enable 占用为 out hi，
同一引脚同时列在 sdmmc1m1-bus4 中。

共同设备树的 HDMI enable-gpios 使用 GPIO2_B0，而 CM5 Wi-Fi
SDIO M1 的 D2 正是 GPIO2_B0。HDMI 驱动的 encoder_enable 在 TMDS
模式调用 gpiod_direction_output(..., 1)，破坏 SDIO 数据线复用。
CM5 原理图将该脚接 Wi-Fi，HDMI 不使用这个 enable GPIO。

在 CM5 专用 rk3576-100ask-a1-cm5-base-v1.dtsi 中，
用 /delete-property/ enable-gpios 删除继承项。
原 A1 common 文件不改，Wi-Fi 四位总线和 HDMI HPD/DDC 保持原样。
差分包同步相同修复。PCIe1 overlay 也继承修复后的基础设备树。

本次提供默认 USB2 OTG1 Host 的完整镜像，PCIe1 保持禁用。
使用 build-cm5-mode.sh usb2 构建，并检查 FIT 提取 DTB 中 HDMI
enable-gpios 已不存在，SDIO M1 D2 配置仍保留。
先前两版镜像没有此修复，不应混用。

烧写后待验证：保持 HDMI 连接，加载 aic8800_bsp 和 aic8800_fdrv，
确认 wlan0 出现、扫描及联网正常，再测试同时显示和 Wi-Fi 传输。
编译校验不代表同时工作已经通过实板验收。

构建完成：`output/cm5-variants/usb2-20260921-073219-gtuNbU/`。
交付文件：`update-cm5-usb2-hdmi-wifi-fix.img`。
SHA256：`99355743f2b67a8bc1c7743186ad6c6c37654f8f420cde1304ad970c7c0c71ea`。
FIT 提取 DTB 比较通过，HDMI 属性列表已无 enable-gpios，
SDIO 四位总线仍含 GPIO2_B0、复用功能 2。

## 烧写后实板回归（2026-09-21）

用户烧写后通过 MCP 绑定串口、1500000 波特率测试：

- 运行设备树 `/proc/device-tree/hdmi@27da0000/enable-gpios` 已不存在。
- HDMI 保持连接，DRM Video Port0 ACTIVE，2560x1440p60；DSI 同时 ACTIVE。
- 依次 insmod `/lib/modules/aic8800_bsp.ko`、`aic8800_fdrv.ko`，
  `ip link set wlan0 up` 成功，扫描正常，未重现 SDIO -110 / set power on fail。
- 启动 wpa_supplicant 后发现镜像原配置是开放网络通配项，会自动连接其他开放热点。
  本次用 wpa_cli 将运行时 network 0 改为指定热点 / WPA-PSK，
  使用用户提供的密码（不在记录中保存），未执行 save_config。
- WPA2-PSK 认证 COMPLETED，DHCP 获得地址和网关，信号 -44 dBm。
- 强制 `ping -I wlan0 -c 20 -i 0.2 -W 2 <gateway>`：20/20，0% 丢包。
- 强制 `ping -I wlan0 -c 3 -W 2 223.5.5.5`：3/3，0% 丢包。
  显式绑定 wlan0，避免被同时连接的 eth0/eth1 掩盖。
- 测试后 DRM 仍为 HDMI-A-1 ACTIVE、2560x1440p60，相关 SDIO 超时日志检查无匹配。

结论：本镜像通过 HDMI 输出开启时 Wi-Fi 加载、扫描、认证、DHCP 和内外网 ICMP
收发的短时共存测试。不等同于长时间吞吐/热插拔稳定性验收，也未通过外部相机确认
屏幕实际画面。Wi-Fi 驱动加载及指定热点配置本次为运行时操作，尚未做开机自动连接。

后续整板测试文件往返校验通过，但 600 包窗口丢包 4.33%；
不能将本项 GPIO 冲突修复等同于无线稳定性全部通过，见全功能回归记录。
