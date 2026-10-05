# InnoEdge Open Hardware Reference Design
## Chuẩn thiết kế mạch điện cho thiết bị IoT vận hành bằng tiền

Tài liệu này cung cấp sơ đồ nguyên lý, tiêu chuẩn thiết kế chống nhiễu công nghiệp và danh sách linh kiện (BOM) để các kỹ sư và maker chế tạo bo mạch điều khiển thương mại tương thích 100% với **InnoEdge SDK**.

---

## ⚡ 1. Sơ đồ khối tổng thể (Block Diagram)

```
       [ Nguồn vào 12V–24V DC ] (Từ nguồn tổ ong / máy bán hàng)
                  │
        ┌─────────┴─────────┐
        ▼                   ▼
 ┌──────────────┐   ┌──────────────┐
 │  Nguồn 5V    │   │ Nguồn 3.3V   │  (Cấp riêng cho ESP32 & Logic)
 │ (Buck LM2596/│   │ (LDO AMS1117/│
 │   MP1584)    │   │  ME6211)     │
 └──────┬───────┘   └──────┬───────┘
        │                  │
        ▼                  ▼
┌──────────────────────────────────────┐
│       Bộ vi điều khiển trung tâm     │
│       ESP32-S3 / ESP32-C3 DevKit     │
└───────┬──────────────┬───────────────┘
        │              │
 ┌──────▼──────┐┌──────▼──────┐┌──────────────┐┌──────────────┐
 │ Đầu vào xung││ Đầu ra Relay││ Cổng RS485 / ││ Khe cắm SIM  │
 │ Coin / Bill ││ Cách ly Opto││ Màn hình LCD ││ Module 4G    │
 │ (Opto PC817)││ (ULN2003)   ││ (SP3485)     ││ (A7670/Air780)
 └─────────────┘└─────────────┘└──────────────┘└──────────────┘
```

---

## 🔌 2. Thiết kế chi tiết từng khối mạch

### Khối A: Nguồn cấp công nghiệp (Power Stage)

Máy coin-op, máy rửa xe và bán hàng tự động thường dùng nguồn tổ ong **12V hoặc 24V DC**. Khi relay, motor hoặc cuộn hút (solenoid) ngắt, điện áp cảm ứng ngược có thể vọt lên **60V–100V**, dễ đánh thủng chip nếu không bảo vệ:

* **Cầu chì tự phục hồi (PPTC Resettable Fuse):** 2A / 30V đặt ở đầu cực dương `VCC_IN`.
* **Diode chống ngược cực (Schottky):** SS34 (3A / 40V) hoặc SS54 mắc nối tiếp hoặc phân cực ngược song song.
* **Diode triệt áp đột biến (TVS Diode):** SMAJ24A hoặc SMBJ28A song song với nguồn vào để kẹp các đỉnh xung điện áp cao áp từ motor.
* **Hạ áp 12V/24V xuống 5V:** Dùng IC nguồn xung hạ áp (Buck Regulator) như **MP1584EN** hoặc **XL4015** (hiệu suất > 90%, không bị nóng như 7805).
* **Hạ áp 5V xuống 3.3V:** Dùng **AMS1117-3.3** hoặc **ME6211** kèm 2 tụ lọc: tụ gốm 100nF (lọc nhiễu cao tần) và tụ nhôm 100µF/16V (ổn định dòng đỉnh khi ESP32 phát WiFi/BLE).

---

### Khối B: Đầu vào đếm xung tiền (Coin Acceptor & Bill Validator)

Các đầu đọc tiền xu (TB-71, TW-130) và đầu đọc tiền giấy (ICT, BV20, CashCode) hoạt động ở nguồn 12V và xuất tín hiệu xung ra ở dạng **Open-Collector (cực thu hở NPN)**:

> [!CAUTION]
> **Tuyệt đối không nối trực tiếp chân COIN/BILL OUT vào ESP32!** Xung 12V sẽ làm cháy chân GPIO lập tức. Bắt buộc phải qua cách ly quang (Optocoupler).

**Sơ đồ nguyên lý cách ly Opto PC817:**

```
  12V ─────[ Điện trở 1kΩ, 1/4W ]───┐
                                    │
                              ┌─────┴─────┐
                              │  Anode    │ (Chân 1)
  Đầu đọc tiền (Xung âm) ────┤  Cathode  │ (Chân 2)
  (Chân COIN / BILL OUT)      └───────────┘
                                 PC817
                              ┌───────────┐
  ESP32 (GPIO ngắt) ──────────┤ Collector │ (Chân 4)
           │                  │  Emitter  │ (Chân 3)
     [Trở treo 10kΩ]          └─────┬─────┘
           │                        │
         +3.3V                     GND (Mass ESP32)
```

* **Lọc nhiễu phần cứng:** Mắc thêm 1 tụ gốm `100nF` song song giữa Collector và Emitter của PC817 để dập các tia lửa điện từ rơ le xung quanh làm nhảy sai số xu.
* **Phần mềm (Firmware):** Debounce thời gian tối thiểu giữa 2 xung là **30ms – 50ms** (đã tích hợp sẵn trong `innoedge_hw`).

