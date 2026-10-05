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
1. Đổi `CONFIG_GTEK_FW_VERSION` lên `"0.1.1"`, `idf.py build`.
2. Upload `build/ie-ota.bin` lên cloud dưới tên `gtek-fw-0.1.1.bin`.
3. Máy sẽ tải trong lần kiểm tra kế (hoặc gửi lệnh `ota_check`).

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
| `IE_ERR_OTA_SIGNATURE`/SHA sai | File upload khác file build; upload lại đúng `build/*.bin` |
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
1. Increment `CONFIG_GTEK_FW_VERSION` to `"0.1.1"` in `menuconfig` or `sdkconfig.defaults`, then run `idf.py build`.
2. Upload `build/ie-ota.bin` to your cloud as `gtek-fw-0.1.1.bin`.
3. The device checks for updates periodically (or triggers immediately upon command `ota_check`).

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
