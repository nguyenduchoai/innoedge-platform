# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
"""Test logic SDK Linux — không cần mạng, không cần websocket-client.

Kết nối cloud thật được thử ở test_linux_cloud.py (cần mock-cloud + Go).
"""

import hashlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from innoedge import InnoEdge, LinuxGPIO, PinRelay, artifact  # noqa: E402
from innoedge import journal as J  # noqa: E402


class Base(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp()
        self.app = self.boot()

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def boot(self):
        """Khởi động lại máy: trạng thái đĩa giữ nguyên, RAM mới hoàn toàn."""
        return InnoEdge(device_id="TEST01", storage_dir=self.dir)

    def drain(self, app=None):
        app = app or self.app
        while not app._jobs.empty():
            app._jobs.get()()


class TestHangDoiTien(Base):
    def test_khung_dung_giao_thuc_va_seq_ben_qua_reboot(self):
        s1 = self.app.publish_payment("coin", 3)
        s2 = self.app.publish_payment("cash", amount_vnd=20000)
        q = self.app.state.data["queue"]
        self.assertEqual({k: q[0][k] for k in ("type", "coins", "seq")}, {"type": "coin", "coins": 3, "seq": s1})
        self.assertEqual((q[1]["type"], q[1]["method"], q[1]["amount"]), ("payment", "cash", 20000))
        app2 = self.boot()  # mất điện: hàng đợi và bộ đếm seq vẫn còn
        self.assertEqual(app2.queue_depth(), 2)
        self.assertGreater(app2.publish_payment("ticket", 5), s2)

    def test_ack_go_dung_khung_ke_ca_lech_thu_tu(self):
        s1 = self.app.publish_payment("coin", 1)
        s2 = self.app.publish_event("door_open", {"n": 1})
        self.app.dispatch_message(json.dumps({"type": "event_ack", "seq": s2}))
        self.app.dispatch_message(json.dumps({"type": "coin_ack", "seq": s1}))
        self.assertEqual(self.boot().queue_depth(), 0)

    def test_tu_choi_dau_vao_hong(self):
        for bad in (lambda: self.app.publish_payment("coin", 0),
                    lambda: self.app.publish_payment("cash", amount_vnd=0),
                    lambda: self.app.publish_payment("xu", 1),
                    lambda: self.app.publish_event("có dấu", {}),
                    lambda: self.app.publish_event("ok", "not-a-dict")):
            with self.assertRaises(ValueError):
                bad()
        self.assertEqual(self.app.queue_depth(), 0)

    def test_hong_mot_ban_khong_mat_gi(self):
        seq = self.app.publish_payment("coin", 2)
        with open(os.path.join(self.dir, "state.json"), "w") as f:
            f.write("{hỏng")
        app = self.boot()
        self.assertEqual(app.queue_depth(), 1)             # khung tiền vẫn còn
        self.assertGreater(app.publish_payment("coin", 1), seq)
        self.assertIn("state_file_corrupt", app._alert_unsent)

    def test_hong_ca_hai_ban_seq_khong_trung_seq_cu(self):
        self.app.publish_payment("coin", 2)
        for name in ("state.json", "state.json.bak"):
            with open(os.path.join(self.dir, name), "w") as f:
                f.write("{hỏng")
        app = self.boot()
        self.assertIn("state_file_corrupt", app._alert_unsent)
        self.assertGreater(app.publish_payment("coin", 1), 10 ** 12)  # seq theo đồng hồ ms
        self.assertTrue(any(".corrupt-" in n for n in os.listdir(self.dir)))

    def test_ghi_dia_loi_thi_khong_de_lai_khung(self):
        from unittest import mock
        with mock.patch.object(self.app.state, "save", side_effect=OSError("disk full")):
            with self.assertRaises(OSError):
                self.app.publish_payment("coin", 1)
        self.assertEqual(self.app.queue_depth(), 0)
        self.assertEqual(self.app.publish_payment("coin", 1), 1)  # seq không bị nhảy cóc


class TestLenhChongTrung(Base):
    def setUp(self):
        super().setUp()
        self.runs = []
        self.register(self.app)

    def register(self, app):
        @app.command("dispense")
        def dispense(params):
            self.runs.append(params.get("n"))
            return {"pulses": 2}

        @app.command("fail")
        def fail(params):
            self.runs.append("fail")
            raise RuntimeError("het xu")

    def test_trung_cung_phien_va_qua_reboot(self):
        self.assertEqual(self.app.handle_command_frame(10, "dispense", {"n": 1})["status"], "ok")
        self.assertEqual(self.app.handle_command_frame(10, "dispense", {})["message"], "duplicate")
        app2 = self.boot()
        self.register(app2)
        self.assertEqual(app2.handle_command_frame(10, "dispense", {})["message"], "duplicate")
        self.assertEqual(self.runs, [1])

    def test_lech_thu_tu_sau_reboot_van_chay(self):
        self.app.handle_command_frame(11, "dispense", {"n": 11})
        app2 = self.boot()
        self.register(app2)
        self.assertEqual(app2.handle_command_frame(10, "dispense", {"n": 10})["status"], "ok")
        self.assertEqual(self.runs, [11, 10])

    def test_lech_thu_tu_van_chay_du(self):
        self.app.handle_command_frame(50, "dispense", {"n": 50})
        self.app.handle_command_frame(40, "dispense", {"n": 40})
        self.assertEqual(self.runs, [50, 40])

    def test_handler_loi_khong_chay_lai_khong_ack_ok(self):
        self.assertEqual(self.app.handle_command_frame(60, "fail", {})["message"], "het xu")
        again = self.app.handle_command_frame(60, "fail", {})
        self.assertEqual(again["status"], "error")
        self.assertIn("failed", again["message"])
        self.assertEqual(self.runs, ["fail"])

    def test_mat_dien_giua_handler_thi_bao_khong_chac(self):
        # Mô phỏng: RUNNING đã ghi đĩa, máy tắt trước khi ghi kết quả.
        self.app._cmd.begin(70)
        self.app.state.data["last_cmd"] = 70
        self.app._persist("test")
        app2 = self.boot()
        self.register(app2)
        self.assertIn("command_interrupted", app2._alert_unsent)
        ack = app2.handle_command_frame(70, "dispense", {})
        self.assertEqual(ack["status"], "error")
        self.assertIn("uncertain", ack["message"])
        self.assertEqual(self.runs, [])

    def test_id_khong_hop_le_va_action_la(self):
        self.assertEqual(self.app.handle_command_frame(0, "dispense", {})["status"], "error")
        self.assertEqual(self.app.handle_command_frame(5, "khong_co", {})["message"], "unknown action")
        self.assertEqual(self.runs, [])

    def test_journal_hong_dung_lai_tu_watermark(self):
        self.app.handle_command_frame(30, "dispense", {"n": 30})
        self.app.state.data["cmd_journal"] = {"entries": "hỏng"}
        self.app.state.save()
        app2 = self.boot()
        self.register(app2)
        self.assertIn("command_journal_reset", app2._alert_unsent)
        self.assertIn("uncertain", app2.handle_command_frame(30, "dispense", {})["message"])
        self.assertEqual(app2.handle_command_frame(31, "dispense", {"n": 31})["status"], "ok")
        self.assertEqual(self.runs, [30, 31])

    def test_roi_khoi_cua_so_khong_bao_gio_ok(self):
        for i in range(100, 100 + J.SLOTS + 1):
            self.app.handle_command_frame(i, "dispense", {})
        self.assertIn("uncertain", self.app.handle_command_frame(100, "dispense", {})["message"])


class TestTienQR(Base):
    def setUp(self):
        super().setUp()
        self.paid = []
        self.app.on_paid(lambda i, a: self.paid.append((i, a)))

    def paid_frame(self, app, intent, amount=25000):
        app.dispatch_message(json.dumps({"type": "payment_paid", "intentId": intent, "amount": amount}))
        self.drain(app)

    def test_gui_lai_chi_giao_mot_lan_ke_ca_qua_reboot(self):
        self.paid_frame(self.app, 88)
        self.paid_frame(self.app, 88)
        app2 = self.boot()
        app2.on_paid(lambda i, a: self.paid.append((i, a)))
        self.paid_frame(app2, 88)
        self.assertEqual(self.paid, [(88, 25000)])

    def test_hai_khach_tra_lech_thu_tu(self):
        self.paid_frame(self.app, 88)
        self.paid_frame(self.app, 87)
        self.assertEqual([i for i, _ in self.paid], [88, 87])

    def test_bi_ngat_hoac_qua_cu_thi_bao_doi_soat_khong_giao(self):
        self.app._paid.begin(91)          # đang giao thì tắt máy
        self.app._persist("test")
        app2 = self.boot()
        app2.on_paid(lambda i, a: self.paid.append((i, a)))
        self.paid_frame(app2, 91)
        self.assertEqual(self.paid, [])
        self.assertEqual(app2._alert_unsent["paid_uncertain"]["severity"], "critical")
        for i in range(1000, 1000 + J.SLOTS + 1):
            self.paid_frame(app2, i)
        n = len(self.paid)
        self.paid_frame(app2, 1000)        # webhook tới muộn, đã rơi khỏi cửa sổ
        self.assertEqual(len(self.paid), n)

    def test_thieu_intent_khong_giao(self):
        self.paid_frame(self.app, 0)
        self.assertEqual(self.paid, [])

    def test_qr_dung_truong_giao_thuc(self):
        got = []
        self.app.on_qr(lambda *a: got.append(a))
        self.app.dispatch_message(json.dumps({"type": "qr", "qrPayload": "000201", "amount": 20000,
                                              "refCode": "R1", "expiresSec": 300, "intentId": 9}))
        self.drain()
        self.assertEqual(got, [("000201", 20000, "R1", 300, 9)])


class TestKichHoat(Base):
    def test_activation_luu_token_va_bao_gan(self):
        hits = []
        self.app.on_assigned(lambda: hits.append("assigned"))
        self.app.dispatch_message(json.dumps({"type": "command", "command": "activation_complete",
                                              "authToken": "tok-123"}))
        self.drain()
        self.assertEqual(hits, ["assigned"])
        app2 = self.boot()
        self.assertTrue(app2.is_assigned())
        self.assertIn("Authorization: Bearer tok-123", app2._headers())


class TestArtifact(Base):
    def make_zip(self):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as z:
            info = zipfile.ZipInfo("runtime/board/controller")
            info.external_attr = 0o755 << 16  # bit x phải sống sót qua giải nén
            z.writestr(info, "#!/bin/sh\necho ok\n")
            z.writestr("bundle.json", '{"schema":"kk-policy-bundle/1"}')
        return buf.getvalue()

    def opener_for(self, data):
        return lambda url, timeout=0: io.BytesIO(data)

    def test_cai_kiem_sha_giu_quyen_chay_va_rollback(self):
        root = os.path.join(self.dir, "opt")
        a = self.make_zip()
        sha = hashlib.sha256(a).hexdigest()
        rel = artifact.install("https://x/a.app", sha, len(a), root, "v1", opener=self.opener_for(a))
        self.assertTrue(artifact.is_executable(rel / "runtime/board/controller"))
        self.assertEqual(artifact.current_version(root), "v1")
        artifact.install("https://x/b.app", sha, len(a), root, "v2", opener=self.opener_for(a))
        self.assertEqual(artifact.current_version(root), "v2")
        self.assertEqual(artifact.rollback(root), "v1")
        self.assertEqual(artifact.current_version(root), "v1")

    def test_tu_choi_file_sai(self):
        root = os.path.join(self.dir, "opt")
        a = self.make_zip()
        sha = hashlib.sha256(a).hexdigest()
        cases = [
            dict(url="https://x/a", sha256="0" * 64, size=len(a), version="v1"),     # sai sha
            dict(url="https://x/a", sha256=sha, size=len(a) + 1, version="v1"),      # sai size
            dict(url="http://x/a", sha256=sha, size=len(a), version="v1"),           # không https
            dict(url="https://x/a", sha256=sha, size=len(a), version="../evil"),     # version bẩn
        ]
        for c in cases:
            with self.assertRaises(artifact.ArtifactError):
                artifact.install(root=root, opener=self.opener_for(a), **c)
        self.assertIsNone(artifact.current_version(root))

    def test_validate_tu_choi_thi_khong_doi_current(self):
        root = os.path.join(self.dir, "opt")
        a = self.make_zip()
        sha = hashlib.sha256(a).hexdigest()
        artifact.install("https://x/a", sha, len(a), root, "v1", opener=self.opener_for(a))

        def bad(path):
            raise RuntimeError("dry-run fail")
        with self.assertRaises(artifact.ArtifactError):
            artifact.install("https://x/a", sha, len(a), root, "v2", validate=bad, opener=self.opener_for(a))
        self.assertEqual(artifact.current_version(root), "v1")

    def test_cai_lai_ban_dang_chay_khong_bao_gio_xoa_no(self):
        root = os.path.join(self.dir, "opt")
        a = self.make_zip()
        sha = hashlib.sha256(a).hexdigest()
        artifact.install("https://x/a", sha, len(a), root, "v1", opener=self.opener_for(a))

        def flaky(path):
            raise RuntimeError("dry-run timeout")
        rel = artifact.install("https://x/a", sha, len(a), root, "v1", validate=flaky, opener=self.opener_for(a))
        self.assertTrue((rel / "bundle.json").exists())
        self.assertEqual(artifact.current_version(root), "v1")

    def test_ban_cu_khac_noi_dung_bi_tu_choi_khong_bi_xoa(self):
        root = os.path.join(self.dir, "opt")
        a = self.make_zip()
        sha = hashlib.sha256(a).hexdigest()
        artifact.install("https://x/a", sha, len(a), root, "v1", opener=self.opener_for(a))
        artifact.install("https://x/a", sha, len(a), root, "v2", opener=self.opener_for(a))
        with self.assertRaises(artifact.ArtifactError):  # v1 là previous, sha khác
            artifact.install("https://x/a", "f" * 64, len(a), root, "v1", opener=self.opener_for(a))
        self.assertTrue(os.path.isdir(os.path.join(root, "releases", "v1")))

    def test_zip_thoat_thu_muc_bi_chan(self):
        buf = io.BytesIO()
        with zipfile.ZipFile(buf, "w") as z:
            z.writestr("../../etc/evil", "x")
        a = buf.getvalue()
        with self.assertRaises(artifact.ArtifactError):
            artifact.install("https://x/a", hashlib.sha256(a).hexdigest(), len(a),
                             os.path.join(self.dir, "opt"), "v1", opener=self.opener_for(a))


class TestGPIO(unittest.TestCase):
    def test_relay_co_tran_an_toan(self):
        relay = PinRelay(pin=17, max_pulse_sec=5, gpio_controller=LinuxGPIO())
        self.assertEqual(relay.max_pulse_sec, 5)


if __name__ == "__main__":
    unittest.main()
