# 12 — Meta Muse Gadget: Kiosk AI Avatar & Bán Hàng Tự Động

Tích hợp **Meta Muse Gadget SDK** ([facebookincubator/muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk)) vào nền tảng **InnoEdge**.

Ví dụ này minh họa cách biến một bo vi điều khiển ESP32-S3 thông thường thành một **Kiosk / Máy bán hàng tự động thông minh** có Avatar biểu cảm và tương tác giọng nói, đồng thời sở hữu đầy đủ hạ tầng thanh toán ngân hàng VietQR và bảo vệ tiền tệ cấp công nghiệp.

---

## 💡 Tại sao kết hợp Meta Muse Gadget với InnoEdge?

| Đặc tính | Meta Muse Gadget SDK | InnoEdge SDK | Khi kết hợp lại |
|---|---|---|---|
| **Mục đích** | AI Assistant cá nhân, nhận diện giọng nói, Avatar đồ họa | Hạ tầng thanh toán tiền thật, quản lý thiết bị IoT thương mại | **Kiosk AI bán hàng hoàn chỉnh** |
| **Giao tiếp khách hàng** | Hội thoại giọng nói tự nhiên, Avatar biểu cảm | Màn hình QR động, còi báo | Khách trò chuyện gọi món tự nhiên, màn hình hiện mã VietQR |
| **Bảo vệ dòng tiền** | ❌ Không có |  Sổ cái NVS bền vững, chống trùng lệnh (`commandId`), chỉ `on_paid` mới nhả hàng | **Tuyệt đối an toàn về tiền**, không bao giờ nhả trùng |
| **Cổng thanh toán** | ❌ Không có |  Tích hợp sẵn SePAY, PayOS, Pay2S, Tingee | Thanh toán chuyển khoản ngân hàng Việt Nam tức thì |
| **An toàn phần cứng** | ❌ Không có |  Trần an toàn relay (`RELAY_MAX_SECONDS`), task nền không block | Phần cứng không lo cháy relay hay kẹt bơm |

```
    Khách nói: "Cho tôi 1 ly Cà phê sữa đá"
                       │
                       ▼
       [ Meta Muse Gadget (Mic / ASR) ]
                       │
                       ▼
       [ Meta Muse Cloud AI Agent ]
         → Phân tích ý định & gọi Tool:
           request_payment(amount_vnd=25000, item_name="Cà phê sữa")
                       │
                       ▼ (Command Bus)
         [ InnoEdge SDK trên ESP32 ]
           → innoedge_request_qr(25000)
                       │
                       ▼
         [ InnoEdge Cloud / VietQR Gateways ]
           → Sinh mã VietQR (SePAY / PayOS / Pay2S / Tingee)
                       │
                       ▼ on_qr()
         [ Hiển thị mã VietQR lên màn hình cạnh Muse Avatar ]
                       │
             Khách quét app ngân hàng trả tiền
                       │
                       ▼ Webhook
         [ on_paid() báo về ESP32 ]
           → Kích Relay rót cà phê (an toàn 7s)
           → Ghi nhận giao dịch vào sổ cái NVS
           → Muse Avatar phát biểu cảm cảm ơn và phát loa: "Cảm ơn bạn!"
```

---

## 🛠️ Phần cứng đề xuất

* **Vi điều khiển:** ESP32-S3 DevKitC-1 (N8R8 hoặc N16R8 khuyên dùng có PSRAM).
* **Màn hình:** ST7789 240x240 hoặc GC9A01 tròn hoặc AMOLED (qua giao tiếp SPI/QPSI).
* **Relay điều khiển:** Module 2 relay cách ly quang (Optocoupler):
  * **Relay 1 (GPIO 4):** Kênh 1 (Cà phê đen - 20.000 đ)
  * **Relay 2 (GPIO 5):** Kênh 2 (Cà phê sữa - 25.000 đ)
* **Nút bấm thao tác:** Nút BOOT trên bo (GPIO 0).
* **Âm thanh (tùy chọn theo Muse Gadget SDK):** Mic INMP441 + Mạch khuếch đại MAX98357A.

---

## 🚀 Hướng dẫn chạy thử nghiệm

### Cách 1: Thử nghiệm với Mock Cloud (Không cần tài khoản Meta hay khóa API)

InnoEdge cung cấp sẵn mock-cloud tích hợp persona `muse`:

