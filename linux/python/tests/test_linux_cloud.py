# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
"""Test tích hợp: client Linux nói chuyện với mock-cloud THẬT qua WebSocket.

Đây là bằng chứng SDK Linux kết nối được cloud — test logic thôi thì không đủ.
Cần Go (dựng mock-cloud) và websocket-client; thiếu thì bỏ qua, có ghi lý do.
"""

import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
MOCK_DIR = os.path.join(HERE, "..", "..", "..", "tools", "mock-cloud")

try:
    import websocket  # noqa: F401
    HAVE_WS = True
except ImportError:
    HAVE_WS = False

from innoedge import InnoEdge  # noqa: E402


def free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def port_open(port):
    with socket.socket() as s:
        return s.connect_ex(("127.0.0.1", port)) == 0


def wait_until(cond, timeout=10.0):
    end = time.time() + timeout
    while time.time() < end:
        if cond():
            return True
        time.sleep(0.05)
    return False


@unittest.skipUnless(HAVE_WS, "chưa cài websocket-client (pip install websocket-client)")
@unittest.skipUnless(shutil.which("go"), "chưa cài Go — không dựng được mock-cloud")
class TestLinuxClientVoiMockCloud(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        binary = os.path.join(cls.tmp, "mock-cloud")
        subprocess.run(["go", "build", "-o", binary, "."], cwd=MOCK_DIR, check=True)
        cls.port = free_port()
        cls.proc = subprocess.Popen(
            [binary, "-addr", f"127.0.0.1:{cls.port}", "-paid-after", "300ms"],
            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if not wait_until(lambda: port_open(cls.port)):
            raise RuntimeError("mock-cloud không lên")

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait(timeout=5)
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def api(self, path, body):
        req = urllib.request.Request(f"http://127.0.0.1:{self.port}{path}", data=json.dumps(body).encode(),
                                     headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=5) as r:
            return json.load(r)

    def test_luong_day_du(self):
        store = tempfile.mkdtemp(dir=self.tmp)
        app = InnoEdge(cloud_url=f"ws://127.0.0.1:{self.port}/ws/", device_id="LINUXTEST01",
                       storage_dir=store, fw_version="0.2.0")
        runs, qrs, paid = [], [], []

        @app.command("dispense")
        def dispense(params):
            runs.append(params.get("n"))
            return {"pulses": 2}

        app.on_qr(lambda *a: qrs.append(a))
        app.on_paid(lambda i, a: paid.append((i, a)))
        app.start()
        try:
            # 1. Kết nối + được kích hoạt (mock gửi activation_complete kèm token).
            self.assertTrue(wait_until(app.is_assigned), "không được kích hoạt")
            self.assertTrue(app.state.data["token"])

            # 2. Tiền: ghi đĩa → gửi → cloud ack → hàng đợi rỗng.
            app.publish_payment("coin", 3)
            app.publish_event("door_open", {"n": 1})
            self.assertTrue(wait_until(lambda: app.queue_depth() == 0), "cloud chưa ack khung tiền")

            # 3. Lệnh từ cloud chạy một lần; gửi lại CÙNG commandId không chạy lần hai.
            self.api("/api/devices/command", {"deviceId": "LINUXTEST01", "action": "dispense",
                                              "params": {"n": 1}})
            self.assertTrue(wait_until(lambda: runs == [1]), f"handler chưa chạy: {runs}")
            self.api("/api/devices/dup", {})
            time.sleep(0.5)
            self.assertEqual(runs, [1], "lệnh trùng bị chạy lần hai")

            # 4. QR: xin QR → nhận QR → webhook giả → on_paid đúng một lần.
            self.assertTrue(app.request_qr(20000))
            self.assertTrue(wait_until(lambda: qrs and paid), f"qr={qrs} paid={paid}")
            intent = paid[0][0]
            self.assertEqual(paid, [(intent, 20000)])
            self.assertEqual(qrs[0][1], 20000)
        finally:
            app.stop()


if __name__ == "__main__":
    unittest.main()
