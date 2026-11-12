#!/usr/bin/env python3
"""Native service lifecycle test; no board, real Weston, or target binary."""
import json
import os
import pathlib
import socket
import subprocess
import tempfile
import time

project = pathlib.Path(__file__).resolve().parent.parent
with tempfile.TemporaryDirectory(prefix="cm5-init-test-") as work:
    root = pathlib.Path(work)
    stub = root / "stub"
    subprocess.run(["gcc", "-Wall", "-Wextra", "-Werror", str(project / "tests/autostart_stub.c"), "-o", str(stub)], check=True)
    cfg = root / "test.conf"
    cfg.write_text("# test config\n")
    runtime = root / "runtime"
    runtime.mkdir()
    env = dict(os.environ, CM5_PROGRAM=str(stub), CM5_CONFIG=str(cfg),
               CM5_PIDFILE=str(root / "app.pid"), CM5_LOGFILE=str(root / "app.log"),
               CM5_RUNTIME_DIR=str(runtime), CM5_REPORT_DIR=str(root / "reports"),
               CM5_WAIT_SECONDS="0", WAYLAND_DISPLAY="wayland-0", CM5_STUB_TRACE=str(root / "trace"))
    def run(action):
        return subprocess.run(["sh", str(project / "S98cm5-factory-test"), action], env=env,
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
    assert run("start").returncode != 0  # Missing Weston socket is bounded.
    assert not (root / "app.pid").exists()
    sock = socket.socket(socket.AF_UNIX)
    sock.bind(str(runtime / "wayland-0"))
    try:
        assert run("start").returncode == 0
        assert run("start").returncode == 0
        assert run("status").returncode == 0
        assert (root / "trace").read_text().splitlines() == ["started"]
        assert run("stop").returncode == 0
        assert run("status").returncode != 0
        reports = list((root / "reports").glob("*.json"))
        assert len(reports) == 1 and json.loads(reports[0].read_text())["stub_stopped_cleanly"]
        assert run("start").returncode == 0
        assert run("restart").returncode == 0
        assert run("stop").returncode == 0
        assert len(list((root / "reports").glob("*.json"))) == 3
        log = (root / "app.log").read_text()
        assert "--config" in log and "--report" in log and str(runtime) in log
    finally:
        run("stop")
        sock.close()
for name in ["S98cm5-factory-test", "cm5-factory-launch"]:
    data = (project / name).read_bytes()
    assert data.startswith(b"#!/bin/sh\n") and b"\r" not in data
print("autostart: bounded socket wait, one instance, stop/restart, persistent unique reports and LF scripts passed")
