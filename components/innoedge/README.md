<a id="top"></a>
# InnoEdge SDK for ESP32

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://github.com/nguyenduchoai/innoedge-platform/blob/main/LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-%E2%89%A55.1-red.svg)](https://docs.espressif.com/projects/esp-idf/)
[![Protocol](https://img.shields.io/badge/Protocol-v1%20Open-green.svg)](https://github.com/nguyenduchoai/innoedge-platform/blob/main/docs/PROTOCOL-v1.md)
[![Web Tools](https://img.shields.io/badge/Web%20Portal-Zero--Install-brightgreen.svg)](https://nguyenduchoai.github.io/innoedge-platform/)

> 🇻🇳 **Tài liệu Tiếng Việt** (bên dưới) | [🇬🇧 English Documentation](#english)

**Nền tảng hạ tầng IoT mã nguồn mở vận hành bằng tiền — chuyên biệt cho máy bán nước tự động, máy giặt sấy, trạm rửa xe tự phục vụ, trạm sạc xe điện EV, kiosk thanh toán và thiết bị thương mại.**

---

## 🇻🇳 Tổng quan (Tiếng Việt)

InnoEdge SDK giải quyết toàn bộ phần hạ tầng kỹ thuật nhạy cảm và phức tạp mà các đội ngũ làm phần cứng IoT thương mại thường phải tự xây dựng lại từ đầu:

* **Không mất doanh thu khi rớt mạng:** Mọi giao dịch tiền mặt, xung xu được ghi sổ cái vào bộ nhớ NVS bền vững trước khi gửi lên cloud. Thuật toán kiểm trùng tự động theo `(device_id, seq)` bảo đảm không bao giờ sót hoặc nhân đôi doanh thu dù mạng chập chờn.
* **Relay không bao giờ kích hoạt 2 lần:** Nhật ký từng `commandId` ghi vào NVS *trước* khi đóng relay và *trước* khi ack. Lệnh gửi lại không chạy lần hai, lệnh lệch thứ tự không bị bỏ, mất điện giữa chừng thì báo đối soát thay vì tự chạy lại. `on_paid` của QR không bao giờ tới code hai lần cho cùng một `intentId`.
* **Cập nhật OTA an toàn (Brick-Proof):** Chạy nền, kiểm size + SHA-256 + version ghi trong image trước khi boot, tự rollback nếu bản mới không vào được cloud. Hoãn khi khách đang giao dịch, thử lại sau 10 phút.

---

## Cài đặt vào dự án ESP-IDF

Thêm component vào dự án ESP-IDF bằng lệnh:

```bash
idf.py add-dependency "nguyenduchoai/innoedge^0.2.0"
```

Hoặc khai báo trực tiếp trong `main/idf_component.yml`:

```yaml
dependencies:
  nguyenduchoai/innoedge: "^0.2.0"
```

---

## Khởi động nhanh trong C (Quick Start)

```c
#include "innoedge.h"
#include "esp_log.h"

static const char *TAG = "app";

// Xử lý lệnh kích hoạt nhả hàng từ Cloud
static esp_err_t on_dispense(cJSON *params, char *result, size_t rl, char *msg, size_t ml)
{
    ESP_LOGI(TAG, "Đang nhả hàng cho khách...");
    // Kích hoạt GPIO Relay ở đây
    snprintf(result, rl, "{\"pulses\":2}");
    snprintf(msg, ml, "Đã nhả 2 món hàng");
    return ESP_OK;
}

void app_main(void)
{
    // 1. Khởi tạo SDK
    innoedge_config_t cfg = {
        .heartbeat_sec = 30,
    };
    ESP_ERROR_CHECK(innoedge_init(&cfg));

    // 2. Đăng ký các lệnh chống trùng lặp
    innoedge_register_command("dispense", on_dispense);

    // 3. Khởi động mạng và kết nối WebSocket lên Cloud
    ESP_ERROR_CHECK(innoedge_start());

    // 4. Ghi nhận giao dịch tiền (lưu an toàn vào NVS trước khi gửi cloud)
    innoedge_publish_payment(INNOEDGE_PAY_COIN, 2, 20000);
}
```

---

## 15 Dự Án Mẫu Chạy Thật (Examples)

SDK đi kèm 15 dự án mẫu hoàn chỉnh, có thể build và nạp ngay cho ESP32 / ESP32-S3:

1. **`01-hello-device`**: Kết nối ESP32 với InnoEdge Cloud qua WebSocket, heartbeat, LED trạng thái.
2. **`02-telemetry`**: Ghi nhận doanh thu & báo động sự cố (kẹt xu, hết hàng), lưu đệm NVS khi rớt mạng.
3. **`03-remote-command`**: Nhận lệnh điều khiển hai chiều từ Cloud/App và trả kết quả phản hồi.
4. **`04-device-config`**: Đồng bộ bảng giá động từ xa, lưu cache NVS dùng khi mất mạng.
5. **`05-ota`**: Cập nhật firmware từ xa an toàn, tự rollback nếu lỗi mạng, hoãn nạp khi khách đang trả tiền.
6. **`06-coin-relay`**: Máy bán hàng dùng xu/xung hoàn chỉnh, nhả hàng chống trùng lặp.
7. **`07-qr-payment`**: Thanh toán quét mã VietQR động, nhận xác nhận tiền về tức thì ~1-3 giây.
8. **`08-carwash`**: Máy rửa xe tự phục vụ, quản lý phiên đa thiết bị theo ngân sách thời gian.
9. **`09-ai-agent`**: Tích hợp mô hình ngôn ngữ lớn (LLM Claude/Qwen) điều khiển thiết bị qua tool calling.
10. **`10-edu-tutor`**: Thiết bị gia sư học tiếng Anh thông minh cho trẻ em (màn hình, giọng nói).
11. **`11-voice-assistant`**: Trợ lý giọng nói thông minh Xiaozhi, đàm thoại song công qua WebSocket.
12. **`12-muse-gadget`**: Kiosk AI Avatar biểu cảm & bán hàng tự động tích hợp Meta Muse SDK.
13. **`13-stem-robot`**: Robot tự hành giao hàng và dịch vụ thông minh trong giáo dục STEM.
14. **`14-digital-signage`**: Bảng quảng cáo kỹ thuật số tương tác qua màn hình LED HUB75/TFT.
15. **`15-central-audio`**: Hệ thống âm thanh IP thông báo công cộng (PA) và phát nhạc nền đa vùng.

---

## Tóm tắt C API Công Khai (`include/innoedge.h`)

### Vòng đời & Trạng thái (Lifecycle)
```c
esp_err_t   innoedge_init(const innoedge_config_t *cfg);
esp_err_t   innoedge_start(void);
bool        innoedge_is_online(void);
bool        innoedge_is_assigned(void);
const char *innoedge_device_id(void);
```

### Doanh thu & Cảnh báo (Telemetry)
```c
esp_err_t   innoedge_publish_payment(innoedge_payment_kind_t kind, int count, int64_t amount_vnd);
esp_err_t   innoedge_publish_event(const char *name, const char *data_json);
esp_err_t   innoedge_send_binary(const uint8_t *data, size_t len);
uint32_t    innoedge_queue_depth(void);
esp_err_t   innoedge_alert(const char *code, const char *severity, const char *message, bool active);
```

### Điều khiển & Thanh toán QR (Actuator & Payment)
```c
esp_err_t   innoedge_register_command(const char *action, innoedge_command_fn fn);
void        innoedge_reboot_after_ack(void);
esp_err_t   innoedge_request_qr(int64_t amount_vnd);
esp_err_t   innoedge_config_json(char *out, size_t out_len, int *version);
esp_err_t   innoedge_config_reload(void);
esp_err_t   innoedge_ota_check(void);
```


---

<a id="english"></a>
## 🇬🇧 English Documentation

> [🇻🇳 Quay lại Tiếng Việt](#top)

**Open-source IoT infrastructure SDK for coin-operated, vending, car-wash, EV charging, and automated payment devices.**

### Overview

InnoEdge SDK solves the mission-critical, heavy IoT infrastructure that commercial hardware teams have to reinvent from scratch:

* **Zero Lost Revenue on Network Drops:** Coin and pulse transactions are written to persistent NVS storage before cloud dispatch. Automatic de-duplication on `(device_id, seq)` guarantees no dropped or double-counted money.
* **Actuators Never Fire Twice:** A per-`commandId` journal is written to NVS *before* relays fire and *before* the ack. Retries never run twice, out-of-order commands are never dropped, and a command cut off by a power loss is flagged for reconciliation instead of replayed. QR `on_paid` never reaches your code twice for the same `intentId`.
* **Brick-Proof OTA Updates:** Background updates verified for size, SHA-256 and the version embedded in the image, with automatic rollback if the new app cannot reach the cloud. Postponed while customers are paying.

### Installation

Add this component to your ESP-IDF project using the ESP Component Manager:

```bash
idf.py add-dependency "nguyenduchoai/innoedge^0.2.0"
```

Or add directly to your `main/idf_component.yml`:

```yaml
dependencies:
  nguyenduchoai/innoedge: "^0.2.0"
```

### Quick Start (C)

```c
#include "innoedge.h"
#include "esp_log.h"

static const char *TAG = "app";

// Handle remote actuator command from Cloud
static esp_err_t on_dispense(cJSON *params, char *result, size_t rl, char *msg, size_t ml)
{
    ESP_LOGI(TAG, "Dispensing item...");
    // Trigger relay GPIO here
    snprintf(result, rl, "{\"pulses\":2}");
    snprintf(msg, ml, "Dispensed 2 items");
    return ESP_OK;
}

void app_main(void)
{
    // 1. Initialize SDK
    innoedge_config_t cfg = {
        .heartbeat_sec = 30,
    };
    ESP_ERROR_CHECK(innoedge_init(&cfg));

    // 2. Register idempotent command handlers
    innoedge_register_command("dispense", on_dispense);

    // 3. Start networking & cloud connection
    ESP_ERROR_CHECK(innoedge_start());

    // 4. Record payment (crash-safe, saved to NVS first)
    innoedge_publish_payment(INNOEDGE_PAY_COIN, 2, 20000);
}
```

---

## Ecosystem & Tools

* **Zero-Install Web Portal:** Test Web Serial Flasher & Web BLE Provisioning at [https://nguyenduchoai.github.io/innoedge-platform/](https://nguyenduchoai.github.io/innoedge-platform/)
* **Full Protocol Specification:** [PROTOCOL-v1.md](https://github.com/nguyenduchoai/innoedge-platform/blob/main/docs/PROTOCOL-v1.md)
* **10 Hardware Reference Cookbooks:** [COMMUNITY-COOKBOOKS.md](https://github.com/nguyenduchoai/innoedge-platform/blob/main/docs/COMMUNITY-COOKBOOKS.md)
* **GitHub Repository:** [nguyenduchoai/innoedge-platform](https://github.com/nguyenduchoai/innoedge-platform)

---

## License

Apache License 2.0 — Free forever for commercial and open-source devices.
