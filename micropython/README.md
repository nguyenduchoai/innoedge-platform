# InnoEdge MicroPython SDK & InnoBot

Thư viện MicroPython chính thức dành cho học sinh, sinh viên, giáo viên và Maker phát triển **Robot STEM**, máy bán hàng tự động và thiết bị IoT thương mại bằng ngôn ngữ Python.

> ⚠️ **Hiện trạng:** thư viện này là HAL robot STEM (động cơ, siêu âm, servo) cộng
> chỗ đăng ký callback. Nó **CHƯA kết nối cloud**: `on_paid` chỉ chạy khi
> bạn gọi `simulate_payment()`, không có giao dịch thật nào tới. Cần thanh toán / lệnh từ
> xa thật trên ESP32 thì dùng SDK ESP-IDF (`components/innoedge`) hoặc thư viện Arduino.

---

## 🚀 Tính Năng

* **Điều Khiển Động Cơ 2 Bánh:** Hỗ trợ mạch cầu H L298N, TB6612FNG (`forward`, `backward`, `turn_left`, `turn_right`, `stop`).
* **Đo Khoảng Cách Siêu Âm:** Cảm biến HC-SR04 tự động tính toán thời gian xung và khoảng cách (cm).
* **Điều Khiển Góc Servo:** Đóng/mở nắp thùng hàng, gắp vật thể (`set_servo(channel, angle)`).
* **Đăng ký sự kiện thanh toán:** `@bot.on_paid` (chưa nối cloud — xem Hiện trạng).
* **Đăng ký lệnh:** decorator `@bot.command("bot_move")` (chưa nối cloud — xem Hiện trạng).

---

## 🛠️ Hướng Dẫn Sử Dụng

### 1. Nạp MicroPython vào ESP32
Tải firmware MicroPython cho ESP32-S3 từ trang chủ [micropython.org](https://micropython.org/download/ESP32_GENERIC_S3/) và nạp vào chip bằng `esptool.py` hoặc [InnoEdge Web Flasher](../web-flasher/index.html).

### 2. Tải Thư Viện Lên Thiết Bị
Sử dụng công cụ `mpremote` hoặc Thonny IDE để chép file `innoedge.py` vào thư mục gốc của ESP32:
```bash
mpremote fs cp innoedge.py :innoedge.py
mpremote fs cp example_robot.py :main.py
```

### 3. Mã Nguồn Mẫu (Robot Giao Hàng Dịch Vụ)
```python
from innoedge import InnoBot

bot = InnoBot()

@bot.on_paid
def on_customer_paid(intent_id, amount_vnd):
    print(f"Nhận {amount_vnd} đ -> Robot bắt đầu đi giao hàng!")
    bot.speak("Cảm ơn bạn! Đơn hàng đang được chuyển tới.")
    
    # Né vật cản nếu có
    if bot.get_distance() < 15:
        bot.turn_right(seconds=0.5)
        
    bot.forward(seconds=2.0)
    bot.set_servo(channel=1, angle=90) # Mở nắp thùng đồ

bot.run()
```
