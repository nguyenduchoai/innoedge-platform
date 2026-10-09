# Hướng Dẫn Triển Khai InnoEdge Đa Nền Tảng (Cross-Platform Matrix)
## Từ Vi Điều Khiển (MCU) Đến Máy Tính Nhúng Đơn Bo Mạch (Linux SBC)

InnoEdge được thiết kế theo kiến trúc chuẩn hóa giao thức mở (Open Protocol v1), cho phép chạy đồng nhất trên mọi thế hệ phần cứng IoT từ **chip vi điều khiển vài chục nghìn đồng** đến **máy tính nhúng công nghiệp mạnh mẽ**.

---

## 🗺️ Ma Trận Phần Cứng Hỗ Trợ

| Nền tảng | Dòng thiết bị tiêu biểu | Môi trường lập trình | Lưu trữ sổ cái | Ứng dụng phổ biến |
|---|---|---|---|---|
| **ESP-IDF** | ESP32-S3, ESP32, ESP32-C3 | C11, FreeRTOS | Phân vùng NVS Flash | Máy bán hàng nhỏ, máy giặt, trạm rửa xe, trạm sạc |
| **Arduino / PlatformIO** | ESP32, RP2040, STM32 | C++ (Thư viện `InnoEdge.h`) | NVS / EEPROM | Dự án Maker, học sinh, sinh viên, nguyên mẫu nhanh |
| **MicroPython** | ESP32, Raspberry Pi Pico | Python 3 (`innoedge.py`) | Flash File System | Robot STEM, giáo dục, xe tự hành học đường |
| **Raspberry Pi** | Pi 3B+, Pi 4B, Pi 5, Zero 2W, CM4 | Python, Go, Node.js, Electron | File SSD / eMMC / SQLite | Kiosk cảm ứng 15.6", máy bán vé, cây xăng tự động |
| **Banana Pi** | BPI-M2, BPI-M5, BPI-CM4 | Armbian, Python SDK | eMMC / SD Card | Kiosk công nghiệp chi phí tối ưu, trạm thu phí |
| **Orange Pi / Jetson** | Orange Pi 5, Jetson Nano | Linux ARM64, Python, C++ | NVMe / eMMC | Kiosk AI nhận diện khuôn mặt, trạm cân thông minh |

---

## 🔌 Sơ Đồ Chân Chuẩn 40-Pin Header (Raspberry Pi & Banana Pi)

Cả Raspberry Pi và Banana Pi đều chia sẻ chuẩn chân cắm GPIO 40-pin vật lý giống nhau. Khi đấu nối với mạch Relay và thiết bị ngoại vi:

```
  3V3 Power [ 1] [ 2] 5V Power (Cấp cho Relay)
   I2C1 SDA [ 3] [ 4] 5V Power
   I2C1 SCL [ 5] [ 6] GND Ground
      GPIO4 [ 7] [ 8] GPIO14 (UART TX)
        GND [ 9] [10] GPIO15 (UART RX)
     GPIO17 [11] [12] GPIO18 (PWM Servo)  <--- Relay 1 (Cà phê đen / Kênh 1)
     GPIO27 [13] [14] GND Ground          <--- Relay 2 (Trà sữa / Kênh 2)
     GPIO22 [15] [16] GPIO23
        3V3 [17] [18] GPIO24
```

---

## 🛠️ Cách Chọn Kiến Trúc Cho Dự Án Của Bạn

### 1. Dùng ESP32 (ESP-IDF / Arduino) khi:
* Chi phí phần cứng cần tối ưu dưới 100.000 đ – 200.000 đ.
* Máy bán nước, trạm rửa xe, máy giặt tự động chỉ cần màn hình LCD nhỏ (ST7789, OLED 0.96") hoặc nút bấm cơ.
* Yêu cầu khởi động tức thì trong 0.5 giây sau khi bật điện.
* Xem ví dụ: [examples/07-qr-payment](../examples/07-qr-payment) hoặc [arduino/InnoEdge](../arduino/InnoEdge).

### 2. Dùng Raspberry Pi / Banana Pi khi:
* Kiosk cần **màn hình cảm ứng lớn (10.1 inch, 15.6 inch, 21 inch HDMI)** với giao diện đồ họa mượt mà (video quảng cáo, âm thanh sống động).
* Kết nối nhiều thiết bị ngoại vi cổng USB: Đầu đọc mã vạch QR công nghiệp, máy in hóa đơn nhiệt USB, camera AI nhận diện khuôn mặt, cảm biến cân nặng qua cổng RS232/USB.
* Chạy ứng dụng giao diện Kiosk bằng Web (Electron, Chromium Kiosk Mode) hoặc Flutter/Qt.
* Xem ví dụ: [linux/python/examples/pi_vending_kiosk.py](../linux/python/examples/pi_vending_kiosk.py) hoặc example robot [linux/python/examples/jumper/](../linux/python/examples/jumper/).
