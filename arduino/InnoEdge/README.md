# InnoEdge Arduino & PlatformIO Library

Sử dụng InnoEdge SDK trực tiếp trong **Arduino IDE** hoặc **PlatformIO** với cú pháp C++ hiện đại, trực quan.

---

## 📦 Cài đặt

### Cách 1: Dành cho PlatformIO
Thêm vào file `platformio.ini` của bạn:

```ini
[env:esp32s3]
platform = espressif32
board = esp32-s3-devkitc-1
framework = arduino, espidf
lib_deps =
    https://github.com/nguyenduchoai/innoedge-platform.git
```

### Cách 2: Dành cho Arduino IDE
1. Tải thư mục `InnoEdge` về máy.
2. Nén thành file `.zip` hoặc chép thẳng thư mục `InnoEdge` vào thư mục `~/Documents/Arduino/libraries/`.
3. Khởi động lại Arduino IDE.

---

## ⚡ Hướng dẫn nhanh (Quickstart)

```cpp
#include <Arduino.h>
#include <InnoEdge.h>

void setup() {
    Serial.begin(115200);

    // 1. Khởi tạo SDK
    InnoEdge.begin("1.0.0");

    // 2. Lắng nghe thanh toán thành công (VietQR / MoMo / Thẻ)
    InnoEdge.onPaid([](int64_t intentId, int64_t amountVnd) {
        Serial.printf("Đã nhận thanh toán: %lld đ. Bắt đầu phục vụ!\n", amountVnd);
    });

    // 3. Đăng ký lệnh điều khiển từ xa
    InnoEdge.onCommand("dispense", [](cJSON *params, char *res, size_t res_len, char *msg, size_t msg_len) {
        Serial.println("Lệnh nhả tiền từ cloud");
        return ESP_OK;
    });

    // 4. Bắt đầu dịch vụ mạng (chạy nền, không chặn loop)
    InnoEdge.start();
}

void loop() {
    // Luồng loop của bạn tự do xử lý cảm biến, màn hình...
    // InnoEdge chạy độc lập trên FreeRTOS task nền!
    delay(1000);
}
```
