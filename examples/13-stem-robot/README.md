<a id="top"></a>
# 13 — Autonomous STEM Robot: Robot tự hành giao hàng (Smart Delivery Rover)

> 🇻🇳 **Tài liệu Tiếng Việt** (toàn bộ nội dung bên dưới) | [🇬🇧 English Documentation](#english)

Ví dụ giáo dục STEM toàn diện kết hợp **Robotics cơ bản** (động cơ, cảm biến, servo) với **hạ tầng dịch vụ thương mại InnoEdge** (thanh toán VietQR ngân hàng, điều khiển từ xa, an toàn phần cứng).

---

## 💡 Ý Tưởng Dự Án

Thay vì chỉ là một chiếc xe đồ chơi chạy loanh quanh vô mục đích, Robot STEM này được thiết kế thành một **xe giao hàng tự hành mini** phục vụ trong trường học, lớp học hoặc quán cafe:

1. **Khách hàng gọi xe / đặt hàng:** Khách bấm nút trên xe hoặc trên app để xin mã VietQR (10.000 đ).
2. **Khách quét mã QR ngân hàng:** Tiền về tài khoản qua SePAY, PayOS, Pay2S hoặc Tingee.
3. **Robot nhận tín hiệu `on_paid`:**
   * Tự động khởi hành, chạy về phía bàn khách.
   * Cảm biến siêu âm HC-SR04 liên tục quét vật cản — nếu có người chắn đường (< 15cm), xe tự động phanh gấp để chống đâm va.
   * Đến nơi, cơ cấu Servo SG90 tự động mở nắp thùng hàng để khách nhận đồ.
   * Chờ 4 giây, nắp tự đóng lại và Robot gửi báo cáo hoàn thành về Cloud!

---

## 🛠️ Danh Sách Linh Kiện (~250.000 đ – 300.000 đ)

| Linh Kiện | Chân ESP32-S3 | Ghi Chú |
|---|---|---|
| **ESP32-S3 DevKitC-1** | — | Bo điều khiển trung tâm WiFi/BLE |
| **Mạch cầu H L298N / TB6612** | IN1 (GPIO 4), IN2 (GPIO 5) | Động cơ bánh trái |
| | IN3 (GPIO 6), IN4 (GPIO 7) | Động cơ bánh phải |
| **Cảm biến siêu âm HC-SR04** | TRIG (GPIO 15), ECHO (GPIO 16) | Đo khoảng cách né vật cản |
| **Động cơ Servo SG90** | PWM (GPIO 18) | Mở/đóng nắp thùng hàng (0°–90°) |
| **Khung xe 2 bánh mica + Bánh xe** | — | Khung xe tự chế hoặc bộ kit robot STEM phổ thông |
| **Nguồn cấp (2 pin 18650 7.4V)** | Hạ áp LM2596 xuống 5V | Cấp nguồn động cơ và ESP32 |

---

## 🚀 Hướng Dẫn Chạy Thử Nghiệm

### Bước 1: Khởi động Mock Cloud
```bash
cd tools/mock-cloud
go run .
```

### Bước 2: Nạp Firmware cho ESP32
Có 2 cách:
* **Cách 1 (Khuyên dùng cho học sinh):** Mở [InnoEdge Web Flasher](../../tools/web-flasher/index.html) trên Chrome/Edge -> Cắm cáp USB -> Nhấn **"Nạp Firmware"** trong 30 giây.
* **Cách 2 (Dành cho lập trình viên ESP-IDF):**
  ```bash
  cd examples/13-stem-robot
  idf.py set-target esp32s3
  idf.py menuconfig   # Điền WiFi và IP Mock Cloud
  idf.py build flash monitor
  ```

---

## 🎮 Thao Tác Kiểm Chứng

1. Nhấn nút **BOOT** trên bo mạch:
   ```
   I stem-robot: Khách ấn nút gọi xe giao hàng (10.000 đ) -> Tạo VietQR...
   I stem-robot: Mã VietQR gọi Robot giao hàng: 10000 đ | Ref: GTMOCKD...
   ```
2. Trên Web Console `http://localhost:8080/dashboard/`:
   Nhấp nút **Simulate Payment (10,000 đ)** để giả lập ngân hàng báo tiền về.
3. Xe lập tức kích hoạt chu trình giao hàng an toàn:
   ```
   I stem-robot: ✓ XÁC NHẬN TIỀN VỀ: 10000 đ -> BẮT ĐẦU NHIỆM VỤ GIAO HÀNG!
   I stem-robot: 🦾 Servo quay góc 90° (MỞ THÙNG ĐỒ)
   I stem-robot: Nhiệm vụ hoàn tất! Robot sẵn sàng cho lượt tiếp theo.
   ```
4. Đặt tay trước cảm biến siêu âm trong lúc xe đang tiến:
   ```
   W stem-robot: ⚠️ CẢNH BÁO: Phát hiện vật cản ở 11.4 cm -> PHANH GẤP!
   ```

---

## 🧩 Lập Trình Kéo Thả Bằng Scratch / Blockly

Bạn có thể mở công cụ [InnoEdge BlockStudio](../../tools/scratch/index.html) để chỉnh sửa hành vi của robot bằng cách kéo thả trực quan các khối lệnh:
* Khối `Khi nhận thanh toán VietQR [10000] đ`
* Khối `Robot tiến lên tốc độ [80]% trong [2] giây`
* Khối `Quay Servo góc [90] độ`
* Xem mã nguồn C++ và MicroPython tự động sinh ra thời gian thực bên cạnh!

---

<a id="english"></a>
## 🇬🇧 English Documentation

> [🇻🇳 Quay lại Tiếng Việt](#top)

A comprehensive STEM educational project bridging **fundamental robotics** (motors, ultrasonic distance sensors, servo mechanisms) with **commercial IoT services** (mobile banking payments, cloud telemetry, hardware safety).

---

## Project Concept

Rather than a simple toy car driving aimlessly, this STEM robot is an **autonomous mini-delivery rover** designed for schools, classrooms, or cafes:

1. **Summon & Order:** A customer presses the button or scans in the app to request an order (10,000 VND).
2. **Instant QR Payment:** Customer transfers via bank app; webhooks authenticate funds.
3. **Robot Receives `on_paid`:**
   * Automatically departs toward the destination table.
   * HC-SR04 ultrasonic distance sensor continuously scans for obstacles — if an obstacle or pedestrian is detected (<15cm), it automatically emergency brakes to prevent collisions.
   * Upon arrival, an SG90 servo automatically lifts the parcel hatch so the customer can collect their item.
   * After 4 seconds, the hatch closes and the rover dispatches a completion telemetry report to the cloud!

---

## Component List (~$10–$15)

| Component | ESP32-S3 Pin | Purpose |
|---|---|---|
| **ESP32-S3 DevKitC-1** | — | Core MCU with WiFi/BLE |
| **L298N / TB6612 Dual H-Bridge** | IN1 (GPIO 4), IN2 (GPIO 5) | Left motor control |
| | IN3 (GPIO 6), IN4 (GPIO 7) | Right motor control |
| **HC-SR04 Ultrasonic Sensor** | TRIG (GPIO 15), ECHO (GPIO 16) | Collision avoidance |
| **SG90 Micro Servo** | PWM (GPIO 18) | Cargo hatch open/close (0°–90°) |
| **2WD Robot Chassis Kit** | — | Acrylic frame, DC gearmotors, caster |
| **Power Supply (2x 18650 Li-ion)** | LM2596 step-down to 5V | Powers motors and ESP32 |

---

## How to Test

### Step 1: Start Mock-Cloud
```bash
cd tools/mock-cloud
go run .
```

### Step 2: Flash Firmware
* **Browser Option (Zero Install):** Open [Web 1-Click Flasher](../../tools/web-flasher/index.html) in Chrome/Edge, plug in the USB cable, and click **"Flash Firmware"** (30s).
* **CLI Option:**
  ```bash
  cd examples/13-stem-robot
  idf.py set-target esp32s3
  idf.py menuconfig
  idf.py build flash monitor
  ```

Press the **BOOT** button to summon the rover, trigger payment, and watch the obstacle avoidance and servo actuation in action!
