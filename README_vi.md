# InnoEdge Platform

**Nền tảng thiết bị IoT & AI biên mã nguồn mở vận hành bằng tiền — từ ESP32, Arduino đến Raspberry Pi & Linux.**

[English](README.md) | [Tiếng Việt](README_vi.md)

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-%E2%89%A55.1-red.svg)](https://docs.espressif.com/projects/esp-idf/)
[![ESP Component Registry](https://components.espressif.com/components/nguyenduchoai/innoedge/badge.svg)](https://components.espressif.com/components/nguyenduchoai/innoedge)
[![Linux SBC](https://img.shields.io/badge/Linux%20SBC-Raspberry%20Pi%20%7C%20Banana%20Pi-orange.svg)](linux/)
[![Arduino](https://img.shields.io/badge/Arduino%20%26%20PlatformIO-Supported-teal.svg)](arduino/)
[![MicroPython](https://img.shields.io/badge/MicroPython-STEM%20Robot-yellow.svg)](micropython/)
[![Protocol](https://img.shields.io/badge/Protocol-v1%20Open-green.svg)](docs/PROTOCOL-v1.md)
[![Examples](https://img.shields.io/badge/Examples-15%20Ready-brightgreen.svg)](examples/)
[![Cookbooks](https://img.shields.io/badge/Cookbooks-10%20B%E1%BA%A3n%20Thi%E1%BA%BFt%20K%E1%BA%BF-purple.svg)](docs/COMMUNITY-COOKBOOKS.md)

---

InnoEdge giải quyết toàn bộ phần hạ tầng kỹ thuật phức tạp mà bất kỳ ai làm thiết bị IoT thương mại (bán nước tự động, máy giặt sấy, trạm sạc xe, kiosk, bảng quảng cáo, loa thông báo...) đều phải viết lại từ đầu:

* **Không mất tiền khi rớt mạng:** Giao dịch ghi sổ cái NVS trước khi gửi, tự động gửi lại có kiểm trùng.
* **Không nhả tiền hai lần:** Chống chạy trùng lệnh bền qua mất điện và reboot bằng watermark NVS.
* **Không biến máy thành cục gạch:** Nạp firmware OTA chạy nền, kiểm tra hash SHA-256, tự rollback nếu lỗi kết nối.
* **Đa nền tảng phần cứng:** Cùng một giao thức cho vi điều khiển siêu rẻ (ESP32/ESP-IDF, Arduino) và máy tính nhúng mạnh mẽ (Raspberry Pi, Banana Pi, Orange Pi qua Python SDK & Go Daemon).
* **AI & Kéo thả trực quan:** Kết nối trực tiếp mô hình ngôn ngữ lớn (Claude/Qwen) qua MCP server và hỗ trợ lập trình kéo thả Scratch 3.0 cho giáo dục STEM.

```c
// ESP32 (ESP-IDF C) — Khởi tạo trong 10 phút
#include "innoedge.h"

void app_main(void)
{
    innoedge_config_t cfg = { .fw_version = "1.0.0" };
    innoedge_init(&cfg);
    innoedge_start();
}
```

```python
# Raspberry Pi & Banana Pi (Python SDK)
from innoedge import InnoEdge

app = InnoEdge(cloud_url="wss://cloud.innoedge.io/ws", fw_version="1.0.0")

@app.on_paid
def on_paid(intent_id, amount):
    print(f"Xác nhận tiền về {amount:,} đ -> Nhả hàng / Kích hoạt dịch vụ")

app.start()
```

---

## Mục lục

1. [Kiến Trúc & Khả Năng Đa Nền Tảng](#kiến-trúc--khả-năng-đa-nền-tảng)
2. [Bắt Đầu Trong 10 Phút](#bắt-đầu-trong-10-phút)
3. [Danh Sách 15 Ví Dụ Chạy Thật](#danh-sách-15-ví-dụ-chạy-thật)
4. [10 Community Cookbooks (Thiết Kế Phần Cứng Mẫu)](#10-community-cookbooks-thiết-kế-phần-cứng-mẫu)
5. [Bộ Công Cụ Tiện Ích Trực Quan](#bộ-công-cụ-tiện-ích-trực-quan)
6. [API Công Khai](#api-công-khai)
7. [Những Thứ Khó Mà InnoEdge Đã Giải](#những-thứ-khó-mà-innoedge-đã-giải)
8. [Cấu Trúc Thư Mục](#cấu-trúc-thư-mục)
9. [Kiểm Thử (Testing)](#kiểm-thử-testing)
10. [Mô Hình Kinh Doanh & Giấy Phép (Open Core & Commercial Licensing)](#mô-hình-kinh-doanh--giấy-phép-open-core--commercial-licensing)

---

## Kiến Trúc & Khả Năng Đa Nền Tảng

InnoEdge tách bạch ranh giới: **Hạ tầng kết nối & Giao thức (Chuẩn hóa)** ↔ **Nghiệp vụ phần cứng sản phẩm (Tự do tùy biến)**.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          INNOEDGE CLOUD PLATFORM                         │
│  Multi-Tenant CMS · Đối Soát Ngân Hàng Tự Động · Quản Lý Hàng Ngàn Máy  │
│  VietQR Webhook (SePAY, PayOS, Pay2S, Tingee) · OTA Phân Phối Theo Lô    │
└────────────────────────────────────┬────────────────────────────────────┘
                                     │ WebSocket / TLS (Protocol v1)
                                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      LỚP THIẾT BỊ ĐA NỀN TẢNG (DEVICES)                  │
├──────────────────────────┬──────────────────────────┬───────────────────┤
│    Vi Điều Khiển (MCU)    │  Máy Tính Nhúng (SBC)    │   Giáo Dục STEM   │
├──────────────────────────┼──────────────────────────┼───────────────────┤
│ • ESP32 / ESP32-S3 (C)   │ • Raspberry Pi (3B/4/5)  │ • MicroPython     │
│ • Arduino / PlatformIO   │ • Banana Pi / Orange Pi  │   InnoBot HAL     │
│ • Chi phí siêu tối ưu    │ • Python SDK + libgpiod  │ • Scratch 3.0     │
│ • Phù hợp: Relay, Động cơ│ • Go Background Agent    │   BlockStudio     │
│   Máy bán nước, Giặt sấy │ • Phù hợp: Kiosk, Video, │ • Phù hợp: Robot, │
│                          │   Bảng quảng cáo, Audio  │   Đồ án học sinh  │
└──────────────────────────┴──────────────────────────┴───────────────────┘
```

Xem chi tiết hướng dẫn đa nền tảng tại [`docs/CROSS-PLATFORM-SBC.md`](docs/CROSS-PLATFORM-SBC.md).

---

## Bắt Đầu Trong 10 Phút

**Yêu cầu:** Một máy tính (macOS/Linux/Windows), ESP32-S3 devkit (hoặc Raspberry Pi), và chưa cần đăng ký tài khoản hay phần cứng phức tạp.

### Bước 1: Khởi động Mock-Cloud & Web Dashboard (Terminal 1)

Repo tích hợp sẵn một Cloud giả lập chạy bằng Go, có đầy đủ Webhook ngân hàng (SePAY / PayOS / Pay2S / Tingee), Web Console realtime và AI Assistant:

```bash
git clone https://github.com/nguyenduchoai/innoedge-platform.git
cd innoedge-platform
go run ./tools/mock-cloud
```

* Mở trình duyệt vào `http://localhost:8080/` để xem **Web Dashboard Realtime**.
* Theo dõi thiết bị online, gửi lệnh nhả tiền, thử nghiệm webhook thanh toán.

### Bước 2: Nạp Firmware Mẫu (Terminal 2)

```bash
cd examples/01-hello-device
idf.py set-target esp32s3
idf.py menuconfig        # InnoEdge SDK -> Cloud base URL -> trỏ vào IP LAN của máy tính
idf.py flash monitor
```

*(Hoặc dùng công cụ nạp trực tiếp qua trình duyệt Chrome/Edge tại [`tools/web-flasher/`](tools/web-flasher/) mà không cần cài ESP-IDF).*

### Bước 3: Cài WiFi qua Web Bluetooth PWA (Không Cần App Mobile)

Mở Chrome truy cập `http://localhost:8080/provision/` (hoặc mở trực tiếp [`tools/web-provision/index.html`](tools/web-provision/index.html)), bấm **"Tìm & Kết nối thiết bị"** qua Web Bluetooth, điền tên WiFi & Mật khẩu rồi bấm Gửi. Thiết bị tự động kết nối WiFi, đồng bộ giờ NTP và online trên Web Dashboard!

---

## Danh Sách 15 Ví Dụ Chạy Thật

Mỗi thư mục example đều có mã nguồn đầy đủ, file cấu hình, hướng dẫn đấu dây và bảng **Troubleshooting** riêng:

| # | Example | Nền Tảng | Nghiệp Vụ Thực Tế | Phần Cứng Cần |
|---|---|---|---|---|
| **01** | [hello-device](examples/01-hello-device) | ESP32-S3 | Provisioning BLE/Web, kết nối Cloud, gửi heartbeat | Devkit |
| **02** | [telemetry](examples/02-telemetry) | ESP32-S3 | Ghi nhận tiền xu/tiền giấy, hàng đợi NVS chống mất điện | Devkit |
| **03** | [remote-command](examples/03-remote-command) | ESP32-S3 | Nhận lệnh điều khiển từ xa, chống chạy trùng qua reboot | Devkit |
| **04** | [device-config](examples/04-device-config) | ESP32-S3 | Đồng bộ đơn giá, cấu hình linh hoạt từ Cloud, cache offline | Devkit |
| **05** | [ota](examples/05-ota) | ESP32-S3 | Cập nhật OTA an toàn qua TLS, hoãn khi máy bận, tự rollback | Devkit |
| **06** | [coin-relay](examples/06-coin-relay) | ESP32-S3 | Máy bán hàng coin-op 2 chiều: nhận xung tiền và kích relay nhả hàng | Đầu đọc xu, Relay |
| **07** | [qr-payment](examples/07-qr-payment) | ESP32-S3 | Sinh mã VietQR động, kích hoạt nhả hàng khi nhận webhook ngân hàng | Devkit + Màn hình |
| **08** | [carwash](examples/08-carwash) | ESP32-S3 | Trạm rửa xe tự động: quản lý nhiều relay (bọt, nước, sấy) theo hạn mức | 4 Relay board |
| **09** | [ai-agent](examples/09-ai-agent) | ESP32-S3 | AI Agent tool-calling (Claude/LLM) tự ra quyết định điều khiển thiết bị | Devkit + Claude |
| **10** | [edu-tutor](examples/10-edu-tutor) | ESP32-S3 | Gia sư thông minh 2 chiều: AI hỏi bài, bé bấm nút tương tác | Devkit + 3 Nút bấm |
| **11** | [voice-assistant](examples/11-voice-assistant) | ESP32-S3 | Trợ lý giọng nói tự host: Giữ nút nói -> Qwen3-ASR -> LLM -> VieNeu TTS | Mic + Loa I2S |
| **12** | [muse-gadget](examples/12-muse-gadget) | ESP32-S3 | Kiosk AI Avatar & Vending Machine: Meta Muse Gadget kết hợp VietQR | Devkit + LCD + Relay |
| **13** | [stem-robot](examples/13-stem-robot) | ESP32 / Pi | Robot STEM tự hành: Né vật cản siêu âm, mở cốp giao hàng khi nhận VietQR | 2 Động cơ, HC-SR04, Servo |
| **14** | [digital-signage](examples/14-digital-signage) | ESP32 / Pi | Bảng quảng cáo: Báo cáo Proof-of-Play, ngắt khẩn cấp, mua slot qua VietQR | Màn hình HDMI / LCD |
| **15** | [central-audio](examples/15-central-audio) | ESP32 / Pi | Loa thông báo đa vùng: Phát nhạc nền BGM, ngắt ưu tiên Paging & Báo cháy | Loa I2S / Cổng AUX 3.5 |

---

## 10 Community Cookbooks (Thiết Kế Phần Cứng Mẫu)

Tài liệu [`docs/COMMUNITY-COOKBOOKS.md`](docs/COMMUNITY-COOKBOOKS.md) chứa công thức chế tạo, danh mục linh kiện (BOM), sơ đồ mạch cách ly opto và mã nguồn ứng dụng mẫu cho 10 dòng sản phẩm:

1. **Tủ Locker Gửi Đồ Tự Động:** Quản lý hàng chục ngăn tủ, thanh toán theo giờ lưu kho, mở ngăn qua VietQR.
2. **Trạm Sạc Xe Máy Điện / Xe Đạp Điện:** Đo đếm kWh (PZEM-004T), thanh toán tiền điện theo thời gian hoặc số điện thực tế.
3. **Tiệm Giặt Sấy Tự Động 24/7:** Điều khiển máy giặt công nghiệp qua Optocoupler, chọn chế độ giặt nhanh/giặt sấy.
4. **Trạm Rửa Xe Tự Phục Vụ:** Đếm ngược thời gian, điều khiển relay máy rửa áp lực cao, bình bọt tuyết, vòi hút bụi.
5. **Kiosk Bán Hàng & Chăm Sóc Khách Hàng AI:** Tích hợp màn hình cảm ứng, trợ lý ảo Avatar giao tiếp với người mua.
6. **Robot Giao Hàng & Đồ Án STEM:** Xe thông minh tự hành vận chuyển bưu phẩm trong tòa nhà hoặc trường học.
7. **Máy Pha Cà Phê & Bán Nước Tự Động:** Điều khiển motor khay chứa lon nước, cảm biến rơi hàng chống kẹt tiền.
8. **Bảng Quảng Cáo Kỹ Thuật Số (Digital Signage):** Quản lý chiến dịch tập trung, báo cáo Proof-of-Play (POW) cho đối tác truyền thông.
9. **Hệ Thống Âm Thanh & Loa Thông Báo Tập Trung:** Phân vùng âm thanh tòa nhà (Zone 1/2), phát thông báo ưu tiên, còi báo động khẩn cấp.
10. **Hộp Nâng Cấp Máy Bán Nước Tự Động Chuẩn MDB / Modbus RTU:** Tích hợp giao thức Multi-Drop Bus (NAMA MDB / ICP Cashless Level 1/2/3) và Modbus RTU RS485 nâng cấp các dòng máy bán hàng tự động & PLC công nghiệp.

Xem thêm sơ đồ mạch điện chi tiết tại [`docs/HARDWARE-REFERENCE.md`](docs/HARDWARE-REFERENCE.md).

---

## Bộ Công Cụ Tiện Ích Trực Quan

> 🚀 **Trải nghiệm trực tuyến trên Web:** Khởi chạy toàn bộ công cụ trực tiếp tại [**nguyenduchoai.github.io/innoedge-platform**](https://nguyenduchoai.github.io/innoedge-platform/) ngay trên trình duyệt (Chrome/Edge) — không cần cài đặt môi trường, không cần cài driver!

Nhà phát triển và cộng đồng có thể triển khai hệ thống mà không cần cài đặt môi trường phức tạp:

| Công Cụ | Thư Mục | Tính Năng |
|---|---|---|
| **InnoEdge Web Portal** | [`tools/web-portal/`](tools/web-portal/) | Trang điều hướng trực quan kết nối công cụ nạp flash, cấu hình Bluetooth và lập trình kéo thả. |
| **InnoEdge BlockStudio** | [`tools/scratch/`](tools/scratch/) | Lập trình kéo thả khối lệnh Scratch 3.0 trực quan cho giáo dục STEM và người mới bắt đầu. |
| **Web 1-Click Flasher** | [`tools/web-flasher/`](tools/web-flasher/) | Nạp firmware nhúng trực tiếp qua trình duyệt web bằng Web Serial API (Chrome/Edge), không cần terminal. |
| **Web Bluetooth Provisioning** | [`tools/web-provision/`](tools/web-provision/) | PWA cài đặt WiFi nhanh chóng cho thiết bị mới qua chuẩn BLE chuẩn hóa. |
| **Global Gateways Connector** | [`tools/connectors/global-gateways/`](tools/connectors/global-gateways/) | Microservice kết nối đa cổng thanh toán quốc tế (Stripe, PayPal, PromptPay Thái Lan QR) và VietQR. |
| **Mock-Cloud & Console** | [`tools/mock-cloud/`](tools/mock-cloud/) | Server giả lập đầy đủ giao thức v1, dashboard giao diện realtime, trình kích hoạt webhook ngân hàng. |
| **InnoEdge Cloud Lite** | [`tools/cloud-lite/`](tools/cloud-lite/) | Bộ Docker Compose hoàn chỉnh + Caddy tự động cấp SSL miễn phí để tự host cloud riêng. |
| **MCP Server for AI Coding** | [`tools/mcp/`](tools/mcp/) | Cung cấp ngữ cảnh API và luật bảo vệ an toàn tiền tệ cho các AI IDE (Claude Code, Cursor, Windsurf). |

---

## API Công Khai

SDK lõi tinh gọn gồm **16 hàm chuẩn hóa** khai báo tại [`components/innoedge/include/innoedge.h`](components/innoedge/include/innoedge.h):

```c
// Vòng đời
esp_err_t   innoedge_init(const innoedge_config_t *cfg);
esp_err_t   innoedge_start(void);
bool        innoedge_is_online(void);
bool        innoedge_is_assigned(void);
const char *innoedge_device_id(void);

// Giao dịch tiền & Viễn trắc (Tự lưu NVS trước khi gửi)
esp_err_t   innoedge_publish_payment(innoedge_pay_kind_t kind, uint32_t count, int64_t amount_vnd);
esp_err_t   innoedge_publish_event(const char *name, const char *data_json);
esp_err_t   innoedge_send_binary(const void *data, size_t len);
uint32_t    innoedge_queue_depth(void);
esp_err_t   innoedge_alert(const char *code, innoedge_alert_severity_t severity, const char *message, bool active);

// Lệnh điều khiển & Thanh toán
esp_err_t   innoedge_register_command(const char *action, innoedge_command_fn fn);
void        innoedge_reboot_after_ack(void);
esp_err_t   innoedge_request_qr(int64_t amount_vnd);
esp_err_t   innoedge_config_json(char *out, size_t len, int *version);
esp_err_t   innoedge_config_reload(void);
esp_err_t   innoedge_ota_check(void);
```

---

## Những Thứ Khó Mà InnoEdge Đã Giải

* **Mất mạng không mất tiền:** Mọi xung tiền nhận được từ đầu đọc xu/tiền giấy đều được ghi vào sổ cái NVS trước khi gửi WebSocket. Cloud tự dedupe theo `(device_id, seq)` nên không bao giờ ghi nhận trùng lặp.
* **Không nhả tiền hai lần:** Cloud gửi lại lệnh sau khi mạng chập chờn là điều tất yếu. SDK lưu vết watermark `commandId` trong NVS và đánh dấu **trước khi** kích hoạt rơ-le nhả hàng. Nếu mất điện đột ngột trong lúc đang nhả hàng, lệnh gửi lại sau khi khởi động sẽ bị chặn ngay lập tức.
* **Quy tắc vàng:** Chỉ có sự kiện `on_paid` được ngân hàng chứng thực mới được phép kích hoạt giao hàng hoặc cấp dịch vụ.
* **Cập nhật OTA không biến máy thành cục gạch:** Firmware mới chỉ được công nhận hợp lệ sau khi máy kết nối thành công tới Cloud. Nếu xảy ra lỗi bootloader sẽ tự động rollback về bản firmware trước đó.
* **Hỗ trợ Giao thức Công nghiệp Vending (MDB & Modbus RTU):** Máy trạng thái 9-bit MDB Cashless peripheral chuẩn NAMA và module Modbus RTU RS485 công nghiệp cách ly quang học, gắn trực tiếp vào bo mạch máy bán hàng tự động và PLC.
* **Đa dạng Cổng Thanh toán Toàn cầu:** Microservice cổng thanh toán tích hợp sẵn Stripe, PayPal, PromptPay Thái Lan QR và VietQR với đối soát webhook tức thì.
* **Hộp Đen Chẩn Đoán & Ghi Vết Sự Cố (Blackbox Crash Recorder):** Tự động lưu vết breadcrumb và gửi báo cáo chẩn đoán sự cố (panic, watchdog timeout, sụt áp brownout) lên Cloud sau khi phục hồi.
* **Bộ Nhớ Xác Định Chống Phân Mảnh RAM (Deterministic Memory):** Static block memory pools và ring buffer luân chuyển lũy thừa 2 loại trừ rủi ro phân mảnh heap, đảm bảo vận hành 24/7/365 không bao giờ cạn kiệt RAM.
* **Đa Kênh Mạng Dự Phòng Tự Chuyển Mạch (Multi-WAN Failover):** Tự động giám sát độ trễ và chuyển hướng kết nối sang mạng 4G LTE khi WiFi bị rớt cáp, tự phục hồi về WiFi khi đường truyền ổn định.
* **Gom Cụm Thiết Bị Nội Bộ (Fleet Master-Worker Mesh):** Cho phép kết nối cụm lên tới 32 máy con (máy giặt, trạm sạc xe) qua ESP-NOW / RS485 về 1 máy Master duy nhất có mạng.
* **Chữ Ký Mật Mã Giao Dịch Chống Sửa Đổi (TAC HMAC-SHA256):** Bảo vệ tính toàn vẹn của từng giao dịch trong NVS, chống can thiệp vật lý vào chip flash.

---

## Cấu Trúc Thư Mục

```
.
├── components/innoedge/     # SDK lõi chuẩn hóa cho ESP-IDF C (Hộp đen, Failover, Cluster, Crypto, Bus, Queue)
├── arduino/InnoEdge/        # Thư viện InnoEdge cho Arduino & PlatformIO (C++)
├── micropython/             # InnoBot HAL & MicroPython cho Robot STEM
├── linux/                   # Hỗ trợ Raspberry Pi, Banana Pi, Orange Pi (Python + Go Agent)
├── components-hw/           # Driver phần cứng mẫu (MDB vending, Modbus RTU, đầu đọc xu, relay, audio I2S)
├── examples/                # 15 ví dụ hoàn chỉnh (01-hello đến 15-central-audio)
├── tools/                   # Web Portal, Mock-cloud, Global Gateways, Web Flasher, Web Provision, Scratch, Cloud Lite, MCP
├── docs/                    # PROTOCOL-v1, HARDWARE-REFERENCE, COMMUNITY-COOKBOOKS
└── tests/run.sh             # Bộ test toàn diện chạy độc lập trên máy tính
```

---

## Kiểm Thử (Testing)

InnoEdge đi kèm bộ kiểm thử toàn diện không cần phần cứng và không cần cài đặt ESP-IDF:

```bash
./tests/run.sh
```

Bao phủ 8 khối kiểm tra tự động:
1. **C Command Bus:** Chống chạy trùng lệnh bền vững qua reboot.
2. **Giao thức Công nghiệp:** Máy trạng thái MDB Cashless & bộ sinh khung tin Modbus RTU CRC16.
3. **CORE Tăng Cường (Resilience):** Hộp đen chẩn đoán sự cố, static memory pool, chuyển mạch mạng Multi-WAN, mesh Master-Worker, và chữ ký mật mã TAC HMAC-SHA256.
4. **Arduino C++ Wrapper:** Kiểm tra cú pháp và tính tương thích API.
5. **MicroPython InnoBot:** Kiểm tra máy học STEM và logic xe tự hành.
6. **Linux SBC Python SDK:** Kiểm tra client, chống trùng lệnh và event flow trên Raspberry Pi.
7. **Linux SBC Agent (Go):** Biên dịch daemon nền của máy tính nhúng.
8. **Mock-Cloud & MCP Server:** Kiểm tra tính toàn vẹn của khung tin giao thức v1.

---

## Mô Hình Kinh Doanh & Giấy Phép (Open Core & Commercial Licensing)

InnoEdge vận hành theo mô hình **Open Core** chuẩn mực trong ngành công nghệ IoT toàn cầu (tương tự ESPHome, Home Assistant, Linux Foundation):

### 1. Phần Mở — Apache License 2.0 (Miễn phí vĩnh viễn)
* Áp dụng cho: Toàn bộ SDK (`components/innoedge`), Giao thức công nghiệp (MDB & Modbus RTU), Global Gateways, Thư viện Arduino, MicroPython, Linux Python SDK, 15 Examples, 10 Cookbooks, Mock-Cloud, và Tài liệu giao thức.
* Quyền lợi: Doanh nghiệp, nhà nghiên cứu và lập trình viên được quyền thương mại hóa, nhúng vào sản phẩm bán lẻ, và tùy biến không giới hạn mà không bị ràng buộc mở mã nguồn thương mại của mình.
* Mục tiêu: Đóng vai trò là "cổng vào" chuẩn mực, tạo dựng cộng đồng hàng ngàn nhà phát triển thiết bị.

### 2. Mô Hình Kiếm Tiền Thương Mại (Monetization Strategies)
Nền tảng InnoEdge mở ra nhiều dòng doanh thu bền vững:

1. **Doanh Thu Đăng Ký Cloud Dịch Vụ (SaaS Subscription):**
   * Cung cấp InnoEdge Cloud Enterprise cho các doanh nghiệp sở hữu chuỗi hàng trăm / hàng ngàn máy bán lẻ tự động.
   * Tính phí thuê bao theo tháng trên mỗi thiết bị hoạt động (ví dụ: 20.000 đ – 50.000 đ / máy / tháng).
   * Giá trị cốt lõi: Quản trị tập trung, ứng dụng Mobile App (iOS/Android) cho chủ máy theo dõi doanh thu thời gian thực, quản lý phân quyền đa cấp, đối soát ngân hàng tự động, và triển khai cập nhật OTA theo lô với SLA 99.9%.

2. **Phí Giao Dịch Thanh Toán (Payment Revenue Sharing):**
   * Tích hợp cổng thanh toán VietQR tự động (SePAY, PayOS, Tingee, Pay2S...).
   * Thu phí vi mô trên mỗi giao dịch thành công (ví dụ: 0.5% – 1% hoặc 200 đ – 500 đ / giao dịch).

3. **Kinh Doanh Phần Cứng Chuẩn Hóa (Hardware Reference Kits):**
   * Sản xuất và bán bo mạch InnoEdge Core Shield, Module Relay cách ly công nghiệp, Bo mạch Robot STEM cho các trường học, trung tâm đào tạo và nhà sản xuất máy bán hàng tự động.

4. **Dịch Vụ Tùy Biến Doanh Nghiệp (Enterprise Customization & SLAs):**
   * Cung cấp dịch vụ tích hợp giải pháp riêng, cài đặt Private Cloud trên hạ tầng của khách hàng lớn và bảo hành dịch vụ kỹ thuật 24/7.
