#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
# Bảng Quảng Cáo Kỹ Thuật Số (Digital Signage) trên Raspberry Pi & Banana Pi

import sys
import os
import time
import logging

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from innoedge import InnoEdge

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")

app = InnoEdge(cloud_url=os.environ.get("INNOEDGE_WS", "ws://127.0.0.1:8080/ws/"),
               fw_version="1.0.0")  # chạy kèm: go run ./tools/mock-cloud

playlist = [
    {"id": 1, "title": "Khuyến Mãi Mùa Hè", "duration": 5},
    {"id": 2, "title": "Cà Phê Rang Xay Mộc", "duration": 6},
    {"id": 3, "title": "Thanh Toán VietQR Tiện Lợi", "duration": 4},
]
current_slot = 0
emergency_mode = False

@app.on_qr
def on_qr(payload, amount, ref_code, expires_sec, intent_id):
    print(f"\n[SIGNAGE SCREEN] Hiển thị mã VietQR mua slot quảng cáo ({amount:,} đ)")
    print(f"  Mã đơn: {ref_code} | Hết hạn sau: {expires_sec}s")
    print(f"  Payload: {payload}")

@app.on_paid
def on_paid(intent_id, amount_vnd):
    global playlist, current_slot
    print(f"\n🎉 [TIỀN VỀ] Khách thanh toán {amount_vnd:,} đ thành công!")
    print("  -> Chèn quảng cáo cá nhân của khách vào màn hình ngay lập tức!")
    user_ad = {"id": intent_id, "title": f"Lời Nhắn Của Khách #{intent_id}", "duration": 8}
    playlist.append(user_ad)
    current_slot = len(playlist) - 1

@app.command("signage_emergency")
def cmd_emergency(params):
    global emergency_mode
    emergency_mode = True
    msg = params.get("message", "BÁO ĐỘNG KHẨN CẤP")
    print(f"\n🚨🚨 [MÀN HÌNH CHUYỂN ĐỎ] CẢNH BÁO: {msg} 🚨🚨")
    return {"emergency": True}

@app.command("signage_clear_emergency")
def cmd_clear_emergency(params):
    global emergency_mode
    emergency_mode = False
    print("\n✓ Đã khôi phục chế độ chiếu quảng cáo bình thường.")
    return {"emergency": False}

if __name__ == "__main__":
    print(f"🚀 Khởi chạy Bảng Quảng Cáo Kỹ Thuật Số trên Linux SBC ({app.device_id})")
    app.start()

    # Vòng lặp chiếu quảng cáo & báo cáo Proof of Play
    for i in range(2):
        if not emergency_mode and playlist:
            ad = playlist[current_slot]
            print(f"\n📺 [MÀN HÌNH ĐANG CHIẾU] Slot #{ad['id']}: {ad['title']} ({ad['duration']}s)")
            time.sleep(ad['duration'])
            # Gửi Proof-of-Play lên Cloud để đối soát doanh thu
            app.publish_event("proof_of_play", {"ad_id": ad["id"], "title": ad["title"], "duration": ad["duration"]})
            current_slot = (current_slot + 1) % len(playlist)

    # Khách bấm nút mua slot quảng cáo cá nhân → QR thật từ cloud, on_paid xếp lịch.
    print("\n--- Khách bấm nút mua slot quảng cáo cá nhân (50.000 đ) ---")
    app.request_qr(amount_vnd=50000)
    app.run()
