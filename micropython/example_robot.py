# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
# Ví dụ Robot STEM Giao Hàng Tự Hành bằng MicroPython

import time
from innoedge import InnoBot

# Nối cloud thật: điền WiFi + URL (vd chạy `go run ./tools/mock-cloud` trên máy tính).
# Để trống CLOUD_URL = chạy giả lập, không cần mạng.
WIFI_SSID, WIFI_PASSWORD = "", ""
CLOUD_URL = ""  # vd "ws://192.168.1.10:8080/ws/"


def make_cloud():
    if not CLOUD_URL:
        return None
    import network
    import ubinascii
    from innoedge_cloud import Cloud
    wlan = network.WLAN(network.STA_IF)
    wlan.active(True)
    if not wlan.isconnected():
        wlan.connect(WIFI_SSID, WIFI_PASSWORD)
        while not wlan.isconnected():
            time.sleep(0.5)
    mac = ubinascii.hexlify(wlan.config("mac")).decode().upper()
    return Cloud(CLOUD_URL, device_id=mac, fw_version="1.0.0")


bot = InnoBot(version="1.0.0", cloud=make_cloud())

# 1. Khi khách hàng quét VietQR trả tiền: Robot bắt đầu nhiệm vụ!
@bot.on_paid
def handle_paid(intent_id, amount_vnd):
    print(f"\n[EVENT] Đã nhận thanh toán {amount_vnd} đ! Bắt đầu hành trình giao hàng.")
    bot.speak("Cảm ơn quý khách! Robot bắt đầu di chuyển.")
    
    # Kiểm tra vật cản trước khi tiến
    dist = bot.get_distance()
    print(f"[SENSOR] Khoảng cách phía trước: {dist} cm")
    
    if dist < 15:
        print("[AVOID] Có vật cản! Robot rẽ phải để tránh.")
        bot.turn_right(speed=80, seconds=0.6)
    
    # Tiến về phía trước đến bàn khách
    bot.forward(speed=80, seconds=2.0)
    
    # Mở nắp thùng hàng bằng Servo để khách lấy đồ
    bot.speak("Đơn hàng của bạn đã tới. Mời bạn nhận đồ!")
    bot.set_servo(channel=1, angle=90)
    
    time.sleep(3) # Chờ 3 giây cho khách lấy đồ
    
    # Đóng nắp thùng hàng và kết thúc
    bot.set_servo(channel=1, angle=0)
    bot.speak("Chúc bạn một ngày tốt lành!")

# 2. Đăng ký lệnh điều khiển từ xa qua Cloud
@bot.command("bot_move")
def handle_move(params):
    direction = params.get("direction", "forward")
    sec = params.get("seconds", 1.0)
    if direction == "forward":
        bot.forward(speed=80, seconds=sec)
    elif direction == "backward":
        bot.backward(speed=80, seconds=sec)
    elif direction == "left":
        bot.turn_left(speed=80, seconds=sec)
    elif direction == "right":
        bot.turn_right(speed=80, seconds=sec)
    return {"status": "ok", "moved": direction}

if __name__ == "__main__":
    if bot.cloud:
        bot.run()  # chặn mãi, nhận thanh toán/lệnh thật từ cloud
    else:
        print("\n--- Chạy giả lập thanh toán VietQR (chưa đặt CLOUD_URL) ---")
        bot.simulate_payment(amount_vnd=20000, intent_id=88)
