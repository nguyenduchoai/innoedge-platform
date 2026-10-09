# InnoEdge Community Cookbooks
## Công thức triển khai các dòng thiết bị IoT thương mại thực tế

Tập tài liệu này tổng hợp 9 "công thức nấu ăn" (cookbooks) hoàn chỉnh từ phần cứng, sơ đồ chân, luồng tiền đến mã nguồn mẫu để bạn chế tạo sản phẩm thương mại chỉ trong vài ngày.

---

## ☕ Cookbook 1: Máy pha cà phê / Trà sữa tự động bằng VietQR

### Bài toán
Khách hàng chọn món trên màn hình máy hoặc bấm nút -> Máy hiển thị mã QR VietQR đúng số tiền món -> Khách quét app ngân hàng thanh toán -> Ngân hàng báo tiền về -> Máy lập tức kích bơm/van pha cà phê cho khách.

```
 [ Khách bấm nút chọn cà phê (25.000 đ) ]
                   │
                   ▼
 [ ESP32 gọi InnoEdge.requestQr(25000) ]
                   │
                   ▼
 [ Cloud sinh mã VietQR với nội dung GTMOCKD... ]
                   │
                   ▼
 [ ESP32 render mã QR lên màn hình TFT qua on_qr ]
                   │
                   ▼
 [ Khách quét mã QR trên App Ngân hàng ]
                   │
                   ▼
 [ Webhook SePAY / PayOS / Pay2S / Tingee báo về Cloud ]
                   │
                   ▼
 [ Cloud đẩy on_paid -> ESP32 kích relay pha cà phê! ]
```

### Mã nguồn mẫu (ESP-IDF / Arduino)

```cpp
#include <Arduino.h>
#include <InnoEdge.h>

#define BTN_CAPPUCCINO_PIN 12
#define RELAY_PUMP_PIN     4

void setup() {
    Serial.begin(115200);
    pinMode(BTN_CAPPUCCINO_PIN, INPUT_PULLUP);
    pinMode(RELAY_PUMP_PIN, OUTPUT);
    digitalWrite(RELAY_PUMP_PIN, LOW);

    InnoEdge.begin("1.0.0");

    // Khi có QR được tạo: hiển thị lên màn hình
    InnoEdge.onQr([](const char* payload, int64_t amount, const char* ref, int expires, int64_t intent) {
        Serial.printf("--- MỜI QUÉT QR THANH TOÁN ---\nSố tiền: %lld đ | Mã đơn: %s\nPayload: %s\n", amount, ref, payload);
        // Hiển thị mã QR lên LCD (VD: dùng thư viện qrcode / TFT_eSPI)
    });

    // Tín hiệu DUY NHẤT được phép nhả hàng: on_paid
    InnoEdge.onPaid([](int64_t intentId, int64_t amountVnd) {
        Serial.printf("Đã nhận đủ %lld đ. Bắt đầu pha cà phê!\n", amountVnd);
        digitalWrite(RELAY_PUMP_PIN, HIGH); // Bật bơm pha cà phê
        delay(8000);                        // Bơm chạy 8 giây
        digitalWrite(RELAY_PUMP_PIN, LOW);
        Serial.println("Pha xong! Kính mời quý khách thưởng thức.");
    });

    InnoEdge.start();
}

void loop() {
    // Khách ấn nút chọn món Cappuccino 25.000 đ
    if (digitalRead(BTN_CAPPUCCINO_PIN) == LOW) {
        delay(50); // debounce
        if (digitalRead(BTN_CAPPUCCINO_PIN) == LOW) {
            Serial.println("Khách chọn Cappuccino 25.000 đ -> Đang tạo mã QR...");
            InnoEdge.requestQr(25000);
            while (digitalRead(BTN_CAPPUCCINO_PIN) == LOW); // chờ nhả nút
        }
    }
    delay(10);
}
```

---

## 🚗 Cookbook 2: Trạm rửa xe tự phục vụ thông minh 4 Relay

### Bài toán
Khách hàng nhét tiền mặt hoặc chuyển khoản để nạp số dư -> Hệ thống quy đổi thành thời gian (VD: 10.000 đ = 5 phút) -> Khách có thể tùy ý chọn bật Nước rửa xe, Bọt tuyết, Khí nén hoặc Hút bụi trong phạm vi ngân sách thời gian -> Hết giờ tự ngắt relay.

