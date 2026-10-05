# 14 — Digital Signage & Smart Billboard: Bảng Quảng Cáo Kỹ Thuật Số Tương Tác

[English](README.md) | [Tiếng Việt](README_vi.md)


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
