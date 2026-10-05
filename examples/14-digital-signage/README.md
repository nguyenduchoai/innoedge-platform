<a id="top"></a>
# 14 — Digital Signage: Bảng quảng cáo kỹ thuật số tương tác (Smart Billboard)

> 🇻🇳 **Tài liệu Tiếng Việt** (toàn bộ nội dung bên dưới) | [🇬🇧 English Documentation](#english)

Ứng dụng thương mại hoàn chỉnh biến màn hình LED ma trận (HUB75), màn hình TFT SPI hoặc màn hình TV HDMI thành một **Bảng quảng cáo kỹ thuật số thông minh** kết nối Cloud InnoEdge.

---

## 💡 Điểm Độc Đáo Của InnoEdge Digital Signage

1. **Quản Lý Tập Trung Từ Xa (Centralized CMS):**
   * Đẩy danh sách phát quảng cáo (playlist), văn bản chạy chữ, hình ảnh từ xa qua Cloud.
   * Cập nhật tức thời mà không cần đến tận nơi cắm USB.
2. **Bằng Chứng Phát Sóng (Proof of Play - POP):**
   * Mỗi khi một quảng cáo được chiếu đủ thời lượng, thiết bị tự động gửi sự kiện `proof_of_play` lên Cloud để thống kê số lượt xem thực tế phục vụ đối soát doanh thu với khách hàng.
3. **Chế Độ Ngắt Khẩn Cấp (Emergency Override):**
   * Lệnh `signage_emergency` từ Cloud lập tức ngắt toàn bộ quảng cáo thương mại để phát thông điệp cảnh báo an ninh, tìm trẻ lạc hoặc báo động sơ tán hỏa hoạn.
4. **Mô Hình Kinh Doanh Tự Phục Vụ (Self-Service Ad Booking):**
   * Người xem đứng trước bảng LED có thể quét mã VietQR (50.000 đ) để mua ngay một slot chiếu lời chúc mừng sinh nhật, lời nhắn tỏ tình hoặc giới thiệu quán ăn mini trong 5 phút!

---

## 🛠️ Phần Cứng Đề Xuất

* **Vi điều khiển:** ESP32-S3 DevKit (chạy LED Matrix HUB75 hoặc màn hình TFT ST7789).
* **Hoặc Máy tính nhúng SBC:** Raspberry Pi 4/5 hoặc Banana Pi (nối cáp HDMI ra màn hình TV 32–65 inch).
* **Đèn LED trạng thái:** GPIO 2.
* **Nút bấm:** GPIO 0 (mô phỏng thao tác mua slot quảng cáo tự phục vụ).

---

## 🚀 Hướng Dẫn Chạy Thử Nghiệm

### Bước 1: Khởi động Mock Cloud
```bash
cd tools/mock-cloud
go run .
```

### Bước 2: Nạp Firmware hoặc Chạy Ứng Dụng
```bash
cd examples/14-digital-signage
idf.py set-target esp32s3
idf.py menuconfig   # Cấu hình WiFi và IP mock-cloud
idf.py build flash monitor
```

---

## 🎮 Thao Tác Kiểm Chứng

1. Màn hình tự động chạy luân phiên các slot quảng cáo và báo cáo Proof-of-Play:
   ```
   I signage: ╔══════════════════════════════════════════════════════════════╗
   I signage: ║ [QUẢNG CÁO] Khuyến Mãi                                    ║
   I signage: ║   >> Siêu Sale Mùa Hè - Giảm 50% toàn bộ sản phẩm!        ║
   I signage: ╚══════════════════════════════════════════════════════════════╝
   I innoedge: event published: proof_of_play {"ad_title":"Khuyến Mãi","duration":6,"total_views":1}
   ```
2. Thử nghiệm mua slot quảng cáo cá nhân (Nhấn nút **BOOT**):
   ```
   I signage: Khách ấn nút mua lượt phát sóng (50.000 đ) -> Tạo VietQR...
   I signage: Mã VietQR Mua Lượt Phát Sóng: 50000 đ | Ref: GTMOCKD...
   ```
3. Giả lập thanh toán thành công trên Web Console (`http://localhost:8080/dashboard/`):
   ```
   I signage: ✓ XÁC NHẬN TIỀN VỀ: 50000 đ -> KÍCH HOẠT QUẢNG CÁO CỦA KHÁCH!
   I signage: ╔══════════════════════════════════════════════════════════════╗
   I signage: ║ [QUẢNG CÁO] Khách Hàng #...                               ║
   I signage: ║   >> Chúc mừng sinh nhật! Chúc bạn ngập tràn niềm vui!    ║
   I signage: ╚══════════════════════════════════════════════════════════════╝
   ```
4. Thử kích hoạt lệnh báo động khẩn cấp từ xa:
   ```bash
   # Gửi lệnh từ Mock Cloud hoặc Dashboard
   {"action":"signage_emergency", "params":{"message":"BÁO CHÁY TẦNG 2 - DI TẢN GẤP!"}}
   ```
   Màn hình lập tức chuyển đỏ sang trạng thái khẩn cấp và ngắt mọi quảng cáo!

---

<a id="english"></a>
## 🇬🇧 English Documentation

> [🇻🇳 Quay lại Tiếng Việt](#top)

A complete commercial application that turns LED matrix panels (HUB75), SPI TFT displays, or HDMI television screens into a **cloud-managed Smart Digital Signage Billboard** powered by the InnoEdge Cloud.

---

## Key Features

1. **Centralized Remote CMS:**
   * Distribute playlists, text tickers, and media schedules remotely over WebSocket.
   * Instant updates without physical USB drive swaps.
2. **Proof of Play (POP) Telemetry:**
   * Each time an advertisement slot completes its duration, the device publishes an authenticated `proof_of_play` event to the cloud for advertiser billing and verification audits.
3. **Emergency Priority Override:**
   * An emergency command (`signage_emergency`) from the cloud immediately preempts commercial ad loops to display public security alerts, evacuation sirens, or missing child notifications.
4. **Self-Service Ad Booking:**
   * Onlookers can scan an on-screen QR code (e.g., 50,000 VND) to purchase a 5-minute personal ad slot (birthday greeting, proposal message, or local business promotion) directly on the screen!

---

## Hardware Options

* **Microcontroller:** ESP32-S3 DevKit driving a HUB75 LED Matrix or ST7789 TFT screen.
* **Or Single Board Computer (SBC):** Raspberry Pi 4/5 or Banana Pi driving a 32–65 inch TV via HDMI (using `linux/python/examples/pi_digital_signage.py`).
* **Onboard Buttons/LEDs:** BOOT button (GPIO0) simulates user ad booking; Status LED on GPIO2.

---

## How to Run

### Step 1: Start Mock-Cloud
```bash
cd tools/mock-cloud && go run .
```

### Step 2: Build & Flash
```bash
cd examples/14-digital-signage
idf.py set-target esp32s3
idf.py menuconfig
idf.py build flash monitor
```

The screen rotates through scheduled media slots and periodically dispatches Proof-of-Play telemetry back to the cloud console.
