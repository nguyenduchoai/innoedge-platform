# InnoEdge SDK for ESP32

**Đưa một ESP32 lên cloud trong 10 phút — rồi tập trung vào sản phẩm của bạn.**

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-%E2%89%A55.1-red.svg)](https://docs.espressif.com/projects/esp-idf/)
[![Protocol](https://img.shields.io/badge/protocol-v1%20open-green.svg)](docs/PROTOCOL-v1.md)

InnoEdge SDK lo phần hạ tầng IoT mà ai làm thiết bị cũng phải viết lại từ đầu:
cài WiFi, giữ kết nối, không mất giao dịch khi rớt mạng, nhận lệnh từ xa mà
không chạy trùng, cập nhật firmware không biến máy thành cục gạch.

Phần sản phẩm — motor, màn hình, giá bán, luồng phục vụ khách — vẫn là của bạn.
SDK không đụng vào.

```c
#include "innoedge.h"

void app_main(void)
{
    innoedge_config_t cfg = { .fw_version = "0.1.0" };
    innoedge_init(&cfg);
    innoedge_start();
}
```

Bấy nhiêu là máy tự mở BLE cho app cài WiFi, vào cloud, gửi heartbeat, nhận OTA,
và kết nối lại khi rớt mạng.

---

## Mục lục

- [Bắt đầu trong 10 phút](#bắt-đầu-trong-10-phút)
- [Các example](#các-example)
- [API công khai](#api-công-khai)
- [SDK làm gì, bạn làm gì](#sdk-làm-gì-bạn-làm-gì)
- [Những thứ khó mà SDK đã giải](#những-thứ-khó-mà-sdk-đã-giải)
- [Cấu trúc repo](#cấu-trúc-repo)
- [Test](#test)
- [Giao thức mở](#giao-thức-mở)
- [Tương thích](#tương-thích)
- [Trạng thái dự án](#trạng-thái-dự-án)
- [Đóng góp](#đóng-góp)
- [Giấy phép](#giấy-phép)

---

## Bắt đầu trong 10 phút

**Cần:** ESP-IDF ≥ 5.1 và một bo ESP32-S3 bất kỳ. **Không cần đăng ký tài
khoản** — repo có sẵn một cloud giả để bạn chạy thử ngay. Example 01–05 và 07
không cần phần cứng gì thêm.

**1. Chạy cloud giả** (cần Go; cửa sổ terminal riêng):

```bash
git clone https://github.com/nguyenduchoai/innoedge-platform.git
cd innoedge-platform
go run ./tools/mock-cloud
```
```
InnoEdge mock-cloud — ĐỒ THỬ, đừng dùng cho production
  đặt cloud base URL của thiết bị = http://192.168.1.42:8080
```

**2. Nạp firmware:**

```bash
cd examples/01-hello-device
idf.py set-target esp32s3
idf.py menuconfig        # InnoEdge SDK → cloud base URL → http://192.168.1.42:8080
idf.py flash monitor
```

> Dùng **IP LAN** mock in ra, không phải `127.0.0.1` — thiết bị nối từ WiFi.
> Mặc định `https://cloud.example.com` là placeholder; SDK dừng với thông báo
> rõ ràng nếu bạn quên đổi.

Xong bước này là chạy được cả 9 example. Khi nào cần cloud thật thì đổi lại
đúng một dòng cấu hình đó. Chi tiết: [`tools/mock-cloud`](tools/mock-cloud).

Lần đầu, máy chưa có WiFi:
```
W (2100) hello: CHỜ CÀI WIFI — mở app, tìm thiết bị tên bắt đầu bằng GTEK-Setup
```
Mở app di động → chọn thiết bị → nhập WiFi. Máy tự reboot rồi:
```
I (1210) hello: device_id (MAC) = AABBCCDDEEFF — dùng mã này để thêm máy trên cloud
I (5000) hello: online=có  assigned=chưa  hàng đợi tồn=0
```
Thêm máy trên app bằng `device_id`, kích hoạt:
```
I (9110) hello: Máy đã được gán cho đối tác — sẵn sàng phục vụ
```

Xong. Máy đã online, gửi heartbeat, và sẵn sàng nhận OTA.

Gõ vào cửa sổ mock để điều khiển máy:
```
> dispense 20000     # nhả tiền
> dup                # gửi lại CÙNG commandId → máy phải trả "duplicate"
```

Kẹt ở bước nào? Mỗi example có bảng **Troubleshooting** riêng ở cuối README.

---

## Các example

Chạy theo thứ tự — mỗi cái thêm đúng một khái niệm, không nhảy cóc.

| # | Example | Học được gì | Phần cứng |
|---|---|---|---|
| 01 | [hello-device](examples/01-hello-device) | Provisioning WiFi qua BLE, lên cloud, kích hoạt máy | devkit |
| 02 | [telemetry](examples/02-telemetry) | Gửi tiền + cảnh báo · hàng đợi chống mất mạng/mất điện | devkit |
| 03 | [remote-command](examples/03-remote-command) | Nhận lệnh từ cloud · chống chạy trùng | devkit |
| 04 | [device-config](examples/04-device-config) | Cấu hình động từ app · cache chạy offline | devkit |
| 05 | [ota](examples/05-ota) | Cập nhật từ xa · rollback · hoãn khi đang phục vụ khách | devkit |
| 06 | [coin-relay](examples/06-coin-relay) | Đầu đọc xu/bill + relay nhả tiền — máy coin-op đủ hai chiều | đầu đọc xu, relay |
| 07 | [qr-payment](examples/07-qr-payment) | QR động · webhook xác nhận tiền về | devkit |
| 08 | [carwash](examples/08-carwash) | Phiên nhiều relay theo ngân sách thời gian | 4 relay |
| 09 | [ai-agent](examples/09-ai-agent) | **AI quyết định → thiết bị thực thi** — Claude tool calling qua `mock-cloud -ai` | devkit + API key |

Mỗi README có: đấu dây, lệnh build, **log mong đợi từng dòng**, và troubleshooting.

---

## API công khai

Toàn bộ SDK là **14 hàm**. Đọc [`innoedge.h`](components/innoedge/include/innoedge.h)
là đủ — không cần đọc source.

### Vòng đời
```c
esp_err_t   innoedge_init(const innoedge_config_t *cfg);
esp_err_t   innoedge_start(void);          // không chặn — mọi thứ chạy nền
bool        innoedge_is_online(void);
bool        innoedge_is_assigned(void);
const char *innoedge_device_id(void);
```

### Gửi lên cloud
```c
esp_err_t innoedge_publish_payment(kind, count, amount_vnd); // vào NVS trước, gửi sau
uint32_t  innoedge_queue_depth(void);
esp_err_t innoedge_alert(code, severity, message, active);
```
`kind` ∈ `INNOEDGE_PAY_COIN` · `_CASH` · `_TICKET` · `_COIN_OUT`.

### Nhận từ cloud
```c
esp_err_t innoedge_register_command(const char *action, innoedge_command_fn fn);
void      innoedge_reboot_after_ack(void);   // dùng trong handler "reboot"
```

### QR · cấu hình · OTA
```c
esp_err_t innoedge_request_qr(int64_t amount_vnd);
esp_err_t innoedge_config_json(char *out, size_t len, int *version);
esp_err_t innoedge_config_reload(void);
esp_err_t innoedge_ota_check(void);
```

### Sự kiện
Truyền `innoedge_events_t` vào `innoedge_init()`. Mọi callback đều có thể để `NULL`.

| Callback | Khi nào gọi |
|---|---|
| `on_payment_ack` | cloud đã ghi nhận một khoản tiền |
| `on_qr` / `on_qr_error` | QR động sẵn sàng / cổng thanh toán lỗi |
| `on_paid` | webhook xác nhận khách đã trả — **tín hiệu duy nhất được phép giao hàng** |
| `on_static_qr` | cloud đẩy QR tĩnh của máy |
| `on_assigned` / `on_unassigned` | máy được gán / bị gỡ khỏi đối tác |
| `on_config` | cấu hình vận hành đã tải xong |
| `on_provisioning` | đang chờ cài WiFi (BLE/SoftAP đã mở) |

Callback chạy trên task WebSocket — giữ ngắn, việc nặng đẩy sang task riêng.

---

## SDK làm gì, bạn làm gì

| SDK lo | Bạn lo |
|---|---|
| Provisioning WiFi (BLE + SoftAP) | Màn hình, âm thanh, nút bấm |
| Kết nối cloud, xác thực, reconnect có backoff | Motor, bơm, van, relay |
| Hàng đợi giao dịch bền qua mất điện | Giá bán, khuyến mãi, quy tắc kinh doanh |
| Chống trùng lệnh, bền qua reboot | Luồng phục vụ khách |
| Cấu hình động + cache offline | Hiệu chỉnh cảm biến, thời gian bơm |
| OTA: tải, kiểm SHA-256, rollback | Nghiệp vụ của từng lệnh |
| Cảnh báo có latch (không spam) | Khi nào phát cảnh báo |
| Đồng bộ giờ NTP | Giao diện, ngôn ngữ, thương hiệu |

**SDK không bao giờ chứa nghiệp vụ của một sản phẩm cụ thể.** Nếu bạn phải sửa
file trong `components/innoedge/` để làm một tính năng sản phẩm — đó là dấu
hiệu API còn thiếu. Mở issue, đừng fork SDK.

---

## Những thứ khó mà SDK đã giải

Đây là phần đáng tiền. Mỗi mục dưới đây là một lỗi đã xảy ra thật ngoài hiện
trường, đã sửa, và đã có test.

**Mất mạng không mất tiền.** Mọi giao dịch ghi NVS *trước* khi gửi. Cloud dedupe
theo `(device_id, seq)` nên gửi lại bao nhiêu lần cũng không ghi trùng. Hàng đợi
gần đầy → tự cảnh báo; hàng đợi tràn → cảnh báo **critical**, không nuốt tiền
trong im lặng.

**Không nhả tiền hai lần.** Cloud gửi lại lệnh sau khi mạng rớt là chuyện bình
thường. SDK giữ high-watermark `commandId` **trong NVS** và đánh dấu *trước* khi
chạy handler — mất điện giữa lúc nhả tiền, lần gửi lại sau reboot vẫn bị chặn.
Thà bỏ sót một lệnh còn hơn nhả tiền hai lần.

**OTA không biến máy thành cục gạch.** Bản mới chỉ được xác nhận sau khi *vào
được cloud*. Treo hay crash trước đó → bootloader tự quay bản cũ. Và OTA chạy
nền, không chặn boot: mạng yếu không làm khách tưởng máy hỏng.

**Không reboot giữa lúc khách đang trả tiền.** Khai `busy_check` một dòng, SDK
tự hoãn tải và reboot tới lần kiểm tra sau.

**Mất WiFi không phải reset máy.** WiFi đã lưu nhưng lên chậm (sóng yếu, router
chậm) → SDK vẫn mở provisioning làm lưới đỡ *và* chờ nền; có mạng là dịch vụ tự
chạy. Không bắt chủ máy cài lại WiFi.

**Offline vẫn bán đúng giá.** Cấu hình vận hành cache trong NVS, đọc được ngay
từ giây đầu. Lỗi mạng không bao giờ xoá cache cũ.

---

## Cấu trúc repo

```
.
├── components/innoedge/     # SDK — MỘT component, publish lên registry
│   ├── include/innoedge.h   #   hợp đồng công khai duy nhất
│   ├── idf_component.yml    #   manifest cho ESP Component Registry
│   ├── Kconfig              #   tham số hạ tầng — không có GPIO nào
│   └── src/                 #   wifi, provisioning, websocket, OTA, hàng đợi,
│                            #   command bus, config store (nội bộ)
├── components-hw/           # driver phần cứng mẫu — KHÔNG thuộc SDK
│   ├── pulse_input/         #   đếm xung đầu đọc xu/bill (có chống dội)
│   ├── relay_control/       #   relay đa kênh + xung nhả tiền
│   └── wash_control/        #   phiên rửa xe theo ngân sách thời gian
├── examples/
│   ├── example.cmake        # đường dẫn component — một chỗ duy nhất
│   ├── sdkconfig.defaults   # cấu hình chung mọi example
│   └── NN-*/                # mỗi example: CMakeLists + main/ + README
├── tools/
│   ├── mock-cloud/          # cloud giả — chạy example không cần tài khoản
│   │   └── ai.go            #   -ai: Claude tool calling → lệnh xuống thiết bị
│   └── mcp/                 # MCP server cho AI coding (Claude Code/Cursor)
├── .github/workflows/ci.yml # CI: test host + build 8 example bằng ESP-IDF thật
├── tests/run.sh             # test host — không cần ESP-IDF, không cần bo
└── docs/PROTOCOL-v1.md      # đặc tả giao thức thiết bị ↔ cloud
```

`components/` là thứ được hỗ trợ và giữ tương thích. `components-hw/` là driver
mẫu để example chạy trên phần cứng thật — dùng thoải mái, nhưng đừng coi là API
ổn định.

### Dùng SDK trong project của bạn

Cách khuyến nghị — cài từ ESP Component Registry, không cần clone repo:

```bash
idf.py add-dependency "innoedge/innoedge^0.1.0"
```
```cmake
# main/CMakeLists.txt
idf_component_register(SRCS "app_main.c" REQUIRES innoedge)
```

Cần thêm driver phần cứng (đầu đọc xu, relay):

```bash
idf.py add-dependency "innoedge/innoedge-hw^0.1.0"
```

Hoặc dùng thẳng từ bản clone:

```cmake
# CMakeLists.txt của project
list(APPEND EXTRA_COMPONENT_DIRS "path/to/innoedge-platform/components")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(my-device)
```

Cách nhanh nhất để bắt đầu là copy thư mục `examples/01-hello-device` rồi sửa.

### Lập trình có AI hỗ trợ

Nối [`tools/mcp`](tools/mcp) vào Claude Code / Cursor / VS Code để AI đọc thẳng
API, giao thức và bộ luật của SDK — thay vì bịa tên hàm và bịa MQTT topic:

```bash
cd tools/mcp && go build -o innoedge-mcp .
claude mcp add innoedge -- $(pwd)/innoedge-mcp -root $(cd ../.. && pwd)
```

---

## Test

```bash
./tests/run.sh
```

Chạy trên máy dev — không cần ESP-IDF, không cần phần cứng. Bao phủ registry
lệnh và chống trùng: phần mà lỗi sẽ khiến máy **nhả tiền hai lần**. 12 nhóm
kiểm tra, gồm cả ca trùng-qua-reboot.

Test dùng stub tối thiểu trong `tests/stubs/`, và stub log vẫn để compiler kiểm
tra format string — bắt được lỗi kiểu `%d` cho `int64_t` (in sai số tiền).

Bộ thứ hai kiểm tra `tools/mock-cloud` trả đúng từng khung tin trong spec:
handshake, kích hoạt, ack tiền đúng đơn giá, QR + webhook, combo, OTA. Bộ thứ ba
chạy `tools/mcp` thật và nói JSON-RPC qua stdio đúng như client MCP sẽ làm. Cả
hai tự bỏ qua nếu máy chưa cài Go.

---

## Giao thức mở

[`docs/PROTOCOL-v1.md`](docs/PROTOCOL-v1.md) đặc tả đầy đủ giao thức thiết bị ↔
cloud: handshake, heartbeat, khung tiền, cảnh báo, lệnh động, QR, cấu hình, OTA.

**Mở công khai có chủ đích.** Bạn không bị khoá vào một nhà cung cấp — có spec
là tự viết được server thay thế. [`tools/mock-cloud`](tools/mock-cloud) là bằng
chứng: ~450 dòng Go cài đặt lại đủ giao thức để chạy cả 8 example.

Nhưng đừng tự dựng lại *client*: SDK đã xử lý retry, dedupe, hàng đợi bền,
rollback — đúng những chỗ tự làm là mất tiền.

---

## Tương thích

| | |
|---|---|
| ESP-IDF | ≥ 5.1 |
| Chip đã chạy thật | ESP32-S3 (build 8/8 trên ESP-IDF 5.5.4) |
| Chip nên chạy được | ESP32, ESP32-S2, ESP32-C3 (chưa kiểm chứng) |
| Transport | WebSocket over TLS |
| Flash tối thiểu | 4MB — `examples/partitions.csv`: 2 slot OTA × 1,875 MB, không có factory |
| Protocol | v1 — ổn định, tương thích ngược |

**Cam kết tương thích:** `innoedge.h` giữ tương thích ngược trong toàn bộ dòng
v1. Thay đổi phá vỡ tương thích sẽ đi kèm major version mới và v1 vẫn được hỗ
trợ tới khi fleet flash xong.

---

## Trạng thái dự án

**Pre-release (0.1.x).** Hạ tầng bên dưới đã chạy thật trên fleet máy coin-op
đang vận hành; phần đóng gói thành SDK và 8 example thì mới.

Nói thẳng những gì chưa xong, thay vì để bạn tự phát hiện:

| Việc | Trạng thái |
|---|---|
| 8 example đã `idf.py build` | **8/8 build thành công** trên ESP-IDF **5.5.4**, target esp32s3, mỗi bản ~1,23–1,24 MB (2026-09-10, máy tác giả). Lần build đầu bắt được 4 lỗi mà kiểm tra tĩnh không thấy — đã sửa hết (bảng partition, CMakeLists thiếu, tên component chỉ có từ IDF 5.3, Kconfig bool trong biểu thức C). |
| Chưa build trên ESP-IDF 5.1 | README tuyên bố ≥ 5.1 nhưng mới build thật trên 5.5.4. CI có job `idf-floor` để chốt, nhưng CI đang bị chặn ở mức billing của GitHub Actions (miễn phí không giới hạn cho repo công khai). Tới lúc đó, coi 5.5 là bản đã kiểm chứng. |
| Chưa flash lên bo thật trong lần verify này | Hạ tầng bên dưới đã chạy trên fleet thật; phần đóng gói SDK + example thì mới build, chưa chạy trên bo. |
| mock-cloud chưa chạy đối đầu với thiết bị thật | Đã test bằng thiết bị giả nối vào (`go test`), đúng từng khung tin. Chưa có bo thật cắm vào. |
| Tiền tố nội bộ còn là `gtek_*` / `CONFIG_GTEK_*` | Di sản từ hệ chạy trước. API công khai (`innoedge_*`) đã đúng tên và sẽ không đổi. Tiền tố nội bộ sẽ đổi ở một bản major. |
| Chỉ có transport WebSocket | Là thứ đang chạy thật. MQTT sẽ thêm khi có nhu cầu thật, không thêm cho đủ bộ. |
| `gtek_config_lookup_combo()` (logic rửa xe) nằm nhầm trong `net/` | Thuộc về `components-hw/wash_control/`. Sẽ dời, không ảnh hưởng API công khai. |
| Mới kiểm chứng trên ESP32-S3 | Các chip ESP32 khác về lý thuyết chạy được. Báo giúp nếu bạn thử. |
| Chưa lên ESP Component Registry | Manifest đã sẵn sàng (`components/innoedge/idf_component.yml`); còn thiếu bước upload. Tới lúc đó vẫn dùng được bằng `EXTRA_COMPONENT_DIRS`. |

## Đóng góp

Rất hoan nghênh — nhất là báo lỗi từ máy chạy thật ngoài hiện trường.

- **Báo lỗi:** mở issue kèm phiên bản ESP-IDF, chip, và log serial.
- **Pull request:** một PR một việc. Chạy `./tests/run.sh` trước khi gửi.
- **Nghiệp vụ sản phẩm không vào SDK.** PR thêm logic motor/giá/màn hình vào
  `components/innoedge/` sẽ bị từ chối — nhưng nếu bạn phải hack SDK để làm được
  việc đó thì API đang thiếu, hãy nói ra.
- **Đổi giao thức** cần cập nhật cả `docs/PROTOCOL-v1.md` và phía server.

Lỗ hổng bảo mật: đọc [SECURITY.md](SECURITY.md), **đừng mở issue công khai**.

---

## Giấy phép

[Apache License 2.0](LICENSE) — dùng được trong sản phẩm thương mại, có cấp phép
sáng chế, không yêu cầu mở mã sản phẩm của bạn.

Xem [NOTICE](NOTICE) cho thư viện bên thứ ba.
