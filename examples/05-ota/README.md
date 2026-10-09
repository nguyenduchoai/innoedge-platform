<a id="top"></a>
# 05 — OTA: Cập nhật firmware từ xa an toàn (Safe Over-The-Air Updates)

> 🇻🇳 **Tài liệu Tiếng Việt** (toàn bộ nội dung bên dưới) | [🇬🇧 English Documentation](#english)

Application **không viết code OTA**. Chỉ khai báo một hàm: "máy có đang phục vụ
khách không?".

## SDK làm gì
```
Nhận manifest → so version → tải HTTPS → kiểm SHA-256 → ghi partition dự phòng
   → reboot → vào được cloud → xác nhận (huỷ rollback)
                            ↘ treo/crash → bootloader quay bản cũ
```

## Bạn làm gì
```c
static bool is_busy(void) { return đang_có_khách; }

innoedge_config_t cfg = { .busy_check = is_busy };
```
Không khai `busy_check` → máy có thể reboot giữa lúc khách đang trả tiền.

## Build & thử
```bash
idf.py set-target esp32s3 && idf.py flash monitor
```
1. Đổi `set(PROJECT_VER "0.1.1")` trong `CMakeLists.txt`, `idf.py build`.
   Phiên bản chỉ có MỘT nguồn này — SDK đối chiếu version ghi trong image với
   manifest, nên image build nhầm version bị từ chối thay vì tải lại mãi.
2. Đưa `build/ie-ota.bin` lên một URL **https** (vd GitHub Release), rồi chạy:
   ```bash
   go run ./tools/mock-cloud -fw 0.1.1 -fw-url https://.../ie-ota.bin -fw-file examples/05-ota/build/ie-ota.bin
   ```
   `-fw-file` là bản sao cục bộ của đúng file đó — mock tính `sha256` + `size`
   cho manifest. Thiếu hai trường này, máy từ chối cài.
3. Máy tải trong lần kiểm tra kế (lúc boot, rồi 6 giờ một lần; bận thì 10 phút
   sau thử lại) — hoặc gửi lệnh `ota_check` để kiểm ngay.

Đúng lúc `ĐANG PHỤC VỤ KHÁCH`, log sẽ cho thấy OTA **hoãn** thay vì reboot.

## Hai điều bắt buộc trong partition table
```
CONFIG_PARTITION_TABLE_TWO_OTA=y        # phải có 2 slot app
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y # bản mới hỏng thì quay bản cũ
```
Đã bật sẵn trong `examples/sdkconfig.defaults`.

## Tại sao OTA chạy nền
Trước đây bản gốc kiểm tra OTA **đồng bộ lúc boot**: mạng yếu là máy đứng
im ~2 phút tải 1.7MB trước khi vẽ được màn hình đầu tiên — khách tưởng máy hỏng.
Giờ SDK đẩy OTA sang task nền, màn hình lên ngay.

## Vàng: đừng tự gọi `esp_ota_mark_app_valid_cancel_rollback()`
SDK gọi hàm này khi WebSocket kết nối thành công lần đầu. Gọi sớm hơn (vd ngay
`app_main`) là **vô hiệu hoá rollback**: bản mới không vào được cloud vẫn được
đánh dấu "khoẻ" và máy chết vĩnh viễn ngoài hiện trường.

## Troubleshooting
| Triệu chứng | Nguyên nhân |
|---|---|
| Boot lặp sau OTA rồi tự về bản cũ | Đúng thiết kế — bản mới không vào được cloud |
| Không bao giờ tải bản mới | Version trên cloud ≤ version đang chạy; hoặc máy nằm ngoài % rollout |
| `SHA-256 KHÔNG khớp manifest` | File trên URL khác file build/`-fw-file`; đưa lại đúng `build/*.bin` |
| `manifest thiếu/sai sha256` | Cloud không gửi `sha256`/`size` — với mock-cloud thì thiếu `-fw-file` |
| `image ghi version 'X' nhưng manifest nói 'Y'` | Quên đổi `PROJECT_VER` trước khi build, hoặc khai sai `-fw` |
| Tải xong không reboot | `busy_check` trả `true` mãi — kiểm tra cờ có được clear không |

---

<a id="english"></a>
## 🇬🇧 English Documentation

> [🇻🇳 Quay lại Tiếng Việt](#top)

Your application **writes zero lines of OTA transport code**. You simply provide one callback: "Is the machine currently busy serving a customer?".

## What the SDK Does Automatically
```
Receive manifest → Compare versions → Download HTTPS stream → Verify SHA-256
   → Write passive partition → Reboot → Authenticate with cloud → Validate (cancel rollback)
                                       ↘ Panic / Crash → Bootloader rolls back to previous app
```

## What Your Application Implements
```c
static bool is_busy(void) { return currently_serving_customer; }

innoedge_config_t cfg = { .busy_check = is_busy };
```
If you omit `busy_check`, a device might reboot while a customer is actively inserting coins or scanning a QR code.

## Build & Testing
```bash
idf.py set-target esp32s3 && idf.py flash monitor
```
1. Set `set(PROJECT_VER "0.1.1")` in `CMakeLists.txt`, then `idf.py build`.
   This is the ONLY version source — the SDK checks the version inside the image
   against the manifest, so a mis-versioned image is refused instead of looping.
2. Put `build/ie-ota.bin` behind an **https** URL (e.g. a GitHub Release), then run:
   ```bash
   go run ./tools/mock-cloud -fw 0.1.1 -fw-url https://.../ie-ota.bin -fw-file examples/05-ota/build/ie-ota.bin
   ```
   `-fw-file` is a local copy of that same file; the mock derives `sha256` + `size`
   for the manifest. Without them the device refuses to install.
3. The device checks at boot, then every 6 hours (10 minutes when busy) — or send
   `ota_check` to check now.

While `is_busy()` returns true, logs will confirm that the update and reboot are **safely postponed**.

## Two Mandatory Partition Table Flags
```ini
CONFIG_PARTITION_TABLE_TWO_OTA=y        # Requires 2 application slots
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y # Roll back if new version crashes
```
These are enabled by default in `examples/sdkconfig.defaults`.

## Why OTA Runs in the Background
Legacy IoT implementations checked OTA synchronously on boot: a slow 2G/3G network caused a 2-minute freeze before the screen could render, leading customers to believe the machine was broken. InnoEdge offloads OTA checking and downloading to a low-priority background task so the user interface launches instantly.

## Golden Rule: Never call `esp_ota_mark_app_valid_cancel_rollback()` manually
The SDK calls this function automatically only after the WebSocket connection successfully establishes with the cloud. Calling it earlier (such as in `app_main`) disables the rollback mechanism, meaning a buggy firmware unable to connect to the cloud will permanently brick the device in the field.

## Troubleshooting
| Symptom | Cause & Solution |
|---|---|
| Device reboots after OTA and reverts to old version | Working as designed — the new firmware panicked or failed to connect to the cloud. |
| Device never downloads new version | Cloud version ≤ running version, or device is outside staged rollout percentage. |
| Download finishes but device never reboots | `busy_check` returns `true` continuously — ensure your busy flag is cleared when service completes. |
| `SHA-256 KHÔNG khớp manifest` | The file at the URL differs from the build / `-fw-file`; re-upload the exact `build/*.bin`. |
| `manifest thiếu/sai sha256` | The cloud sent no `sha256`/`size` — with mock-cloud, `-fw-file` is missing. |
| `image ghi version 'X' nhưng manifest nói 'Y'` | `PROJECT_VER` was not bumped before building, or `-fw` is wrong. |
