<a id="top"></a>
# 09 — AI Agent: AI quyết định, thiết bị thực thi (LLM Tool Calling)

> 🇻🇳 **Tài liệu Tiếng Việt** (toàn bộ nội dung bên dưới) | [🇬🇧 English Documentation](#english)

Demo đào tạo cho câu hỏi *"AI tương tác với thiết bị vật lý thế nào?"* — bằng
thiết bị thật và LLM thật, không slide.

```
"bật đèn rồi cho biết nhiệt độ"
        │
        ▼
   Claude (tool calling)
        │  led(on=true)                          read_temperature()
        ▼                                          ▼
   mock-cloud -ai  ──{"action":"led",…}──▶  ESP32  ──{"celsius":38.2}──▶  mock-cloud
        │                                                                     │
        ▼                                                                     ▼
   "Đã bật đèn. Nhiệt độ chip hiện 38°C."  ◀──────────────────────────────────┘
```

## Điểm dạy quan trọng nhất

**Firmware này không biết gì về AI.** Nó đăng ký 5 lệnh — `led`, `motor`,
`show`, `read_temperature`, `ping` — y hệt example 03. Ai gửi lệnh (người gõ
hay LLM tự quyết) là chuyện của phía server.

Kiến trúc đúng thì thay người bằng AI **không phải sửa một dòng firmware**. Đó
là lý do tách "năng lực thiết bị" (firmware) khỏi "quyết định" (server).

## Phần cứng

Devkit ESP32-S3. Không bắt buộc đấu gì — LED trên bo (GPIO2) và cảm biến nhiệt
trong chip là đủ. Có relay/motor thì nối vào GPIO4.

## Chạy

**1. Server AI** (terminal riêng, cần Go + API key Claude):

```bash
export ANTHROPIC_API_KEY=sk-ant-...      # hoặc: ant auth login
go run ./tools/mock-cloud -ai
```

**2. Thiết bị:**

```bash
idf.py set-target esp32s3
idf.py menuconfig    # InnoEdge SDK → cloud base URL → http://<IP mock in ra>:8080
idf.py flash monitor
```

**3. Gõ tiếng người vào cửa sổ mock-cloud:**

```
> bật đèn rồi cho biết nhiệt độ
  ← {"type":"command","commandId":1,"action":"led","params":{"on":true}}
  → {"type":"command_ack","commandId":1,"status":"ok","message":"LED da bat","result":{"on":true}}
  ← {"type":"command","commandId":2,"action":"read_temperature","params":{}}
  → {"type":"command_ack","commandId":2,"status":"ok","message":"38.2 C","result":{"celsius":38.2}}
🤖 Đã bật đèn. Nhiệt độ chip hiện khoảng 38°C.

> chạy motor 5 giây
  ← {"type":"command","commandId":3,"action":"motor","params":{"seconds":5}}
  → {"type":"command_ack","commandId":3,"status":"ok","result":{"running":true,"seconds":5}}
🤖 Motor đang chạy, sẽ tự dừng sau 5 giây.

> tắt nó đi
🤖 Đã tắt đèn.          ← AI nhớ ngữ cảnh: "nó" = đèn vừa bật
```

Mọi khung tin `←`/`→` là **giao thức InnoEdge thật** — cùng khung tin cloud sản
xuất dùng. Học viên thấy được từng bước, không có gì giấu.

## Kịch bản 20 phút trên lớp

| Phút | Làm gì | Học viên thấy |
|---|---|---|
| 0–3 | Flash, máy online với mock-cloud | Thiết bị lên mạng không cần app |
| 3–6 | Chế độ thường: gõ `led on` | Lệnh → ack, hình dạng khung tin |
| 6–10 | Chuyển `-ai`: "bật đèn" | **Cùng khung tin đó**, giờ LLM tự chọn |
| 10–13 | "bật đèn rồi đọc nhiệt độ" | LLM gọi 2 tool nối tiếp, dùng kết quả thật |
| 13–16 | "chạy motor 10 phút" | LLM gửi 600 → firmware cắt còn 60: **trần an toàn nằm ở thiết bị, không tin AI** |
| 16–18 | Rút cáp giữa lệnh | tool trả lỗi "không ack" → AI nói thật, không bịa |
| 18–20 | Mở `ai.go` + `app_main.c` | Vòng lặp tool calling 40 dòng; firmware 5 handler |

## Ba luật khi để AI điều khiển phần cứng

**1. Trần an toàn nằm ở firmware, không ở prompt.** LLM có thể hiểu "5 phút"
thành 300 giây, hoặc tham số bị sửa trên đường đi. `MOTOR_MAX_SEC` chặn ở nơi
duy nhất không ai qua mặt được — chính thiết bị. Cùng nguyên tắc với trần nhả
tiền ở example 06.

**2. Tool result phải là dữ liệu thật.** `read_temperature` trả số đo thật;
lệnh lỗi trả `is_error`. LLM chỉ được nói lại những gì thiết bị báo. Không có
"AI bịa rằng đã bật đèn".

**3. Handler không được block.** `motor` chạy 60 giây thì đặt bộ đếm rồi ack
ngay, task riêng tắt motor. Ngồi chờ trong handler là treo cả kênh lệnh — và
LLM sẽ chờ ack tới timeout.

## Đổi gì để thành sản phẩm của bạn

| Muốn | Sửa |
|---|---|
| Thêm khả năng mới (van, bơm, loa) | 1 handler trong `app_main.c` + 1 tool trong `tools/mock-cloud/ai.go` |
| Thay mock bằng cloud thật | Cloud của bạn gửi cùng khung tin `command` — firmware không đổi |
| Thêm giọng nói | Đặt STT trước LLM, TTS sau — vòng lặp lệnh giữ nguyên |
| Nhiều thiết bị | `pickDevice(id)` — AI gọi tool kèm `device_id` |

## Troubleshooting

| Triệu chứng | Nguyên nhân |
|---|---|
| `✗ LLM: ... 401` | Thiếu `ANTHROPIC_API_KEY` (hoặc `ant auth login`) |
| AI trả lời nhưng máy không làm gì | Máy chưa nối — gõ `ls` trong mock để xem |
| `không ack lệnh ... sau 15s` | Handler đang block, hoặc máy rớt mạng; xem serial monitor |
| `read_temperature` sai số | Nhiệt độ **chip**, không phải phòng — bình thường cao hơn 10–20°C |

---

<a id="english"></a>
## 🇬🇧 English Documentation

> [🇻🇳 Quay lại Tiếng Việt](#top)

A live training demonstration showing how **Large Language Models interact with the physical world** using real hardware and tool calling — zero PowerPoint slides.

```
"Turn on the light and tell me the current chip temperature"
        │
        ▼
   Claude (LLM Tool Calling)
        │  led(on=true)                          read_temperature()
        ▼                                          ▼
   mock-cloud -ai  ──{"action":"led",…}──▶  ESP32  ──{"celsius":38.2}──▶  mock-cloud
        │                                                                     │
        ▼                                                                     ▼
   "Light is on. Chip temperature is 38°C."  ◀────────────────────────────────┘
```

## The Core Architectural Principle
**The embedded firmware knows nothing about AI.** It registers 5 hardware actions — `led`, `motor`, `show`, `read_temperature`, `ping` — exactly as in Example 03. Whether commands originate from a human operator clicking buttons or an autonomous LLM reasoning engine is purely a server-side concern.

With clean architectural decoupling, replacing a human operator with an autonomous AI requires **zero firmware modifications**.

## Hardware Requirements
ESP32-S3 devkit. Onboard LED (GPIO2) and internal chip temperature sensor are sufficient. Connect an optional relay/motor to GPIO4.

## How to Run

### 1. Start AI Mock-Cloud (Terminal 1)
Requires Go and an Anthropic API Key:
```bash
export ANTHROPIC_API_KEY=sk-ant-...
go run ./tools/mock-cloud -ai
```

### 2. Build & Flash Firmware (Terminal 2)
```bash
cd examples/09-ai-agent
idf.py set-target esp32s3
idf.py menuconfig    # InnoEdge SDK → Cloud base URL → http://<mock-cloud-ip>:8080
idf.py flash monitor
```

### 3. Talk to the Device in Natural Language (Terminal 1)
Type natural language into the mock-cloud console:
```
> turn on the light and report the temperature
  ← {"type":"command","commandId":1,"action":"led","params":{"on":true}}
  → {"type":"command_ack","commandId":1,"status":"ok","message":"LED is on","result":{"on":true}}
  ← {"type":"command","commandId":2,"action":"read_temperature","params":{}}
  → {"type":"command_ack","commandId":2,"status":"ok","message":"38.2 C","result":{"celsius":38.2}}
🤖 I've turned on the light. The current chip temperature is 38.2°C.

> run the motor for 3 seconds
  ← {"type":"command","commandId":3,"action":"motor","params":{"seconds":3}}
  → {"type":"command_ack","commandId":3,"status":"ok","message":"motor running for 3s","result":{"active":true}}
🤖 Started the motor for a 3-second cycle.
```