### Sơ đồ đấu nối 4 Relay
* **Relay 1 (GPIO 4):** Van điện từ / Máy bơm cao áp (Nước rửa).
* **Relay 2 (GPIO 5):** Máy bọt tuyết.
* **Relay 3 (GPIO 6):** Máy nén khí xịt khô.
* **Relay 4 (GPIO 7):** Máy hút bụi nội thất.

### Cấu hình Combo qua Cloud (`innoedge_config`)
Thay vì viết cứng thời gian trong firmware, sử dụng tính năng **Dynamic Config** của InnoEdge:
```json
{
  "pricing": { "coinPerBillVnd": 1000 },
  "combos": [
    {
      "id": "BASIC",
      "name": "Rửa nhanh",
      "priceVnd": 20000,
      "payload": {
        "steps": [
          { "device": "water", "seconds": 180 },
          { "device": "foam", "seconds": 60 }
        ]
      }
    }
  ]
}
```
Khi chủ trạm rửa xe muốn tăng/giảm thời gian khuyến mãi trên app quản lý, Cloud sẽ đẩy cấu hình mới về máy và tự động lưu vào NVS. Máy mất mạng vẫn lưu đúng giá và combo mới nhất!

---

## 📦 Cookbook 3: Tủ gửi đồ thông minh (Smart Locker)

### Bài toán
Khách gửi đồ quét QR để thuê ngăn tủ -> Tủ mở chốt điện (Solenoid Lock 12V) -> Khách đóng cửa tủ -> Khi lấy đồ, khách nhập mã OTP hoặc quét QR để mở tủ lại.

### Tính năng an toàn bắt buộc:
* **Chống mở nhầm ngăn:** Mỗi ngăn tủ có 1 cảm biến công tắc hành trình (Microswitch) báo trạng thái Đóng/Mở.
* **Chống kẹt lệnh khi mất điện:** Sử dụng lệnh từ xa `open_door` kèm `commandId`. Nếu điện chập chờn, SDK sẽ tự động lọc bỏ các lệnh cũ đã thực hiện trước đó để không mở tung tất cả các ngăn tủ sau khi có điện lại.

```c
static esp_err_t cmd_open_door(cJSON *params, char *res, size_t res_len, char *msg, size_t msg_len) {
    int door_num = 1;
    if (params) {
        cJSON *door = cJSON_GetObjectItem(params, "door");
        if (cJSON_IsNumber(door)) door_num = door->valueint;
    }
    ESP_LOGI("LOCKER", "Kích mở chốt điện tủ số %d", door_num);
    // Kích xung 500ms cho cuộn hút chốt điện
    open_solenoid(door_num, 500);

    snprintf(msg, msg_len, "Đã mở ngăn %d", door_num);
    snprintf(res, res_len, "{\"door\":%d,\"status\":\"opened\"}", door_num);
    return ESP_OK;
}
```

---

## ⚡ Cookbook 4: Trạm sạc xe máy điện mini có tính giờ

### Bài toán
Đặt trạm sạc xe máy điện ở chung cư mini / quán cafe -> Khách quét QR chọn gói sạc (VD: 10.000 đ sạc trong 2 tiếng) -> Relay cấp nguồn cho ổ cắm -> Hết giờ tự ngắt.

* **Bảo vệ quá dòng:** Tích hợp module cảm biến dòng điện CT (Current Transformer ZMCT103C hoặc ACS712).
* **Phát hiện xe đã rút sạc:** Nếu dòng điện tiêu thụ < 0.1A liên tục trong 10 phút (xe đã đầy pin hoặc khách rút sạc đi trước), thiết bị phát sự kiện `publish_event("charging_finished")` lên cloud để đóng phiên sạc sớm.

---

## 🤖 Cookbook 5: Kiosk / Máy bán hàng thông minh có AI Avatar Meta Muse & VietQR

### Bài toán
Khách hàng trò chuyện tự nhiên với máy qua mic (sử dụng Meta Muse Gadget SDK) để chọn đồ uống -> Muse Agent cloud nhận diện món và gọi lệnh sinh mã VietQR -> InnoEdge hiển thị mã VietQR lên màn hình cạnh Avatar biểu cảm -> Khách quét QR chuyển khoản qua SePAY/PayOS/Pay2S/Tingee -> Ngân hàng báo tiền về -> InnoEdge kích relay rót đồ và Muse Avatar cảm ơn khách.

