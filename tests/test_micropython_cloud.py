# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
"""Test micropython/innoedge_cloud.py trên CPython.

Phần offline: hàng đợi bền, nhật ký lệnh, chống giao QR hai lần.
Phần mạng: WebSocket tự viết (RFC 6455) nói chuyện với mock-cloud thật — cần Go.
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
sys.path.insert(0, os.path.join(HERE, "..", "micropython"))
MOCK = os.path.join(HERE, "..", "tools", "mock-cloud")

import innoedge_cloud as ic  # noqa: E402


class Offline(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.path = os.path.join(self.dir, "state.json")

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def boot(self):
        return ic.Cloud("ws://127.0.0.1:1/ws/", "AABBCCDDEEFF", storage=self.path)

    def test_hang_doi_ben_qua_reboot(self):
        c = self.boot()
        s1 = c.publish_payment("coin", 3)
        c2 = self.boot()
        self.assertEqual(c2.queue_depth(), 1)
        self.assertGreater(c2.publish_payment("cash", amount_vnd=20000), s1)
        c2.dispatch(json.dumps({"type": "coin_ack", "seq": s1}))
        self.assertEqual(self.boot().queue_depth(), 1)

    def test_lenh_mot_lan_lech_thu_tu_va_bi_ngat(self):
        runs = []
        c = self.boot()
        c.command("go")(lambda p: runs.append(p.get("n")))
        self.assertEqual(c.handle_command(11, "go", {"n": 11})["status"], "ok")
        self.assertEqual(c.handle_command(10, "go", {"n": 10})["status"], "ok")  # lệch thứ tự
        self.assertEqual(c.handle_command(11, "go", {})["message"], "duplicate")
        self.assertEqual(runs, [11, 10])
        c._cmd.begin(12)  # tắt máy giữa handler
        c._save()
        c2 = self.boot()
        c2.command("go")(lambda p: runs.append(p))
        self.assertIn("uncertain", c2.handle_command(12, "go", {})["message"])
        self.assertTrue(any(a["code"] == "command_interrupted" for a in c2._pending_alerts))
        self.assertEqual(runs, [11, 10])

    def test_qr_khong_giao_hai_lan(self):
        paid = []
        c = self.boot()
        c.on_paid(lambda i, a: paid.append(i))
        for intent in (88, 88, 87):
            c.dispatch(json.dumps({"type": "payment_paid", "intentId": intent, "amount": 1000}))
        c2 = self.boot()
        c2.on_paid(lambda i, a: paid.append(i))
        c2.dispatch(json.dumps({"type": "payment_paid", "intentId": 88, "amount": 1000}))
        self.assertEqual(paid, [88, 87])


def _free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


@unittest.skipUnless(shutil.which("go"), "chưa cài Go — không dựng được mock-cloud")
class WithMockCloud(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.mkdtemp()
        binary = os.path.join(cls.tmp, "mock-cloud")
        subprocess.run(["go", "build", "-o", binary, "."], cwd=MOCK, check=True)
        cls.port = _free_port()
        cls.proc = subprocess.Popen([binary, "-addr", "127.0.0.1:%d" % cls.port, "-paid-after", "300ms"],
                                    stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)
        for _ in range(100):
            with socket.socket() as s:
                if s.connect_ex(("127.0.0.1", cls.port)) == 0:
                    return
            time.sleep(0.05)
        raise RuntimeError("mock-cloud không lên")

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait(timeout=5)
        shutil.rmtree(cls.tmp, ignore_errors=True)

    def pump(self, c, cond, secs=8):
        end = time.time() + secs
        while time.time() < end:
            c.poll(0.05)
            if cond():
                return True
        return False

    def api(self, path, body):
        req = urllib.request.Request("http://127.0.0.1:%d%s" % (self.port, path),
                                     data=json.dumps(body).encode(),
                                     headers={"Content-Type": "application/json"}, method="POST")
        with urllib.request.urlopen(req, timeout=5) as r:
            return json.load(r)

    def test_luong_day_du(self):
        c = ic.Cloud("ws://127.0.0.1:%d/ws/" % self.port, "MPYTEST00001",
                     storage=os.path.join(self.tmp, "mpy.json"))
        runs, paid = [], []
        c.command("go")(lambda p: runs.append(p.get("n")) or {"moved": True})
        c.on_paid(lambda i, a: paid.append((i, a)))
        self.assertTrue(self.pump(c, lambda: c.st["token"]), "không được kích hoạt")
        c.publish_payment("coin", 2)
        self.assertTrue(self.pump(c, lambda: c.queue_depth() == 0), "cloud chưa ack tiền")
        self.api("/api/devices/command", {"deviceId": "MPYTEST00001", "action": "go", "params": {"n": 1}})
        self.assertTrue(self.pump(c, lambda: runs == [1]), "lệnh chưa chạy: %s" % runs)
        self.api("/api/devices/dup", {})
        self.pump(c, lambda: False, secs=0.6)
        self.assertEqual(runs, [1], "lệnh trùng bị chạy lần hai")
        self.assertTrue(c.request_qr(15000))
        self.assertTrue(self.pump(c, lambda: paid), "chưa nhận payment_paid")
        self.assertEqual(paid[0][1], 15000)


if __name__ == "__main__":
    unittest.main()
