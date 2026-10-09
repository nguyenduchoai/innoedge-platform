# 05 — OTA (cập nhật từ xa)

[English](README.md) | [Tiếng Việt](README_vi.md)


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
