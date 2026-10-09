# InnoEdge MicroPython SDK & InnoBot

Thư viện MicroPython chính thức dành cho học sinh, sinh viên, giáo viên và Maker phát triển **Robot STEM**, máy bán hàng tự động và thiết bị IoT thương mại bằng ngôn ngữ Python.

> **Nối cloud:** `innoedge_cloud.py` nói PROTOCOL-v1 qua WebSocket tự viết (chỉ
> dùng `socket`/`ssl`/`hashlib` có sẵn). Cùng cam kết với SDK ESP-IDF: tiền ghi flash
> trước khi gửi, lệnh chạy tối đa một lần, `on_paid` không bao giờ hai lần. Đã test
> trên CPython với mock-cloud; **chưa thử trên board MicroPython thật**.

```python
from innoedge import InnoBot
from innoedge_cloud import Cloud

cloud = Cloud("ws://192.168.1.10:8080/ws/", device_id="<MAC>", fw_version="1.0.0")
bot = InnoBot(cloud=cloud)

@bot.on_paid
def paid(intent_id, amount_vnd):
    bot.forward(seconds=2)

bot.run()   # chặn mãi: nhận lệnh + thanh toán thật
```

Chép cả hai file lên board: `mpremote fs cp innoedge.py innoedge_cloud.py :`.
Xem `example_robot.py` (đặt `WIFI_SSID`, `CLOUD_URL`).

---

## 🚀 Tính Năng

* **Điều Khiển Động Cơ 2 Bánh:** Hỗ trợ mạch cầu H L298N, TB6612FNG (`forward`, `backward`, `turn_left`, `turn_right`, `stop`).
* **Đo Khoảng Cách Siêu Âm:** Cảm biến HC-SR04 tự động tính toán thời gian xung và khoảng cách (cm).
* **Điều Khiển Góc Servo:** Đóng/mở nắp thùng hàng, gắp vật thể (`set_servo(channel, angle)`).
* **Sự kiện thanh toán VietQR:** `@bot.on_paid` — nhận `payment_paid` thật khi gắn `cloud=`.
* **Lệnh từ xa:** `@bot.command("bot_move")` — chống trùng theo `commandId`.

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