---

### Khối C: Đầu ra điều khiển Relay (Tải công suất)

Dùng để nhả tiền (Coin Hopper), đóng mở van nước máy rửa xe, hoặc cấp nguồn sạc xe điện:

* **Mạch kích Relay:** Dùng transistor NPN (SS8050) hoặc IC đệm **ULN2003 / ULN2803** kích cuộn hút 12V/5V.
* **Diode dập tia lửa (Flyback Diode):** Bắt buộc mắc **1N4007** ngược cực song song với cuộn hút relay để dập sức điện động cảm ứng ngược khi relay ngắt.
* **Tiếp điểm Relay (NO/COM):**
  * Tải cảm ứng (Motor bơm, solenoid 220V): Mắc thêm **Mạch Snubber RC** (Tụ 0.1µF 400V + Điện trở 100Ω 2W) song song giữa 2 tiếp điểm Relay để chống hồ quang dính tiếp điểm.

---

### Khối D: Giao tiếp Modem 4G LTE Cat-1 (SIMCOM A7670 / Air780E)

Dành cho thiết bị lắp đặt ngoài trời không có WiFi:

* **Nguồn nuôi module 4G:** Đỉnh dòng khi truyền sóng 4G có thể lên tới **2A**. Cần đặt tụ hóa dung lượng lớn `1000µF` hoặc `470µF Tantalum` sát chân `VBAT` của module 4G.
* **Giao tiếp UART:**
  * `ESP32 TX` (GPIO 17) ──> `4G Module RXD`
  * `ESP32 RX` (GPIO 18) <── `4G Module TXD`
* **Chân điều khiển nguồn (Power Key):** GPIO điều khiển transistor mở nguồn module để firmware có thể tự động "Hard Reset" module khi rớt mạng quá lâu.

---

## 📌 3. Bảng gán chân GPIO khuyến nghị (Pinout Map)

| Chức năng | ESP32-S3 | ESP32-C3 | Ghi chú |
|---|---|---|---|
| **Đầu vào đếm xu (Coin)** | GPIO 4 | GPIO 0 | Có interrupt, opto PC817 |
| **Đầu vào đếm bill (Tiền giấy)** | GPIO 5 | GPIO 1 | Có interrupt, opto PC817 |
| **Relay 1 (Nhả tiền / Bơm chính)** | GPIO 6 | GPIO 2 | Kích mức HIGH qua ULN2003 |
| **Relay 2 (Phụ)** | GPIO 7 | GPIO 3 | Kích mức HIGH qua ULN2003 |
| **Relay 3 (Phụ)** | GPIO 15 | GPIO 4 | |
| **Relay 4 (Phụ)** | GPIO 16 | GPIO 5 | |
| **UART 4G TX** | GPIO 17 | GPIO 6 | Nối vào 4G RXD |
| **UART 4G RX** | GPIO 18 | GPIO 7 | Nối vào 4G TXD |
| **Màn hình I2C (SDA)** | GPIO 8 | GPIO 8 | Màn OLED 0.96" hoặc LCD 1602 |
| **Màn hình I2C (SCL)** | GPIO 9 | GPIO 9 | Màn OLED 0.96" hoặc LCD 1602 |
| **Nút nhấn Reset WiFi** | GPIO 0 | GPIO 9 | Giữ 5s để xoá WiFi về mode BLE |

---

## 💰 4. Bảng danh mục linh kiện & Dự toán giá thành (BOM)

| STT | Tên linh kiện | Mã tham chiếu | Số lượng | Đơn giá ước tính |
|:---:|---|---|:---:|:---:|
| 1 | Vi điều khiển | ESP32-S3-WROOM-1-N8R2 hoặc ESP32-C3 | 1 | 55.000 đ |
| 2 | IC nguồn xung hạ áp | MP1584EN hoặc XL4015 (Module/Chip) | 1 | 18.000 đ |
| 3 | LDO 3.3V | AMS1117-3.3 SOT-223 | 1 | 2.500 đ |
| 4 | Diode triệt áp TVS | SMAJ24A / SMBJ28A | 2 | 4.000 đ |
| 5 | Cách ly quang Opto | PC817C DIP-4 | 2 | 3.000 đ |
| 6 | Rơ le 12V 10A | Songle SRD-12VDC-SL-C | 4 | 28.000 đ |
| 7 | IC đệm kích relay | ULN2003A SOP-16 | 1 | 3.500 đ |
| 8 | Diode dập hồ quang | 1N4007 + Tụ chống sét VDR | 4 | 5.000 đ |
| 9 | Cọc đấu dây vặn ốc | KF301-2P / KF301-3P | 6 | 12.000 đ |
| 10 | Mạch in 2 lớp (PCB) | Đặt làm 10x10 cm tại JLCPCB/trong nước | 1 | 25.000 đ |
| **Tổng** | **Chi phí phần cứng hoàn thiện 1 bo** | | | **~156.000 đ** |
