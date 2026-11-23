#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 2 ]]; then
    echo "用法: $0 <官方源码基线包.tar.gz> <要恢复到的新目录>" >&2
    exit 2
fi

archive=$(realpath -e -- "$1")
destination=$(realpath -m -- "$2")
parent=$(dirname -- "$destination")
if [[ -e "$2" || -L "$2" || ! -d "$parent" ]]; then
    echo "请指定尚不存在的目标目录，其上级目录必须已存在。当前 SDK 会保留。" >&2
    exit 1
fi

read -r expected _ < "$archive.sha256"
if [[ ! "$expected" =~ ^[[:xdigit:]]{64}$ ]]; then
    echo "校验文件格式错误: $archive.sha256" >&2
    exit 1
fi
echo "正在校验基线包……"
actual=$(sha256sum < "$archive")
if [[ ${actual%% *} != "${expected,,}" ]]; then
    echo "基线包 SHA-256 不匹配，请重新获取压缩包和校验文件。" >&2
    exit 1
fi

stage=$(mktemp -d -- "$parent/.cm5-restore.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
echo "正在解压源码、工具链和板级配置……"
tar --extract --gzip --file="$archive" --directory="$stage" --no-same-owner --same-permissions -- 100ask-rk3576_SDK
sdk="$stage/100ask-rk3576_SDK"
config=device/rockchip/.chips/rk3576/rockchip_rk3576_dshanpi-a1_cm5_defconfig
if [[ ! -d "$sdk" || -L "$sdk" || ! -x "$sdk/build.sh" || ! -s "$sdk/$config" ]]; then
    echo "压缩包缺少 DshanPI A1 CM5 的编译入口或板级配置。" >&2
    exit 1
fi

mv --no-clobber --no-target-directory -- "$sdk" "$destination"
if [[ -e "$sdk" ]]; then
    echo "目标目录已被占用，请换一个目录重试。" >&2
    exit 1
fi
printf '已恢复 SDK 到 %s\n' "$destination"