### Kiến trúc phân tầng (Muse Gadget + InnoEdge)

```
 [ Khách nói: "Cho tôi 1 ly Cà phê sữa đá" ]
                    │
                    ▼
 [ Meta Muse Gadget SDK (Mic & ASR) ]
                    │
                    ▼
 [ Meta Muse AI Cloud ] ── gọi tool "request_payment" ──▶ [ InnoEdge Command Bus ]
                                                                     │
                                                                     ▼
 [ Màn hình Kiosk: Hiện VietQR + Avatar Chờ ] ◀── on_qr() ─── [ InnoEdge Cloud ]
                    │
   Khách quét QR ngân hàng
                    │
                    ▼
 [ Ngân hàng báo tiền về qua Webhook ] ── on_paid() ────────▶ [ ESP32 kích Relay ]
                                                                     │
 [ Avatar Meta Muse chuyển trạng thái HAPPY ] ◀── publish_event() ───┘
```

### Điểm mấu chốt kỹ thuật:
1. **Lớp AI (Meta Muse):** Chịu trách nhiệm thấu hiểu ngôn ngữ tự nhiên, hiển thị Avatar đồ họa sống động, và phát âm thanh phản hồi thân thiện.
2. **Lớp Tiền & Phần cứng (InnoEdge):** Chịu trách nhiệm sinh mã VietQR, bảo vệ chống trùng lệnh (`commandId`), ghi sổ cái NVS bền vững chống mất điện, và giới hạn trần thời gian relay (`RELAY_MAX_SECONDS`).
3. **Mã nguồn mẫu đầy đủ:** Xem tại [examples/12-muse-gadget](../examples/12-muse-gadget).

---

## 🏎️ Cookbook 6: Robot STEM Dịch Vụ Tự Hành (Autonomous Service Robot)

### Bài toán
Chế tạo xe Robot STEM 2 bánh tự hành phục vụ giao đồ ăn / thức uống / tài liệu trong lớp học hoặc văn phòng. Khách hàng quét mã VietQR -> Ngân hàng báo tiền về -> Robot tự khởi hành, né vật cản an toàn bằng sóng siêu âm, đến nơi kích hoạt Servo mở nắp thùng hàng cho khách lấy đồ.

### Sơ đồ đấu nối phần cứng:
* **ESP32-S3:** Bo điều khiển trung tâm WiFi/BLE.
* **Mạch cầu H L298N / TB6612:**
  * Bánh trái: IN1 (GPIO 4), IN2 (GPIO 5).
  * Bánh phải: IN3 (GPIO 6), IN4 (GPIO 7).
* **Cảm biến siêu âm HC-SR04:** TRIG (GPIO 15), ECHO (GPIO 16) - tự động phanh gấp khi khoảng cách < 15cm.
* **Servo SG90:** PWM (GPIO 18) - quay 0° đóng nắp, 90° mở nắp thùng đồ.
* **Nguồn điện:** 2 cell 18650 (7.4V) qua mạch hạ áp LM2596 xuống 5V.

### Lập trình kéo thả với Scratch & Blockly:
Giáo viên và học sinh có thể mở ngay [InnoEdge BlockStudio](../tools/scratch/index.html) để lập trình kéo thả trực quan các khối lệnh:
* Khối `Khi nhận thanh toán VietQR [10.000 đ]`
* Khối `Robot tiến lên tốc độ [80%] trong [2] giây`
* Khối `Quay Servo góc [90] độ`
* Xem mã nguồn C++ và MicroPython tự động sinh ra thời gian thực bên cạnh!

Mã nguồn C/C++ chuẩn ESP-IDF đầy đủ xem tại [examples/13-stem-robot](../examples/13-stem-robot).

---

## 🖥️ Cookbook 7: Kiosk Cảm Ứng Bán Hàng Màn Hình Lớn trên Raspberry Pi & Banana Pi

### Bài toán
Chế tạo Kiosk bán hàng tự động chuyên nghiệp có màn hình cảm ứng lớn (10.1" / 15.6" HDMI), giao diện đồ họa cảm ứng mượt mà (chạy bằng Python Tkinter/PyQt, Electron hoặc Web), kết nối mạch Relay nhả hàng qua cổng GPIO 40-pin và in hóa đơn nhiệt qua cổng USB.

