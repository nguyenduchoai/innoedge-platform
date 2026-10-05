<a id="top"></a>
# 08 — Car Wash: Phiên rửa xe đa thiết bị theo thời gian (Multi-Relay Sessions)

> 🇻🇳 **Tài liệu Tiếng Việt** (toàn bộ nội dung bên dưới) | [🇬🇧 English Documentation](#english)

Example "sản phẩm hoàn chỉnh": lệnh từ cloud + cấu hình động + nhiều relay +
an toàn phần cứng.

## Mô hình
Một combo **không phải** một chuỗi bước tự động. Nó là **ngân sách giây cho từng
thiết bị**, khách tự quyết dùng lúc nào:

```
Combo "Rửa cơ bản" = { nước: 120s, bọt: 60s, khí: 60s, hút: 0s }

Khách bấm NƯỚC  → relay nước ON, trừ ngân sách nước
Khách bấm BỌT   → relay nước OFF, relay bọt ON, trừ ngân sách bọt
Ngân sách nước = 0 → nút NƯỚC khoá, các nút khác vẫn dùng được
Mọi ngân sách = 0 (hoặc quá hạn phiên) → phiên đóng, tắt hết relay
```

Cùng lúc **chỉ một relay bật** — hai bơm chạy song song là tụt áp/hỏng bơm.

## Đấu dây

| Chân | Thiết bị |
|---|---|
| GPIO4 | relay bơm nước |
| GPIO5 | relay bơm bọt |
| GPIO6 | relay van khí |
| GPIO7 | relay máy hút |
| GPIO? | cảm biến có nước (tuỳ chọn, active-low) |

Đổi trong `menuconfig → Board — car wash`. GPIO `-1` = kênh chỉ ghi log → build
và test logic được mà không cần relay thật.

## Build
```bash
idf.py set-target esp32s3
# sửa sdkconfig.defaults nếu chân khác mặc định
idf.py flash monitor
```

## Thử
Gửi lệnh từ cloud:
```jsonc
{"type":"command","commandId":150,"action":"start_wash","params":{"combo":"A"}}
// hoặc không cần cấu hình sẵn:
{"type":"command","commandId":151,"action":"start_wash",
 "params":{"steps":[{"device":"water","seconds":120},{"device":"foam","seconds":60}]}}
```
```
I (30100) carwash: mở phiên: nước=120s bọt=60s khí=60s hút=0s
I (35100) carwash: còn: nước=120s bọt=60s khí=60s hút=0s · phiên=240s
I (50100) carwash: demo: khách bấm XỊT NƯỚC → ESP_OK
I (55100) carwash: còn: nước=115s bọt=60s khí=60s hút=0s · phiên=225s
```

## Bốn lớp an toàn

| Lớp | Chặn cái gì |
|---|---|
| Độc quyền relay | hai bơm chạy cùng lúc |
| Ngân sách từng thiết bị | khách dùng quá phần đã trả |
| Trần phiên (`MASTER_MARGIN_SEC`) | phiên treo vĩnh viễn khi khách bỏ đi |
| Watchdog `MAX_ACTIVATION_SEC` | relay kẹt ON đốt bơm |
| Cảm biến nước (tuỳ chọn) | bơm chạy khô |

Cả bốn đều nằm trong `wash_control` — chạy độc lập, không cần mạng.

## Vì sao combo đọc từ cache NVS
`gtek_config_lookup_combo()` đọc **cache**, không gọi mạng. Mất WiFi giữa ca vẫn
mở đúng phiên. Đó là lý do example 04 (config) nên chạy trước example này.

## Chỉnh theo máy thật
Đừng tin ngân sách trên giấy. Bơm thật có thời gian mồi, van thật có độ trễ,
áp nước thay đổi theo giờ. Chạy thử với xe thật rồi chỉnh `seconds` trong combo
trên app — đó là lý do combo nằm ở cloud chứ không compile vào firmware.

## Troubleshooting
| Triệu chứng | Nguyên nhân |
|---|---|
| `khong co combo A trong cau hinh` | Cache config chưa có / chưa gán đối tác — chạy example 04 trước |
| Relay không kêu | GPIO đang `-1`, hoặc module relay active-low mà firmware xuất active-high |
| Bấm nút không ăn | Chưa có phiên (`start_wash`), hoặc ngân sách thiết bị đó = 0 |
| Relay tắt sau đúng 600s | Watchdog `MAX_ACTIVATION_SEC` — đúng thiết kế |
| Bơm chạy khô | Lắp cảm biến nước và đặt `WASH_WATER_SENSOR_GPIO` |

---

<a id="english"></a>
## 🇬🇧 English Documentation

> [🇻🇳 Quay lại Tiếng Việt](#top)

A complete production product pattern: remote cloud commands + dynamic pricing configuration + multi-relay control + hardware safety interlocks.

## Operational Model
A wash combo is **not** a rigid automated sequence. It is a **flexible second budget allocated across devices**, allowing customers to switch between tools at will:

```
Combo "Standard Wash" = { Water: 120s, Foam: 60s, Air: 60s, Vacuum: 0s }

Customer presses WATER  → Water relay turns ON, water budget decrements
Customer presses FOAM   → Water relay turns OFF, foam relay turns ON, foam budget decrements
Water budget reaches 0  → WATER button locks out, other available tools remain usable
All budgets reach 0     → Session closes, all relays immediately turn OFF
```

**Mutual Hardware Exclusion:** Only **one high-power pump/relay can run at any given moment** to prevent voltage drop and pump burnout.

## Wiring Diagram

| ESP32 Pin | Connected Device |
|---|---|
| GPIO4 | High-pressure water pump relay |
| GPIO5 | Snow foam pump relay |
| GPIO6 | Compressed air solenoid relay |
| GPIO7 | Vacuum motor relay |
| GPIO15 | Water pressure switch (Optional, active-low) |

*Simulation:* Setting GPIOs to `-1` executes the state machine in simulation mode with console logging.

## Build & Flash
```bash
idf.py set-target esp32s3 && idf.py flash monitor
```

## Testing via Cloud Command
Send a session start command from the Web Console:
```jsonc
{"type":"command","commandId":150,"action":"start_wash","params":{"combo":"A"}}
```
Console output:
```
I (30100) carwash: Session opened: water=120s foam=60s air=60s vacuum=0s
I (35100) carwash: Remaining: water=120s foam=60s air=60s · session_timer=240s
I (50100) carwash: Demo: Customer pressed WATER button → Activated
I (55100) carwash: Remaining: water=115s foam=60s air=60s · session_timer=225s
```

## Four Layers of Hardware Safety Interlocks

1. **Mutual Relay Exclusion:** Turning ON any relay automatically turns OFF all other competing relays.
2. **Session Timeout Watchdog:** A global countdown timer forces all relays OFF even if a customer abandons the bay mid-session.
3. **No-Water Pressure Sensor Alert:** If the water pressure sensor triggers active-low during pump operation, the pump stops immediately and an `alert_no_water` is dispatched to the cloud.
4. **Offline Cache Continuity:** Combos and duration budgets are cached in NVS so existing sessions never freeze during internet drops.