**Terminal 1 — Khởi chạy Mock Cloud với Persona Muse:**
```bash
cd tools/mock-cloud
export ANTHROPIC_API_KEY=sk-ant-...   # Tùy chọn nếu muốn LLM tự trò chuyện
go run . -ai -persona muse
```

**Terminal 2 — Nạp Firmware cho ESP32-S3:**
```bash
cd examples/12-muse-gadget
idf.py set-target esp32s3
idf.py menuconfig   # InnoEdge SDK -> Cấu hình WiFi và WebSocket URL trỏ về IP máy tính chạy mock-cloud (port 8080)
idf.py build flash monitor
```

**Thao tác thử nghiệm:**
1. Bấm nút **BOOT** trên bo:
   ```
   I muse-gadget: Khách ấn nút chọn món: Cà phê sữa đá (25.000 đ) -> Tạo VietQR...
   I muse-gadget: 🤖 [MUSE AVATAR] -> THINKING (Đang suy nghĩ) | "Đang khởi tạo giao dịch VietQR 25.000 đ..."
   I muse-gadget: Mã VietQR đã sẵn sàng: 25000 đ | Ref: GTMOCKD...
   I muse-gadget: 🤖 [MUSE AVATAR] -> PAYMENT_PENDING (Chờ quét VietQR) | "Mời bạn quét VietQR 25000 đ để nhận món"
   ```
2. Trên Web Console `http://localhost:8080/dashboard/`:
   Nhấp nút **Simulate Payment (25,000 đ)** hoặc gửi Webhook giả lập từ SePAY / PayOS / Pay2S / Tingee.
3. Firmware lập tức nhận `on_paid`:
   ```
   I muse-gadget: ✓ XÁC NHẬN TIỀN VỀ: 25000 đ (intent=...)
   I muse-gadget: 🤖 [MUSE AVATAR] -> DISPENSING (Đang nhả hàng / rót) | "Thanh toán thành công! Đang pha chế..."
   I muse-gadget: Kích hoạt Kênh 2 (Cà phê sữa) trong 7 giây
   ...
   I muse-gadget: Relay Kênh 2 đã tự ngắt an toàn.
   I muse-gadget: 🤖 [MUSE AVATAR] -> HAPPY (Cảm ơn quý khách!) | "Đã chuẩn bị xong món Cà phê sữa!"
   ```

---

### Cách 2: Tích hợp trực tiếp với Meta Muse App thật

Khi kết nối với hệ sinh thái chính thức của Meta:
1. Đăng ký nhận SDK Token tại cổng nhà phát triển Meta: `https://gadgets.muse.ai/`.
2. Mở ứng dụng **Meta Muse** trên điện thoại (iOS / Android) -> Bật **Developer Mode**.
3. Tiến hành ghép nối thiết bị qua Bluetooth/WiFi.
4. Meta Muse Cloud sẽ đóng vai trò AI Agent trung tâm, gửi lệnh `request_payment` và `dispense` xuống thiết bị thông qua InnoEdge Command Bus.
5. InnoEdge đảm bảo mọi giao dịch thanh toán VietQR đều được đối soát độc lập, chống trùng lặp và lưu trữ bền vững trong bộ nhớ flash NVS.

---

## 🔒 3 Quy tắc an toàn bắt buộc khi kết hợp AI với thiết bị có tiền

1. **Tuyệt đối không nhả hàng chỉ dựa vào lời nói của AI:**
   LLM có thể bị ảo giác (hallucination) hoặc bị prompt injection lừa rằng "khách đã trả tiền". Thiết bị CHỈ kích hoạt relay khi sự kiện `on_paid` do InnoEdge SDK xác nhận qua chữ ký số hoặc webhook ngân hàng hợp lệ.
2. **Trần an toàn phần cứng (`RELAY_MAX_SECONDS`):**
   Thời gian đóng relay được giới hạn cứng tại firmware (tối đa 10 giây). Kể cả khi cloud mất kết nối hoặc gửi sai tham số, relay vẫn sẽ tự ngắt.
3. **Chống trùng lệnh bằng `commandId`:**
   Mọi lệnh điều khiển cơ cấu nhả hàng từ AI Agent đều phải mang `commandId`. InnoEdge SDK ghi nhận watermark vào NVS trước khi thực thi để ngăn chặn việc lặp lại thao tác khi mạng chập chờn.
