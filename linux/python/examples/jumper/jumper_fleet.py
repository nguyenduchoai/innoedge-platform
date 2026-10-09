#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
"""Jumper (robot cua 22 khớp, Rockchip RK3576) nối vào InnoEdge — quản lý đội robot.

InnoEdge KHÔNG lái khớp. Vòng điều khiển 1 kHz và các chốt an toàn của Jumper
(nghiêng quá 50°, mất phản hồi động cơ 100 ms, mất lệnh 500 ms) nằm trong
controller của chính nó; một lệnh đi qua Internet trễ hàng trăm mili-giây không
có chỗ trong vòng đó. Phần InnoEdge lo là thứ một đội robot cần mà controller
không lo:

  - heartbeat kèm tình trạng: controller sống/chết, bundle đang chạy, nhiệt SoC;
  - cảnh báo lên cloud khi controller dừng hoặc SoC quá nóng;
  - cài bundle chuyển động `.app` từ xa: kiểm SHA-256 + kích thước, chạy
    `controller --dry-run` trên chính robot TRƯỚC khi chuyển `current`;
  - rollback một lệnh; khởi động lại controller khi người vận hành xác nhận.

Chạy thử với cloud giả:  go run ./tools/mock-cloud
                         python3 jumper_fleet.py --cloud ws://<ip>:8080/ws/ --bundle-root /tmp/mjrl
"""

import argparse
import glob
import json
import logging
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2]))  # chạy thẳng từ repo

from innoedge import InnoEdge, artifact  # noqa: E402

log = logging.getLogger("jumper")

BUNDLE_SCHEMA = "kk-policy-bundle/1"
TEMP_ALERT_C, TEMP_CLEAR_C = 85.0, 80.0  # trễ 5°C để cảnh báo không bật/tắt liên tục


def soc_temp_c():
    temps = []
    for zone in glob.glob("/sys/class/thermal/thermal_zone*/temp"):
        try:
            temps.append(int(Path(zone).read_text().strip()) / 1000.0)
        except (OSError, ValueError):
            pass
    return max(temps) if temps else None


def controller_state(service):
    if not service:
        return "not-configured"
    r = subprocess.run(["systemctl", "is-active", service], capture_output=True, text=True)
    return r.stdout.strip() or "unknown"


def check_bundle(path):
    """Cửa kiểm trước khi cho bundle mới thành `current`. Raise = từ chối."""
    path = Path(path)
    manifest = json.loads((path / "bundle.json").read_text())
    if manifest.get("schema") != BUNDLE_SCHEMA:
        raise ValueError(f"schema {manifest.get('schema')!r}, cần {BUNDLE_SCHEMA}")
    board = manifest.get("runtimes", {}).get("board", {}).get("file", "runtime/board/controller")
    controller = path / board
    if not artifact.is_executable(controller):
        raise ValueError(f"{board} không có hoặc mất quyền chạy")
    # --dry-run: controller mở bundle như trên board, báo cáo, KHÔNG mở bus động cơ.
    cmd = [str(controller), "--bundle", str(path), "--dry-run"]
    if os.geteuid() == 0 and shutil.which("systemd-run"):
        # Đừng chỉ tin vào cờ --dry-run: binary nào phớt lờ nó sẽ mở bus động cơ trong
        # lúc controller thật đang chạy. Sandbox không mạng (DDS loopback/shm không với
        # tới bus), không thiết bị, không IPC, user tạm.
        cmd = ["systemd-run", "--wait", "--pipe", "--collect", "--quiet",
               "-p", "PrivateNetwork=yes", "-p", "PrivateDevices=yes", "-p", "PrivateIPC=yes",
               "-p", "DynamicUser=yes", "-p", "NoNewPrivileges=yes", "-p", "ProtectSystem=strict",
               "--"] + cmd
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    if r.returncode != 0:
        raise ValueError(f"controller --dry-run lỗi: {(r.stderr or r.stdout).strip()[-300:]}")


def build_app(args):
    app = InnoEdge(cloud_url=args.cloud, device_id=args.device_id, fw_version=args.version,
                   storage_dir=args.storage, heartbeat_sec=args.heartbeat)
    root = Path(args.bundle_root)

    def status():
        return {"controller": controller_state(args.controller_service),
                "bundle": artifact.current_version(root), "soc_temp_c": soc_temp_c()}

    app.heartbeat_extra = status

    @app.command("robot_status")
    def robot_status(params):
        return status()

    @app.command("bundle_install")
    def bundle_install(params):
        before = artifact.current_version(root)
        artifact.install(params["url"], params["sha256"], params["size"], root,
                         params["version"], validate=check_bundle)
        app.publish_event("jumper_bundle", {"action": "install", "from": before,
                                            "to": params["version"]})
        # Không tự khởi động lại: robot có thể đang đứng/đi. Người vận hành gửi
        # controller_restart khi robot đã nằm nghỉ.
        return {"version": params["version"], "previous": before,
                "restart_required": controller_state(args.controller_service) == "active"}

    @app.command("bundle_rollback")
    def bundle_rollback(params):
        now = artifact.rollback(root)
        app.publish_event("jumper_bundle", {"action": "rollback", "to": now})
        return {"version": now}

    @app.command("controller_restart")
    def controller_restart(params):
        if not args.controller_service:
            raise RuntimeError("chưa cấu hình --controller-service")
        if params.get("confirm") is not True:
            raise RuntimeError('cần params {"confirm": true} — robot phải đang nằm nghỉ')
        subprocess.run(["systemctl", "restart", args.controller_service], check=True, timeout=60)
        return {"controller": controller_state(args.controller_service)}

    return app


def watch(app, args, stop_after=None):
    """Theo dõi tại chỗ; cảnh báo có latch nên gọi lặp không spam cloud."""
    hot = False
    n = 0
    while stop_after is None or n < stop_after:
        state = controller_state(args.controller_service)
        if state != "not-configured":
            app.alert("jumper_controller_down", "critical",
                      f"Controller Jumper khong chay ({state})", active=(state != "active"))
        t = soc_temp_c()
        if t is not None:
            if t >= TEMP_ALERT_C and not hot:
                hot = True
                app.alert("jumper_soc_hot", "warning", f"SoC {t:.0f}C - kiem tra quat/tan nhiet")
            elif t <= TEMP_CLEAR_C and hot:
                hot = False
                app.alert("jumper_soc_hot", active=False)
        n += 1
        time.sleep(args.watch_sec)


def main():
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("--cloud", default=os.environ.get("INNOEDGE_WS", "ws://127.0.0.1:8080/ws/"))
    p.add_argument("--device-id", default=None, help="mặc định: MAC của máy")
    p.add_argument("--version", default="0.2.0", help="phiên bản phần mềm fleet này")
    p.add_argument("--storage", default="/var/lib/innoedge-jumper")
    p.add_argument("--bundle-root", default="/opt/mjrl", help="controller chạy --bundle <root>/current")
    p.add_argument("--controller-service", default=None, help="tên unit systemd của controller")
    p.add_argument("--heartbeat", type=int, default=30)
    p.add_argument("--watch-sec", type=int, default=10)
    args = p.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(asctime)s %(name)s %(levelname)s %(message)s")

    app = build_app(args)
    app.start()
    try:
        watch(app, args)
    except KeyboardInterrupt:
        pass
    finally:
        app.stop()


if __name__ == "__main__":
    main()
