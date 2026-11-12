#!/bin/bash
# Host-only test binaries reuse architecture-neutral SDK headers and Ubuntu's
# runtime libraries. They never replace the AArch64 delivery binary.
set -euo pipefail
cd "$(dirname "$0")/.."
host=${HOST_DIR:-../../buildroot/output/rockchip_rk3576/host}
sysroot=$host/aarch64-buildroot-linux-gnu/sysroot
mkdir -p build/native-include build/preview
for name in alsa cairo; do
    ln -sfn "$(realpath "$sysroot/usr/include/$name")" "build/native-include/$name"
done
for header in "$sysroot"/usr/include/wayland*.h; do
    ln -sfn "$(realpath "$header")" build/native-include/
done
read -r -a flags <<< "$("$host/bin/pkg-config" --cflags wayland-client cairo pangocairo alsa)"
includes=(-Ibuild/native-include -Ibuild -Isrc "${flags[@]}")
libs=(-l:libwayland-client.so.0 -l:libpangocairo-1.0.so.0 -l:libpango-1.0.so.0 -l:libgobject-2.0.so.0 -l:libglib-2.0.so.0 -l:libcairo.so.2 -l:libasound.so.2 -lm)
gcc -O1 -g -std=gnu11 "${includes[@]}" src/main.c src/factory.c src/nvme_probe.c src/checks.c src/hardware.c build/xdg-shell-protocol.c "${libs[@]}" -o build/cm5-factory-test-native
san=(-fsanitize=address,undefined -fno-omit-frame-pointer)
gcc -O1 -g -std=gnu11 "${san[@]}" "${includes[@]}" tests/backend_test.c src/factory.c src/nvme_probe.c src/checks.c src/hardware.c -lm -l:libcairo.so.2 -l:libasound.so.2 -o build/backend-test
gcc -O1 -g -std=gnu11 "${san[@]}" "${includes[@]}" tests/video_test.c -lm -o build/video-test
gcc -O1 -g -std=gnu11 "${san[@]}" "${includes[@]}" tests/ui_test.c src/factory.c src/nvme_probe.c src/checks.c src/hardware.c build/xdg-shell-protocol.c "${libs[@]}" -o build/ui-test
python3 tests/check.py ./build/backend-test ./build/cm5-factory-test-native ./build/video-test
# Fontconfig/Cairo caches survive process exit. Disable cache-only leak reports
# for this UI smoke test; bounds/undefined-behaviour checks remain enabled.
ASAN_OPTIONS=detect_leaks=0 ./build/ui-test
./build/cm5-factory-test-native --config cm5-factory-test.conf --render-preview build/preview
sh -n cm5-factory-launch

sh -n S98cm5-factory-test
python3 tests/autostart_test.py

gcc -O1 -g -std=gnu11 "${san[@]}" -Isrc tests/checks_test.c src/checks.c -lm -o build/checks-test
./build/checks-test build/checks-test.wav

gcc -O1 -g -std=gnu11 "${san[@]}" "${includes[@]}" tests/auto_test.c src/factory.c src/nvme_probe.c src/checks.c src/hardware.c -l:libcairo.so.2 -l:libasound.so.2 -lm -o build/auto-test
./build/auto-test

gcc -O1 -g -std=gnu11 "${san[@]}" "${includes[@]}" tests/camera_auto_test.c src/nvme_probe.c src/checks.c -l:libcairo.so.2 -l:libasound.so.2 -lm -o build/camera-auto-test
./build/camera-auto-test

gcc -O1 -g -std=gnu11 "${san[@]}" -Isrc tests/nvme_probe_test.c src/nvme_probe.c -o build/nvme-probe-test
python3 tests/nvme_probe_test.py ./build/nvme-probe-test