### Kiến trúc triển khai trên Linux SBC:
* **Hệ điều hành:** Raspberry Pi OS (Debian 12 Bookworm) hoặc Armbian (Banana Pi BPI-M2/M5).
* **Ứng dụng Kiosk:** Sử dụng [InnoEdge Linux Python SDK](../linux/python) dưới dạng systemd service.
* **Đấu nối phần cứng (40-Pin Header):**
  * Relay 1 (Món 1): GPIO 17 (Chân vật lý 11).
  * Relay 2 (Món 2): GPIO 27 (Chân vật lý 13).
  * Máy in hóa đơn nhiệt / Máy quét QR: Cổng USB tiêu chuẩn.

### Mã nguồn mẫu Kiosk Python:
Xem mã nguồn chi tiết tại [linux/python/examples/pi_vending_kiosk.py](../linux/python/examples/pi_vending_kiosk.py):
```python
from innoedge import InnoEdge, PinRelay

app = InnoEdge(cloud_url="ws://127.0.0.1:8080/ws")
relay_coffee = PinRelay(pin=17, active_high=False, max_pulse_sec=10)

@app.on_paid
def handle_payment(intent_id, amount_vnd):
    print(f"Ngân hàng xác nhận đã nhận {amount_vnd} đ -> Kích relay rót đồ!")
    relay_coffee.pulse(seconds=5)

```

---

## 📺 Cookbook 8: Bảng Quảng Cáo Kỹ Thuật Số Tương Tác & Báo Cáo Proof-of-Play (Digital Signage)

### Bài toán
Quản lý chuỗi màn hình quảng cáo tập trung tại thang máy, siêu thị, cây xăng. Cloud đẩy playlist quảng cáo từ xa; màn hình phát luân phiên và gửi sự kiện **Proof of Play (POW)** lên Cloud để đối soát doanh thu hiển thị thực tế. Người xem đứng trước bảng LED có thể quét VietQR (50.000 đ) để mua slot chiếu lời chúc hoặc quảng cáo cá nhân trong 5 phút. Khi có báo động khẩn cấp (hỏa hoạn), Cloud lập tức ngắt toàn bộ quảng cáo để phát thông điệp cứu nạn.

