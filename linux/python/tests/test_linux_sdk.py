# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge

import os
import sys
import shutil
import tempfile
import unittest

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))

from innoedge import InnoEdge, PinRelay, LinuxGPIO

class TestInnoEdgeLinuxSDK(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.mkdtemp()
        self.app = InnoEdge(device_id="TEST-PI-01", storage_dir=self.temp_dir)

    def tearDown(self):
        shutil.rmtree(self.temp_dir, ignore_errors=True)

    def test_device_id_va_ledger(self):
        self.assertEqual(self.app.device_id, "TEST-PI-01")
        self.assertTrue(os.path.exists(os.path.join(self.temp_dir, "ledger.json")))

    def test_chong_trung_lenh_high_watermark(self):
        executed = []

        @self.app.command("dispense")
        def handle_dispense(params):
            executed.append(params)
            return {"status": "dispensed"}

        # Lệnh 1: commandId = 10 -> Thành công
        ack1 = self.app.handle_command_frame(10, "dispense", {"channel": 1})
        self.assertEqual(ack1["status"], "ok")
        self.assertEqual(len(executed), 1)

        # Lệnh 2: commandId = 10 (bị gửi lại do mạng chập chờn) -> PHẢI CHẶN TRÙNG!
        ack2 = self.app.handle_command_frame(10, "dispense", {"channel": 1})
        self.assertEqual(ack2["status"], "dup")
        self.assertEqual(len(executed), 1, "Lệnh trùng lặp không được chạy lại!")

        # Lệnh 3: commandId = 5 (lệnh cũ hơn) -> PHẢI CHẶN!
        ack3 = self.app.handle_command_frame(5, "dispense", {"channel": 1})
        self.assertEqual(ack3["status"], "dup")
        self.assertEqual(len(executed), 1)

        # Lệnh 4: commandId = 11 (lệnh mới hơn) -> Chấp nhận
        ack4 = self.app.handle_command_frame(11, "dispense", {"channel": 2})
        self.assertEqual(ack4["status"], "ok")
        self.assertEqual(len(executed), 2)

    def test_on_paid_event(self):
        received_paid = []

        @self.app.on_paid
        def on_paid(intent_id, amount):
            received_paid.append((intent_id, amount))

        self.app.dispatch_message('{"type":"paid","amount":25000,"intentId":88}')
        self.assertEqual(len(received_paid), 1)
        self.assertEqual(received_paid[0], (88, 25000))

    def test_relay_safety_watchdog(self):
        gpio = LinuxGPIO()
        relay = PinRelay(pin=17, max_pulse_sec=5, gpio_controller=gpio)
        self.assertEqual(relay.max_pulse_sec, 5)

    def test_digital_signage_emergency_and_ad_booking(self):
        state = {"emergency": False, "ads": []}

        @self.app.command("signage_emergency")
        def on_signage_emergency(params):
            state["emergency"] = params.get("active", False)
            return {"status": "ok", "emergency": state["emergency"]}

        @self.app.on_paid
        def on_ad_paid(intent_id, amount):
            if amount >= 50000:
                state["ads"].append(f"Ad-Paid-{intent_id}")

        # Cloud sends emergency broadcast
        ack = self.app.handle_command_frame(20, "signage_emergency", {"active": True, "message": "FIRE EVACUATION"})
        self.assertEqual(ack["status"], "ok")
        self.assertTrue(state["emergency"])

        # Customer buys ad via VietQR
        self.app.dispatch_message('{"type":"paid","amount":50000,"intentId":999}')
        self.assertEqual(len(state["ads"]), 1)
        self.assertEqual(state["ads"][0], "Ad-Paid-999")

    def test_central_audio_zone_filtering_and_priority(self):
        my_zone = 1
        audio_state = {"mode": "BGM", "last_broadcast": ""}

        @self.app.command("audio_paging")
        def on_audio_paging(params):
            target_zone = params.get("zone", 0)
            if target_zone in (0, my_zone):
                audio_state["mode"] = "PAGING"
                audio_state["last_broadcast"] = params.get("text", "")
                return {"status": "ok", "played": True}
            return {"status": "ignored", "reason": "different_zone"}

        # Paging targeted to zone 2 -> Ignored by zone 1
        ack_zone2 = self.app.handle_command_frame(30, "audio_paging", {"zone": 2, "text": "Zone 2 call"})
        self.assertEqual(ack_zone2["result"]["status"], "ignored")
        self.assertEqual(audio_state["mode"], "BGM")

        # Paging targeted to zone 1 -> Accepted
        ack_zone1 = self.app.handle_command_frame(31, "audio_paging", {"zone": 1, "text": "Customer check-in"})
        self.assertEqual(ack_zone1["result"]["status"], "ok")
        self.assertEqual(audio_state["mode"], "PAGING")
        self.assertEqual(audio_state["last_broadcast"], "Customer check-in")

if __name__ == "__main__":
    unittest.main()

