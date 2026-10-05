# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 InnoEdge
# Ví dụ Robot STEM Giao Hàng Tự Hành bằng MicroPython

import time
from innoedge import InnoBot

bot = InnoBot(version="1.0.0")

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
    bot.run()
    # Thử nghiệm giả lập một giao dịch nạp tiền VietQR
    print("\n--- Chạy thử nghiệm giả lập thanh toán VietQR ---")
    bot.simulate_payment(amount_vnd=20000, intent_id=88)