* **Phần cứng:** ESP32-S3 (LED Matrix Hub75 / ST7789) hoặc Raspberry Pi / Banana Pi (Màn hình HDMI 32–65").
* **Mã nguồn mẫu C/C++ (ESP-IDF):** [examples/14-digital-signage](../examples/14-digital-signage).
* **Mã nguồn mẫu Python (Raspberry Pi & Banana Pi):** [linux/python/examples/pi_digital_signage.py](../linux/python/examples/pi_digital_signage.py).

---

## 📢 Cookbook 9: Hệ Thống Âm Thanh & Loa Thông Báo Tập Trung Đa Vùng (Multi-Zone IP Audio)

### Bài toán
Hệ thống loa thông báo công cộng (Public Address - PA) và phát nhạc nền (BGM) đa vùng cho chuỗi cửa hàng, siêu thị, nhà xưởng, trường học, tòa nhà:
* **Phát nhạc nền BGM:** Phát liên tục ở mức âm lượng vừa phải (40%).
* **Phân vùng đa điểm (Multi-Zone):** Phát riêng từng tầng (Zone 1, Zone 2) hoặc phát toàn khu vực (Zone ALL).
* **Ngắt ưu tiên thông báo khẩn cấp (Priority Paging):** Khi ban quản trị phát thông báo qua lệnh `audio_announce`, hệ thống tự động ngắt nhạc nền, phát chuông "Ding-Dong", tăng âm lượng lên 85% để phát lời thoại, sau đó tự động khôi phục nhạc nền khi hết giờ.
* **Còi báo cháy 100% âm lượng:** Lệnh `audio_emergency` ghi đè toàn hệ thống để sơ tán hỏa hoạn.
* **Dịch vụ Jukebox:** Khách quét mã VietQR (10.000 đ) để order bài hát yêu thích phát lên hệ thống loa.

* **Phần cứng:** ESP32-S3 + Mạch khuếch đại I2S (MAX98357A / ES8311) hoặc Raspberry Pi / Banana Pi (Cổng 3.5mm AUX / USB Audio).
* **Mã nguồn mẫu C/C++ (ESP-IDF):** [examples/15-central-audio](../examples/15-central-audio).
* **Mã nguồn mẫu Python (Raspberry Pi & Banana Pi):** [linux/python/examples/pi_central_audio.py](../linux/python/examples/pi_central_audio.py).

---

## 🥤 Cookbook 10: Hộp Nâng Cấp Máy Bán Nước Tự Động Chuẩn MDB / ccTalk (Industrial MDB Retrofit Box)

### Bài toán
Hàng triệu máy bán nước ngọt, máy pha cà phê tự động cỡ lớn trên thị trường (FAS, Crane, Sanden Vendo, Necta, Fuji Electric) hoạt động bằng chuẩn công nghiệp **MDB (Multi-Drop Bus)** và **ccTalk**, không có kết nối Internet, không thể quét mã QR, và chủ máy phải đi thu tiền mặt thủ công mỗi ngày.

### Giải pháp InnoEdge Retrofit Box
Cắm một hộp điều khiển InnoEdge ESP32 trực tiếp vào giắc cắm cáp MDB 6-chân của máy bán nước (thông qua mạch cách ly Opto 9-bit UART). Bo mạch đóng vai trò là **MDB Cashless Device (Địa chỉ 0x10)**:
1. **Lắng nghe phím bấm từ khách:** Khi khách chọn lon nước trên bàn phím máy, VMC gửi lệnh `0x13 0x00 VEND REQUEST` (kèm giá tiền và số khay hàng).
2. **Kích hoạt thanh toán linh hoạt:**
   * Màn hình ngoài hiển thị mã QR (VietQR / PromptPay / Stripe).
   * Hoặc khách quét thẻ thành viên RFID.
3. **Chấp thuận nhả hàng:** Khi nhận tín hiệu thanh toán thành công, InnoEdge phản hồi lệnh `0x05 VEND APPROVED` về máy bán nước.
4. **Xác nhận hàng rơi:** Cảm biến quang học của máy bán nước phát hiện lon nước đã rơi vào hộc nhận và gửi lệnh `0x13 0x02 VEND SUCCESS` về InnoEdge để ghi nhận vào sổ cái NVS và báo cáo doanh thu lên Cloud.

* **Phần cứng:** ESP32-S3 + Mạch chuyển mức MDB Optocoupler (9-bit UART) + Nguồn hạ áp cách ly từ 24V/34V DC của bus MDB.
* **Mã nguồn Driver MDB chuẩn hóa:** [components-hw/innoedge_hw/src/innoedge_mdb.h](../components-hw/innoedge_hw/src/innoedge_mdb.h) và [innoedge_mdb.c](../components-hw/innoedge_hw/src/innoedge_mdb.c).
* **Kiểm thử tự động:** Bộ test MDB hoàn chỉnh trong [tests/test_industrial_protocols.c](../tests/test_industrial_protocols.c).

---

## 🤝 Quy trình Đóng góp & Cơ chế RFC (Governance)

InnoEdge là nền tảng mở. Chúng tôi khuyến khích mọi đóng góp từ cộng đồng theo chuẩn kỹ thuật cao:

1. **Báo cáo lỗi (Bug Reports):** Mở Issue trên GitHub kèm log serial, phiên bản ESP-IDF, và các bước tái hiện.
2. **Đề xuất tính năng mới (RFC - Request for Comments):**
   * Nếu bạn muốn mở rộng giao thức [PROTOCOL-v1.md](../docs/PROTOCOL-v1.md) (ví dụ: thêm loại thanh toán mới hoặc transport MQTT/4G), hãy mở một RFC Discussion để toàn bộ cộng đồng cùng phản biện trước khi viết code.
3. **Quy tắc Code & Pull Request:**
   * Một PR cho một mục tiêu cụ thể.
   * Chạy `./tests/run.sh` — toàn bộ 3 bộ test (C Command Bus, Go Mock-Cloud, Go MCP) bắt buộc phải XANH hoàn toàn.
   * Giữ tương thích ngược với [innoedge.h](../components/innoedge/include/innoedge.h).
