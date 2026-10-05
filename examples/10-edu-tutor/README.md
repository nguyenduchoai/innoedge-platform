<a id="top"></a>
# 10 — Edu Tutor: Gia sư học tiếng Anh thông minh (Interactive Learning Tutor)

> 🇻🇳 **Tài liệu Tiếng Việt** (toàn bộ nội dung bên dưới) | [🇬🇧 English Documentation](#english)

Demo đào tạo lấy từ sản phẩm thật **VIMATE Edu** (`vimate.vn` — robot học tiếng
Anh ESP32-S3, màn cảm ứng, mic/loa, wake word "Hi Lily"). Ở đây chạy trên
devkit trắng: màn hình là serial log, giọng nói là chữ, nút chạm là nút BOOT.
**Kiến trúc và khung tin y hệt sản phẩm.**

```
  phụ huynh gõ "bắt đầu bài học"
          │
          ▼
   Claude (persona edu)
     │ show_card("apple","quả táo")            ┐
     │ say("Đây là quả táo, apple!")           │ lệnh  ──▶  ESP32
     │ quiz("Apple là gì?", [mèo, táo, sao])   ┘            │
     │        ...DỪNG, chờ bé...                             │ bé bấm BOOT 2 lần
     │ ◀── {"type":"event","name":"quiz_answer","data":{"index":1}} ◀─┘
     │ say("Đúng rồi!") + show_reward(1)
     ▼
  từ tiếp theo…
```

## Vì sao example này khác 09

Example 09 là **một chiều**: người/AI ra lệnh, máy làm. Example này là **hai
chiều**: máy gửi *sự kiện* (bé chọn đáp án, bé gọi) và AI phải phản ứng. Đó là
API mới `innoedge_publish_event()` — cùng hàng đợi bền với tiền, nên rớt WiFi
đúng lúc bé bấm cũng không mất câu trả lời.

Hai chiều là ranh giới giữa "điều khiển từ xa" và "hội thoại với thế giới thật".

## Chạy

```bash
# Terminal 1 — gia sư AI
export ANTHROPIC_API_KEY=sk-ant-...
go run ./tools/mock-cloud -ai -persona edu

# Terminal 2 — thiết bị
cd examples/10-edu-tutor
idf.py set-target esp32s3 && idf.py menuconfig   # cloud base URL → http://<IP mock>:8080
idf.py flash monitor
```

Gõ vào cửa sổ mock: `bắt đầu bài học`

## Kết quả mong đợi

Cửa sổ mock-cloud:
```
> bắt đầu bài học
  ← {"type":"command","commandId":1,"action":"show_card","params":{"word":"apple","hint":"quả táo"}}
  → {"type":"command_ack","commandId":1,"status":"ok","message":"da hien the"}
  ← {"type":"command","commandId":2,"action":"say","params":{"text":"Chào bé! Đây là apple, quả táo đỏ."}}
  ← {"type":"command","commandId":3,"action":"quiz","params":{"question":"Apple nghĩa là gì?","options":["con mèo","quả táo","mặt trời"]}}
🤖 (chờ bé trả lời)
  → {"type":"event","name":"quiz_answer","seq":12,"data":{"index":1}}
  ← {"type":"event_ack","seq":12}
⚡ [sự kiện từ thiết bị AABB…] quiz_answer {"index":1}
  ← {"type":"command","commandId":4,"action":"say","params":{"text":"Đúng rồi! Giỏi quá!"}}
  ← {"type":"command","commandId":5,"action":"show_reward","params":{"stars":1}}
```

Serial monitor thiết bị:
```
┌──────────── THẺ HỌC ────────────┐
│  apple                          │
│  quả táo                        │
└─────────────────────────────────┘
🔊 Lily: Chào bé! Đây là apple, quả táo đỏ.
❓ Apple nghĩa là gì?
   [A] con mèo   (bấm 1 lần)
   [B] quả táo   (bấm 2 lần)
   [C] mặt trời  (bấm 3 lần)
✋ bé chọn [B]
🔊 Lily: Đúng rồi! Giỏi quá!
🎉 ⭐
```

## Đối chiếu với VIMATE Edu thật

| Khái niệm | VIMATE Edu (sản phẩm) | Example này |
|---|---|---|
| Thẻ từ | MCP `self.edu.show_card(image,title,subtitle)` | lệnh `show_card{word,hint}` |
| Câu hỏi | khung `{"type":"quiz","question","options[]","step","total"}` | lệnh `quiz{question,options[]}` |
| Bé trả lời | chạm nút → `home_select("quizans_<i>")` | BOOT ×n → `event quiz_answer{index}` |
| Thưởng | MCP `self.edu.show_reward(stars)` | lệnh `show_reward{stars}` |
| Gọi gia sư | wake word "Hi Lily" → `listen{mode:"wake"}` | giữ BOOT 2s → `event wake` |
| Lời nói | TTS → loa (Opus) | lệnh `say{text}` → log |
| Ai dạy | `LessonRunner` — bước học cố định trong DB | LLM tự chạy bài theo system prompt |

Dòng cuối là **điểm dạy quan trọng nhất**. Demo để LLM tự dẫn bài cho dễ thấy
tool calling. Sản phẩm thật thì **không** — VIMATE dùng `LessonRunner` với các
bước `Story → ImageCard → Quiz → Speaking` lấy từ database, LLM chỉ chấm câu
trả lời và nói chuyện. Lý do:

- **Nhất quán**: 1.000 bé phải học cùng một bài, không phải 1.000 bài ngẫu hứng.
- **Kiểm duyệt được**: nội dung cho trẻ em phải xem trước, không sinh lúc chạy.
- **Chi phí**: một lượt LLM cho *mỗi bước* × mỗi bé × mỗi ngày.
- **Chịu lỗi**: LLM lỗi thì bài vẫn chạy, chỉ mất phần "nói chuyện".

Chuyển từ demo sang sản phẩm = dời "bài học" từ system prompt sang dữ liệu, và
để LLM chỉ làm phần nó giỏi: hiểu bé nói gì và trả lời tự nhiên.

## Ba luật khi AI dạy trẻ qua thiết bị

**1. Mọi lời nói với bé phải đi qua một kênh kiểm soát được.** System prompt
bắt Lily dùng `say` — không có `say` là bé không nghe thấy gì. Kênh đó là chỗ
đặt bộ lọc nội dung, giới hạn độ dài, và log để phụ huynh xem lại.

**2. Nút chỉ có nghĩa khi có câu hỏi.** Firmware bỏ qua bấm nút ngoài quiz và
chỉ nhận một câu trả lời mỗi câu hỏi. Đừng để bé bấm loạn làm AI rối.

**3. Trần ở thiết bị.** `show_reward` cắt tối đa 3 sao dù LLM gửi 100. Cùng
nguyên tắc với motor ở example 09 và nhả tiền ở example 06.

## Troubleshooting

| Triệu chứng | Nguyên nhân |
|---|---|
| Lily nói mà máy không hiện | Thiếu `-persona edu` — persona mặc định không có tool `say` |
| Bấm nút không có phản ứng | Chưa có quiz đang mở (xem serial: "chưa có câu hỏi"), hoặc bấm quá chậm — 3 lần trong 1,5s |
| `event` gửi mà AI không phản ứng | mock chạy không có `-ai`; event vẫn được ack nhưng không ai đọc |
| Bé bấm đúng mà Lily bảo sai | Index bắt đầu từ 0: A=0, B=1, C=2 — kiểm tra log `✋ bé chọn` |

---

<a id="english"></a>
## 🇬🇧 English Documentation

> [🇻🇳 Quay lại Tiếng Việt](#top)

An interactive demonstration derived from a commercial education device: an ESP32-S3 conversational learning tutor. Here it runs on a bare devkit: display is simulated via serial logs, speech via text, and answers via the BOOT button. **The architecture and wire protocol match the commercial device verbatim.**

```
Parent types "start the lesson"
        │
        ▼
 Claude (Edu Persona)
   │ show_card("apple", "A red fruit")      ┐
   │ say("Look! This is an apple.")         │ Cloud commands ──▶ ESP32
   │ quiz("What is an apple?", [cat, apple])┘                     │
   │        ...WAITING for student...                             │ Student presses BOOT 2 times
   │ ◀── {"type":"event","name":"quiz_answer","data":{"index":1}} ◀─┘
   │ say("Correct! Great job!") + show_reward(1)
   ▼
 Next lesson card…
```

## Why This Differs from Example 09
Example 09 was **unidirectional**: the AI sends commands, and the device reports ACKs. Example 10 is **bidirectional**: the device dispatches asynchronous *telemetry events* (student button choices, wake word triggers), and the AI must reason and respond in real-time.

It leverages `innoedge_publish_event()`, which shares the same crash-safe persistent NVS queue as monetary transactions — ensuring student answers are never lost during WiFi flickers.

## How to Run

### 1. Start AI Tutor Engine (Terminal 1)
```bash
export ANTHROPIC_API_KEY=sk-ant-...
go run ./tools/mock-cloud -ai -persona edu
```

### 2. Build & Flash Firmware (Terminal 2)
```bash
cd examples/10-edu-tutor
idf.py set-target esp32s3 && idf.py menuconfig
idf.py flash monitor
```

### 3. Initiate Lesson in Mock-Cloud Terminal
Type: `start lesson` or `bắt đầu bài học`

Console log:
```
> start lesson
  ← {"type":"command","commandId":1,"action":"show_card","params":{"word":"apple","hint":"Red fruit"}}
  → {"type":"command_ack","commandId":1,"status":"ok","message":"card displayed"}
  ← {"type":"command","commandId":2,"action":"say","params":{"text":"Hello! This is an apple."}}
  ← {"type":"command","commandId":3,"action":"quiz","params":{"question":"What is an apple?","options":["A cat","A red fruit","The sun"]}}
🤖 (Waiting for student answer...)
```
Press BOOT twice on the ESP32 (option 1: "A red fruit"):
```
  → {"type":"event","name":"quiz_answer","seq":12,"data":{"index":1}}
  ← {"type":"event_ack","seq":12}
⚡ [Event from device] quiz_answer {"index":1}
  ← {"type":"command","commandId":4,"action":"say","params":{"text":"Correct! You earned a star!"}}
```
