#!/usr/bin/env python3
"""Read-only sysfs/proc fixtures. No real board or privileged operations."""
import json
import pathlib
import subprocess
import sys
import tempfile

backend, app, video = sys.argv[1:4]
with tempfile.TemporaryDirectory(prefix="cm5-fixture-") as work:
    root = pathlib.Path(work)
    files = {
        "proc/asound/card2/id": "rockchipes8388\n",
        "sys/bus/usb/devices/2-1.1/idVendor": "1234\n",
        "sys/bus/usb/devices/2-1.1/idProduct": "5678\n",
        "sys/bus/usb/devices/2-1.1:1.0/bInterfaceClass": "08\n",
        "sys/bus/usb/devices/2-1.2/idVendor": "4321\n",
        "sys/bus/usb/devices/2-1.2/idProduct": "8765\n",
        "sys/bus/usb/devices/2-1.2/bDeviceClass": "00\n",
        "sys/class/udc/test-controller/state": "configured\n",
        "sys/kernel/config/usb_gadget/test/UDC": "test-controller\n",
        "sys/class/drm/card0-HDMI-A-1/status": "disconnected\n",
        "sys/class/drm/card0-HDMI-A-1/modes": "1024x768\n",
        "sys/bus/usb/devices/2-1.1/bDeviceClass": "00\n",
        "sys/bus/usb/devices/2-1.1/product": 'Test "USB"\n',
        "sys/bus/usb/devices/2-1.1/speed": "480\n",
        "sys/bus/usb/devices/2-1/idVendor": "1a86\n",
        "sys/bus/usb/devices/2-1/idProduct": "8091\n",
        "sys/bus/usb/devices/2-1/bDeviceClass": "09\n",
        "sys/block/mmcblk1/device/type": "SD\n",
        "sys/class/block/mmcblk1/size": "32768\n",
        "sys/class/net/eth0/carrier": "1\n",
        "sys/class/net/eth0/speed": "1000\n",
        "sys/class/net/eth0/fixture_ipv4": "\n",
        "sys/class/net/eth0/phydev/phy_id": "0x0000011a\n",
        "sys/class/net/eth1/carrier": "0\n",
        "sys/class/net/eth1/speed": "-1\n",
        "sys/class/net/eth1/fixture_ipv4": "\n",
        "sys/class/net/eth1/phydev/phy_id": "0x0000011a\n",
        "sys/class/video4linux/video11/name": "rkisp_mainpath\n",
        "sys/class/rtc/rtc0/name": "rk808-rtc\n",
        "sys/class/rtc/rtc1/name": "rtc-pcf8563 4-0051\n",
    }
    for path, value in files.items():
        target = root / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(value, encoding="utf-8")
    function = root / "sys/kernel/config/usb_gadget/test/functions/ffs.adb"
    function.mkdir(parents=True)
    linked = root / "sys/kernel/config/usb_gadget/test/configs/b.1/adb"
    linked.parent.mkdir(parents=True)
    linked.symlink_to(function)
    config = root / "test.conf"
    config.write_text("usb1_port=\nusb2_port=\ncsi1_video=/dev/video11\n", encoding="utf-8")
    report = root / "report.json"
    subprocess.run([backend, str(root), str(config), str(report)], check=True)
    data = json.loads(report.read_text(encoding="utf-8"))
    assert data["schema"] == 3
    assert all("auto_scope" in t and "automatic_verdict" in t for t in data["tests"])
    assert len(data["tests"]) == 14 and data["overall"] == "INCOMPLETE"
    assert all(item["id"] != "m2_1" for item in data["tests"])
    assert data["pass"] == 7 and data["skip"] == 0
    assert '"value"\n真实操作证据\\' in data["tests"][0]["evidence"]
    assert "\ufffd" in data["tests"][-1]["evidence"]
    subprocess.run([app, "--probe-root", str(root), "--config", str(config), "--scan", "--report", str(report)], check=True)
    data = json.loads(report.read_text(encoding="utf-8"))
    assert not {"headphone","headset_mic","dsi","user_led","fan","debug_uart","power","rtc"} & {t["id"] for t in data["tests"]}
    assert data["pass"] == 0 and data["overall"] == "INCOMPLETE"
    config.write_text("unknown_key=bad\n", encoding="utf-8")
    assert subprocess.run([app, "--config", str(config), "--scan"]).returncode == 2
    assert subprocess.run([app, "--auto", "--probe-root", str(root)]).returncode == 2
    assert not list(root.glob("*.tmp-*"))
subprocess.run([video], check=True)
print("all offline checks passed; physical hardware has not been exercised")
