# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
"""Test example Jumper: lệnh bundle_install đi đúng đường thật — journal chống trùng
→ tải → kiểm SHA-256 → `controller --dry-run` → chuyển `current` — với bundle giả."""

import argparse
import hashlib
import io
import json
import os
import shutil
import sys
import tempfile
import unittest
import urllib.request
import zipfile
from unittest import mock

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
sys.path.insert(0, os.path.join(HERE, "..", "examples", "jumper"))

import jumper_fleet  # noqa: E402
from innoedge import artifact  # noqa: E402


def fake_bundle(dry_run_ok=True):
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, "w") as z:
        z.writestr("bundle.json", json.dumps({
            "schema": "kk-policy-bundle/1",
            "runtimes": {"board": {"file": "runtime/board/controller"}}}))
        info = zipfile.ZipInfo("runtime/board/controller")
        info.external_attr = 0o755 << 16
        z.writestr(info, '#!/bin/sh\n[ "$3" = "--dry-run" ] && exit %d\nexit 2\n' % (0 if dry_run_ok else 1))
    return buf.getvalue()


@unittest.skipIf(os.name != "posix", "cần shell POSIX để chạy controller giả")
class TestJumperFleet(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.args = argparse.Namespace(
            cloud="ws://127.0.0.1:1/ws/", device_id="JUMPER01", version="0.2.0",
            storage=os.path.join(self.tmp, "state"), bundle_root=os.path.join(self.tmp, "mjrl"),
            controller_service=None, heartbeat=30, watch_sec=1)
        self.app = jumper_fleet.build_app(self.args)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def install(self, cid, data, version):
        params = {"url": f"https://cdn.example/{version}.app", "sha256": hashlib.sha256(data).hexdigest(),
                  "size": len(data), "version": version}
        with mock.patch.object(urllib.request, "urlopen", lambda url, timeout=0: io.BytesIO(data)):
            return self.app.handle_command_frame(cid, "bundle_install", params)

    def test_cai_rollback_va_chong_trung(self):
        good = fake_bundle()
        ack = self.install(1, good, "v1")
        self.assertEqual(ack["status"], "ok", ack)
        self.assertEqual(artifact.current_version(self.args.bundle_root), "v1")
        self.assertEqual(self.install(2, good, "v2")["result"]["previous"], "v1")
        # Cloud gửi lại lệnh cài v1 (commandId 1): không chạy lại, không đổi current.
        self.assertEqual(self.install(1, good, "v1")["message"], "duplicate")
        self.assertEqual(artifact.current_version(self.args.bundle_root), "v2")
        ack = self.app.handle_command_frame(3, "bundle_rollback", {})
        self.assertEqual(ack["result"]["version"], "v1")

    def test_dry_run_hong_thi_khong_doi_current(self):
        self.install(1, fake_bundle(), "v1")
        ack = self.install(2, fake_bundle(dry_run_ok=False), "v2")
        self.assertEqual(ack["status"], "error")
        self.assertIn("dry-run", ack["message"])
        self.assertEqual(artifact.current_version(self.args.bundle_root), "v1")

    def test_restart_can_xac_nhan_va_cau_hinh(self):
        ack = self.app.handle_command_frame(9, "controller_restart", {"confirm": True})
        self.assertEqual(ack["status"], "error")
        self.assertIn("controller-service", ack["message"])

    def test_status_khong_lai_khop(self):
        res = self.app.handle_command_frame(10, "robot_status", {})["result"]
        self.assertEqual(res["controller"], "not-configured")
        self.assertIn("soc_temp_c", res)


if __name__ == "__main__":
    unittest.main()
