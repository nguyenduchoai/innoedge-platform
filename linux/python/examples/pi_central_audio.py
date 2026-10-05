#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
# Hệ Thống Loa Thông Báo & Âm Thanh Tập Trung Đa Vùng trên Raspberry Pi & Banana Pi

import sys
import os
import time
import logging

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..")))
from innoedge import InnoEdge

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")

app = InnoEdge(cloud_url="ws://127.0.0.1:8080/ws", fw_version="1.0.0")

my_zone = 1 # Vùng 1: Tầng 1 / Quầy thu ngân
current_mode = "BGM" # BGM | PAGING | EMERGENCY
current_volume = 40

@app.command("audio_announce")
def cmd_announce(params):
    global current_mode, current_volume
    target_zone = params.get("zone", 0)
    if target_zone != 0 and target_zone != my_zone:
        return {"status": "skipped", "reason": f"not for zone {my_zone}"}

    msg = params.get("message", "Kính mời quý khách...")
    sec = params.get("seconds", 3)

    print("\n" + "=" * 60)
    print(f"🔔 [DING-DONG] NGẮT NHẠC NỀN -> PHÁT THÔNG BÁO ƯU TIÊN (ZONE {my_zone})")
    print(f"   🗣️ Nội dung: \"{msg}\"")
    print(f"   🔊 Âm lượng: 85% (Thời lượng: {sec}s)")
    print("=" * 60)

    current_mode = "PAGING"
    current_volume = 85
    time.sleep(sec)

    print(f"\n✓ Hết thông báo -> Tự động khôi phục phát nhạc nền BGM (Âm lượng {40}%).")
    current_mode = "BGM"
    current_volume = 40
    return {"status": "announced", "zone": my_zone}

@app.command("audio_emergency")
def cmd_emergency(params):
    global current_mode, current_volume
    active = params.get("active", True)
    if active:
        current_mode = "EMERGENCY"
        current_volume = 100
        print("\n🚨🚨🚨 [HỎA HOẠN] CÒI BÁO CHÁY TẬP TRUNG HÚ 100% ÂM LƯỢNG! 🚨🚨🚨")
    else:
        current_mode = "BGM"
        current_volume = 40
        print("\n✓ Đã tắt còi báo cháy, trở về phát nhạc nền.")
    return {"emergency": active}

@app.on_paid
def on_paid(intent_id, amount_vnd):
    print(f"\n🎵 [JUKEBOX PAID] Nhận {amount_vnd:,} đ qua VietQR -> Xếp bài hát yêu thích vào danh sách phát!")

if __name__ == "__main__":
    print(f"🚀 Khởi chạy Hệ Thống IP Audio Đa Vùng (Zone {my_zone}) trên Linux SBC ({app.device_id})")
    app.start()

    print(f"🎵 [LOA ĐANG PHÁT] Nhạc nền du dương (Âm lượng {current_volume}%)")

    # Giả lập lệnh phát thông báo ưu tiên từ Cloud
    print("\n--- Giả lập Cloud gửi lệnh phát thông báo khẩn cấp ---")
    app.dispatch_message('{"type":"command","commandId":1,"action":"audio_announce","params":{"message":"Kính mời khách hàng số 102 tới quầy nhận nước","seconds":2,"zone":1}}')

    # Giả lập khách quét VietQR nạp tiền order bài hát
    app.dispatch_message('{"type":"paid","amount":10000,"intentId":555}')

    time.sleep(1)
    app.stop()
