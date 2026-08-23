# 02 — Telemetry: tiền & cảnh báo

Gửi giao dịch tiền và cảnh báo sự cố lên cloud, an toàn trước mất mạng/mất điện.

## Phần cứng
Chỉ cần bo devkit — dùng nút **BOOT (GPIO0)** có sẵn.

| Thao tác | Ý nghĩa |
|---|---|
| Bấm nhanh BOOT | khách bỏ 1 xu → `innoedge_publish_payment()` |
| Giữ BOOT > 2s | máy kẹt xu → `innoedge_alert(..., true)` |
| Thả ra | hết kẹt → `innoedge_alert(..., false)` |

## Build
```bash
idf.py set-target esp32s3 && idf.py flash monitor
```

## Bài test quan trọng nhất: rớt mạng không mất tiền

1. Máy online, bấm BOOT → log `cloud đã ghi: ...`, `tồn 0`.
2. **Tắt WiFi router** (hoặc `idf.py monitor` rồi rút mạng).
3. Bấm BOOT 5 lần → `tồn 1,2,3,4,5` — không có `cloud đã ghi`.
4. **Bật lại WiFi**.
5. Trong ~3s: 5 dòng `cloud đã ghi` chạy về, `tồn` về 0.
6. Rút điện giữa bước 3 rồi cắm lại → hàng đợi vẫn còn nguyên (nằm trong NVS).

## 4 loại tiền SDK hỗ trợ
```c
innoedge_publish_payment(INNOEDGE_PAY_COIN,     3, 0);      // khách bỏ 3 xu
innoedge_publish_payment(INNOEDGE_PAY_CASH,     0, 20000);  // bill 20.000đ
innoedge_publish_payment(INNOEDGE_PAY_TICKET,  25, 0);      // hủy 25 vé thưởng
innoedge_publish_payment(INNOEDGE_PAY_COIN_OUT, 10, 0);     // trả khách 10 xu
```
`TICKET` và `COIN_OUT` **không tính doanh thu** — chúng để đối soát.

## Quy tắc cảnh báo
- `code` là định danh máy-đọc (`coin_jam`, `coin_empty`, `paper_empty`, `no_water`…).
- Cloud gom theo `(máy, code)`: 1 cảnh báo active mỗi code; gọi lặp chỉ tăng `count`.
- `active=false` = resolve. **Luôn nhớ clear** — không thì app đối tác đỏ mãi.
- Gọi khi offline vẫn an toàn (trả lỗi, không crash).

## Điều SDK làm mà bạn không thấy
| Việc | Chi tiết |
|---|---|
| Cấp `seq` đơn điệu | lưu NVS, dùng để cloud dedupe |
| Ghi NVS trước khi gửi | mất điện không mất tiền |
| Gửi lại tới khi có ack | mỗi 3s + ngay khi ack phần tử trước |
| Hàng đợi gần đầy | tự phát alert `payment_queue_backlog_high` (80% cap) |
| Hàng đợi tràn | tự phát alert `payment_queue_overflow_drop` **critical** — không nuốt tiền trong im lặng |

## Troubleshooting
| Triệu chứng | Nguyên nhân |
|---|---|
| Không thấy `cloud đã ghi` dù online | Máy chưa được gán đối tác → cloud không ack (đúng thiết kế, tiền giữ trong hàng đợi) |
| `tồn` tăng mãi không giảm | Kiểm tra `assigned` ở example 01 trước |
| Alert không hiện trên app | `code` gửi rồi nhưng chưa clear cái cũ — mỗi code chỉ 1 alert active |
