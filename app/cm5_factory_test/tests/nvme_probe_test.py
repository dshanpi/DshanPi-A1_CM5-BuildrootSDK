"""Realistic sysfs symlinks; never opens a disk or touches host hardware."""
import pathlib
import subprocess
import sys
import tempfile

binary = sys.argv[1]
with tempfile.TemporaryDirectory(prefix="cm5-nvme-") as work:
    root = pathlib.Path(work)

    def put(path, value):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(value)

    def link(path, target):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.symlink_to(target)

    def device(slot, number, ns=1):
        endpoint = root / f"sys/devices/platform/2a2{slot}0000.pcie/pci000{slot}:00/000{slot}:01:00.0"
        nvme = endpoint / f"nvme/nvme{number}"
        block = nvme / f"nvme{number}n{ns}"
        for key, value in {"class": "0x010802", "current_link_width": "1", "current_link_speed": "5.0 GT/s PCIe"}.items():
            put(endpoint / key, value)
        for key, value in {"transport": "pcie", "state": "live", "model": f"SSD-slot{slot}", "serial": str(number)}.items():
            put(nvme / key, value)
        put(block / "size", "8192")
        put(block / "dev", f"259:{number * 10 + ns}")
        driver = root / "sys/bus/pci/drivers/nvme"
        driver.mkdir(parents=True, exist_ok=True)
        for path, target in [(endpoint / "driver", driver), (nvme / "device", endpoint), (block / "device", nvme), (root / f"sys/class/block/nvme{number}n{ns}", block)]:
            if not path.exists():
                link(path, target)
        return endpoint, nvme, block

    def check(slot=0, selection="auto", expected=True, contains=""):
        p = subprocess.run([binary, str(root), str(slot), selection], capture_output=True, text=True)
        assert p.returncode == (0 if expected else 1), (slot, selection, p.stdout, p.stderr)
        assert contains in p.stdout, p.stdout

    # No endpoint: bridges alone never qualify.
    check(expected=False)
    # Numbering is deliberately reversed relative to physical ports.
    ep, nvme, block = device(0, 7)
    device(1, 0)
    check(contains="/dev/nvme7n1")
    check(1, "", contains="/dev/nvme0n1")
    check(selection="/dev/nvme0n1", expected=False)
    for selection in ["/dev/sda", "/dev/mmcblk0", "/dev/nvme7n1p1", "/dev/../nvme7n1"]:
        check(selection=selection, expected=False)
    for path, invalid, original in [(nvme / "transport", "tcp", "pcie"), (nvme / "state", "resetting", "live"), (ep / "class", "0x060400", "0x010802"), (ep / "current_link_width", "0", "1"), (block / "size", "0", "8192")]:
        put(path, invalid)
        check(expected=False)
        put(path, original)
    (ep / "driver").unlink()
    check(expected=False)
    link(ep / "driver", root / "sys/bus/pci/drivers/nvme")
    device(0, 7, 2)
    check(expected=False, contains="多个NVMe")
    check(selection="/dev/nvme7n1", contains="/dev/nvme7n1")
    # Fixture isolation: a namespace symlink outside the chosen controller fails.
    (root / "sys/class/block/nvme7n1").unlink()
    link(root / "sys/class/block/nvme7n1", root / "sys/class/block/nvme0n1")
    check(selection="/dev/nvme7n1", expected=False)
print("NVMe slot mapping and negative checks passed")
