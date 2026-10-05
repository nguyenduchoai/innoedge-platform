<a id="top"></a>
# 02 — Telemetry: Tiền & Cảnh báo (Money & Alerts)

> 🇻🇳 **Tài liệu Tiếng Việt** (toàn bộ nội dung bên dưới) | [🇬🇧 English Documentation](#english)

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

---

<a id="english"></a>
## 🇬🇧 English Documentation

> [🇻🇳 Quay lại Tiếng Việt](#top)

Dispatch financial transactions and hardware alerts to the cloud with crash-safe NVS persistence against network drops and sudden power loss.

## Hardware Requirements
Only a devkit board — uses the onboard **BOOT button (GPIO0)**.

| Action | Meaning |
|---|---|
| Quick press BOOT | Customer inserts 1 coin → `innoedge_publish_payment()` |
| Hold BOOT > 2s | Coin mechanism jammed → `innoedge_alert(..., true)` |
| Release BOOT | Coin jam cleared → `innoedge_alert(..., false)` |

## Build & Flash
```bash
idf.py set-target esp32s3 && idf.py flash monitor
```

## The Crucial Test: Zero Lost Money on Network Drop

1. When the device is online, press BOOT → Log shows `cloud acknowledged payment`, `queue_depth=0`.
2. **Turn off your WiFi router** (or unplug network cable).
3. Press BOOT 5 times → Log shows `queue_depth=1,2,3,4,5` — no cloud acknowledgement.
4. **Turn WiFi back on**.
5. Within ~3s: 5 acknowledgment logs stream back from the cloud, and `queue_depth` returns to 0.
6. Unplug the USB cable during Step 3 and plug back in → The payment queue remains completely intact inside NVS flash memory.

## 4 Supported Payment Kinds
```c
innoedge_publish_payment(INNOEDGE_PAY_COIN,     3, 0);      // Customer inserted 3 coins
innoedge_publish_payment(INNOEDGE_PAY_CASH,     0, 20000);  // Bill acceptor: 20,000 VND
innoedge_publish_payment(INNOEDGE_PAY_TICKET,  25, 0);      // Redeem 25 prize tickets
innoedge_publish_payment(INNOEDGE_PAY_COIN_OUT, 10, 0);     // Hopper dispensed 10 coins
```
*Note:* `TICKET` and `COIN_OUT` do not increment revenue — they are used for financial audits and hopper balance tracking.

## Hardware Alert Rules
- `code` is a machine-readable identifier (`coin_jam`, `coin_empty`, `paper_empty`, `no_water`, etc.).
- The cloud deduplicates on `(device_id, code)`: exactly 1 active alert per code; repeated triggers simply increment the counter.
- `active=false` marks the alert as resolved. **Always remember to clear alerts** once the hardware condition returns to normal.
- Safe to call while offline (records state safely without crashing).

## Under the Hood: What the SDK Handles for You
| Feature | Details |
|---|---|
| Monotonic `seq` counter | Persisted to NVS, used by cloud for idempotency |
| NVS write before transmission | Zero lost money on sudden power failure |
| Automatic retry until ACK | Retries every 3s + immediately upon previous ACK |
| Queue near capacity | Automatically raises `payment_queue_backlog_high` alert (80% threshold) |
| Queue overflow safety | Automatically raises `payment_queue_overflow_drop` **critical** alert — never drops money silently |

## Troubleshooting
| Symptom | Cause & Solution |
|---|---|
| No cloud acknowledgment while online | Device not yet assigned to partner — cloud rejects unassigned payments (intended design; money stays safely in queue). |
| Queue depth grows and never decreases | Check device assignment status from Example 01. |
| Alert still shows on app after hardware fix | Alert was triggered with `active=true` but not cleared with `active=false`. |
