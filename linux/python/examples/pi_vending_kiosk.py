#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
# Ví dụ Kiosk Bán Nước Tự Động trên Raspberry Pi & Banana Pi

import time
import logging
from innoedge import InnoEdge, PinRelay

logging.basicConfig(level=logging.INFO, format="%(asctime)s [%(levelname)s] %(message)s")

# 1. Khởi tạo InnoEdge trên Raspberry Pi / Banana Pi
app = InnoEdge(cloud_url="ws://127.0.0.1:8080/ws", fw_version="1.0.0")

# 2. Cấu hình 2 Relay điều khiển vòi rót qua chân GPIO
# (Trên Raspberry Pi & Banana Pi: GPIO 17 = chân vật lý 11, GPIO 27 = chân vật lý 13)
relay_coffee = PinRelay(pin=17, active_high=False, max_pulse_sec=10) # Cà phê đen (20.000 đ)
relay_milktea = PinRelay(pin=27, active_high=False, max_pulse_sec=10) # Trà sữa (25.000 đ)

# 3. Khi khách chọn món trên màn hình cảm ứng: Gọi hàm xin mã VietQR
def on_customer_select_item(item_name, price_vnd):
    print(f"\n[KIOSK] Khách chọn '{item_name}' ({price_vnd} đ) -> Đang tạo VietQR...")
    app.request_qr(price_vnd)

# 4. Khi nhận được chuỗi VietQR từ Cloud: Hiển thị lên màn hình Kiosk
@app.on_qr
def show_qr_on_screen(payload, amount, ref_code, expires_sec, intent_id):
    print("=" * 60)
    print(f" MỜI QUÉT MÃ VIETQR THANH TOÁN: {amount:,} đ")
    print(f" Mã đơn hàng: {ref_code} | Hết hạn sau: {expires_sec}s")
    print(f" Chuỗi QR Payload: {payload}")
    print(" (Trên Raspberry Pi: vẽ mã QR lên màn hình HDMI/Touchscreen)")
    print("=" * 60)

# 5. Tín hiệu DUY NHẤT được phép nhả hàng: Ngân hàng đã xác nhận tiền về!
@app.on_paid
def handle_payment_success(intent_id, amount_vnd):
    print(f"\n🎉 [TIỀN VỀ TÀI KHOẢN] Ngân hàng xác nhận đã nhận {amount_vnd:,} đ!")
    
    if amount_vnd <= 20000:
        print("[ROBOT] Kích hoạt Relay 1 (Cà phê đen) trong 5 giây...")
        relay_coffee.pulse(seconds=5)
    else:
        print("[ROBOT] Kích hoạt Relay 2 (Trà sữa) trong 7 giây...")
        relay_milktea.pulse(seconds=7)

    app.publish_event("kiosk_sale_done", {
        "intent_id": intent_id,
        "amount_vnd": amount_vnd,
        "status": "success"
    })

# 6. Đăng ký lệnh từ xa qua InnoEdge Command Bus
@app.command("dispense")
def cmd_dispense(params):
    channel = params.get("channel", 1)
    sec = params.get("seconds", 3)
    if channel == 1:
        relay_coffee.pulse(seconds=sec)
    elif channel == 2:
        relay_milktea.pulse(seconds=sec)
    return {"status": "ok", "channel": channel, "seconds": sec}

if __name__ == "__main__":
    print(f"🚀 Kiosk Khởi động trên Linux SBC ({app.device_id})")
    app.start()

    # Mô phỏng một giao dịch khách bấm chọn món và thanh toán
    print("\n--- MÔ PHỎNG LUỒNG GIAO DỊCH KIOSK ---")
    on_customer_select_item("Cà phê sữa đá", 25000)

    # Giả lập gói tin QR về từ Cloud
    app.dispatch_message('{"type":"qr","payload":"00020101021238540010A00000072701240006970422...","amount":25000,"refCode":"PI8899","expiresSec":300,"intentId":77}')

    # Giả lập Webhook ngân hàng báo tiền về
    app.dispatch_message('{"type":"paid","amount":25000,"intentId":77}')

    time.sleep(1)
    app.stop()
