# InnoEdge Python SDK Cho Linux SBC (Raspberry Pi, Banana Pi, Orange Pi)

SDK chính thức dành cho các máy tính đơn bo mạch (Single-Board Computers) chạy hệ điều hành Linux (Raspberry Pi OS, Armbian, Ubuntu, Debian) để phát triển **Kiosk Bán Hàng Cảm Ứng, Máy Bán Nước Tự Động, Tủ Locker Thông Minh, Robot Dịch Vụ**.

---

## 🌟 Tính Năng

* **Hỗ Trợ Toàn Diện SBC:** Chạy mượt mà trên **Raspberry Pi (3/4/5/Zero 2W)**, **Banana Pi (BPI-M2/M5)**, **Orange Pi**, **Jetson**, và máy tính nhúng Linux x86_64/ARM64.
* **Tự Động Nhận Diện Phần Cứng:** Đọc mã Serial phần cứng CPU trên Pi hoặc `/etc/machine-id` để tạo mã định danh duy nhất (`PI-XXXXXXXX`).
* **Sổ Cái Bền Vững (Durable Ledger):** Lưu trữ lịch sử giao dịch và High-Watermark `commandId` vào đĩa cứng (`~/.innoedge/ledger.json`) — không bao giờ mất dữ liệu hay chạy trùng lệnh khi mất điện đột ngột.
* **Module Điều Khiển GPIO & Relay:** Tự động phát hiện `libgpiod` (chuẩn Linux hiện đại trên Raspberry Pi OS Bookworm & Armbian) hoặc `sysfs`, kèm trần an toàn phần cứng (`max_pulse_sec`).
* **Hỗ Trợ VietQR Đa Cổng:** Tương thích trực tiếp với các cổng thanh toán Việt Nam (SePAY, PayOS, Pay2S, Tingee).

---

## 🚀 Hướng Dẫn Cài Đặt

Trên Raspberry Pi hoặc Banana Pi:
```bash
# 1. Cài đặt thư viện
cd innoedge-sdk-esp32/linux/python
pip install .

# (Tùy chọn) Cài đặt libgpiod nếu dùng Raspberry Pi OS mới:
sudo apt install -y python3-libgpiod
```

---

## 💻 Mã Nguồn Mẫu (Kiosk 2 Kênh Relay)

```python
from innoedge import InnoEdge, PinRelay

# Khởi tạo client kết nối Cloud
app = InnoEdge(cloud_url="ws://127.0.0.1:8080/ws")

# Khởi tạo 2 Relay (GPIO 17 & 27)
relay_coffee = PinRelay(pin=17, active_high=False, max_pulse_sec=10)
relay_milktea = PinRelay(pin=27, active_high=False, max_pulse_sec=10)

# Khi khách chọn món trên màn hình cảm ứng:
def buy_coffee():
    app.request_qr(amount_vnd=25000)

# Khi mã QR được tạo từ Cloud:
@app.on_qr
def on_qr(payload, amount, ref_code, expires_sec, intent_id):
    # Vẽ mã QR lên màn hình Kiosk (sử dụng Tkinter, PyQt hoặc Electron)
    print(f"Mời quét QR: {payload}")

# Tín hiệu DUY NHẤT được phép nhả hàng: on_paid
@app.on_paid
def on_paid(intent_id, amount_vnd):
    print(f"Đã nhận {amount_vnd} đ. Bắt đầu rót đồ uống!")
    relay_coffee.pulse(seconds=5)

# Nhận lệnh điều khiển từ xa
@app.command("dispense")
def cmd_dispense(params):
    relay_coffee.pulse(seconds=3)
    return {"status": "ok"}

app.run()
```
