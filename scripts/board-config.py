#!/usr/bin/env python3
"""Apply the DshanPI A1 CM5 board files or restore the first local snapshot."""
import argparse
import fcntl
import hashlib
import json
import os
import shutil
import stat
import tempfile
from pathlib import Path

repo = Path(__file__).resolve().parents[1]


def target(root, name):
    path = root / name
    if path.resolve() != path or root not in path.parents:
        raise ValueError(f"路径不在指定目录中或含符号链接: {name}")
    if path.exists() and not path.is_file():
        raise ValueError(f"目标不是普通文件: {path}")
    return path


def replace(source, dest, mode):
    dest.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=dest.parent, delete=False) as tmp:
        try:
            with source.open("rb") as src:
                shutil.copyfileobj(src, tmp)
            tmp.flush()
            os.fchmod(tmp.fileno(), mode)
            os.replace(tmp.name, dest)
        finally:
            Path(tmp.name).unlink(missing_ok=True)


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def run(action, sdk):
    state = sdk / ".cm5-board-backup"
    if state.is_symlink():
        raise ValueError(f"备份目录不能是符号链接: {state}")
    state.mkdir(mode=0o700, exist_ok=True)
    with target(state, "lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        index = target(state, "originals.json")
        saved = json.loads(index.read_text()) if index.exists() else {}
        if action == "apply":
            manifest = json.loads((repo / "manifest.json").read_text())
            for name, meta in manifest.items():
                if digest(target(repo, name)) != meta["sha256"]:
                    raise ValueError(f"配置文件校验失败: {name}")
                dest = target(sdk, name)
                if name not in saved:
                    saved[name] = None
                    if dest.exists():
                        mode = stat.S_IMODE(dest.stat().st_mode)
                        original = target(state / "files", name)
                        replace(dest, original, mode)
                        saved[name] = [mode, digest(original)]
            with tempfile.NamedTemporaryFile(mode="w", dir=state, delete=False) as tmp:
                json.dump(saved, tmp)
            os.replace(tmp.name, index)
            for name, meta in manifest.items():
                replace(repo / name, target(sdk, name), int(meta["mode"], 8))
            print(f"已应用 {len(manifest)} 个板级文件。")
        else:
            if not index.exists():
                raise ValueError("没有首次应用时的备份。")
            for name, meta in saved.items():
                target(sdk, name)
                if meta and digest(target(state / "files", name)) != meta[1]:
                    raise ValueError(f"备份校验失败: {name}")
            for name, meta in saved.items():
                dest = target(sdk, name)
                if meta is None:
                    dest.unlink(missing_ok=True)
                else:
                    replace(state / "files" / name, dest, meta[0])
            print(f"已恢复首次应用前的 {len(saved)} 个文件状态。")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("apply", "restore"))
    parser.add_argument("sdk", type=Path)
    args = parser.parse_args()
    sdk = args.sdk.resolve()
    if not (sdk / "build.sh").is_file() or not (sdk / "device/rockchip/.chips/rk3576").is_dir():
        parser.error("请指定已解压的 RK3576 SDK 根目录。")
    run(args.action, sdk)
