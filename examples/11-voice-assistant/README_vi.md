# 11 — Voice Assistant: nói với máy, máy trả lời bằng giọng

[English](README.md) | [Tiếng Việt](README_vi.md)


Kiểu Xiaozhi, trên InnoEdge core. Giữ nút để nói, thả ra máy trả lời.

```
   giữ BOOT                                   thả BOOT
      │                                          │
      ▼  listen start                            ▼  listen stop
   ESP32 ──PCM16 16 kHz, 20 ms/khung──▶  mock-cloud -ai -voice
                                              │ ASR (Whisper)   → "bật đèn"
                                              │ Claude          → led(on) → máy → ack
                                              │ TTS             → PCM
   loa   ◀──tts start · PCM đúng nhịp · tts stop──┘
   LED sáng khi máy đang nói
```

## Phần cứng — bộ rẻ nhất mua được (~120.000đ)

| Linh kiện | Chân ESP32-S3 | Ghi chú |
|---|---|---|
| **INMP441** (mic I2S) SCK | GPIO4 | |
| INMP441 WS | GPIO5 | |
| INMP441 SD | GPIO6 | |
| INMP441 L/R | GND | kênh trái |
| **MAX98357A** (amp I2S) BCLK | GPIO15 | |
| MAX98357A LRC | GPIO16 | |
| MAX98357A DIN | GPIO7 | |
| Loa 4Ω 3W | MAX98357A +/− | |
| Cả hai VDD | 3V3 | |

Đổi chân trong `sdkconfig.defaults` (rồi `idf.py fullclean`). Chân `-1` = tắt
kênh — không có mic vẫn build và boot được.

> Bo có codec chip (ES8311, ES8388 — như ESP32-S3-BOX hay VIMATE) **không** dùng
> driver này: chúng cần I2C cấu hình codec. Đó là driver riêng theo bo.

## Chạy

Mặc định dùng **Qwen3-ASR** (nghe) và **VieNeu TTS** (nói) — hai provider tiếng
Việt đang chạy thật trên VIMATE Edu, **cài trên server local** của bạn. Claude là
bộ não.

```bash
# Terminal 1 — voice stack (một lần; lần đầu tải model, xem tools/voice-stack)
cd tools/voice-stack && docker compose --profile gpu up -d      # Qwen3 cần NVIDIA
#   không GPU:  docker compose up -d vieneu-tts   rồi dùng -asr dashscope hoặc -asr whisper

# Terminal 2 — cloud giả có AI + giọng nói (mặc định đã trỏ localhost:8000/8080)
export ANTHROPIC_API_KEY=sk-ant-...
go run ./tools/mock-cloud -ai -voice

# Terminal 3 — thiết bị
cd examples/11-voice-assistant
idf.py set-target esp32s3 && idf.py menuconfig   # cloud base URL → http://<IP mock>:8080
idf.py flash monitor
```

Giữ BOOT, nói *"bật đèn"*, thả ra.

Voice stack chạy trên máy khác (server có GPU): `-asr-url http://<ip>:8000/v1
-tts-url http://<ip>:8080/v1`. Chi tiết, yêu cầu phần cứng, bật auth:
[`tools/voice-stack/README.md`](../../tools/voice-stack/README.md).

### Hợp đồng hai provider (lấy từ Edu, không đoán)

| | Endpoint | Điểm dễ sai |
|---|---|---|
| Qwen3-ASR | `POST /v1/chat/completions`, audio là content-part `audio_url` (vLLM) / `input_audio` (DashScope) | **không** phải `/audio/transcriptions`; kết quả có thể bọc `<asr_text>` hoặc lọt chữ Hán — mock dọn như VIMATE |
| VieNeu TTS | `POST /v1/audio/speech` `{model, input, voice, style, response_format:"pcm", sample_rate}` | nhận `sample_rate` → mock **xin thẳng 16 kHz**, không resample; kiểm header `X-Audio-Sample-Rate`, sai thì từ chối |

## Kết quả mong đợi

Serial monitor:
```
I voice: sẵn sàng — chạy `mock-cloud -ai -voice`, GIỮ nút BOOT để nói
I ie.audio: listen start
I ie.audio: listen stop
I voice: 🎤 nghe được: "bật đèn"
I voice: 🔊 máy đang nói…
I voice: 🔇 xong
```
mock-cloud:
```
  🎤 AABB… bắt đầu nói
  🎤 AABB… nói xong: 1.4s
  🎤 AABB…: "bật đèn"
  ← {"type":"stt","text":"bật đèn"}
🤖 Đã bật đèn cho bạn.
  🔊 → AABB…: 1.2s "Đã bật đèn cho bạn."
```
Persona mặc định là `device` (example 09) nên nếu máy cũng chạy handler `led`
thì đèn sáng thật — ở example này chỉ có LED báo "đang nói"; ghép với 09 nếu
muốn AI điều khiển thật qua giọng.

## Không có GPU / không muốn tự host

| Muốn | Lệnh |
|---|---|
| Qwen3 cloud (DashScope, Alibaba) | `export DASHSCOPE_API_KEY=...` · `-asr dashscope` |
| Whisper (OpenAI, hoặc faster-whisper-server local CPU) | `-asr whisper -asr-key ... [-asr-url http://...]` |
| TTS OpenAI thay VieNeu | `-tts openai -tts-key ...` (24 kHz, mock tự resample) |

VieNeu CPU là đủ và không có bản cloud — laptop thường vẫn chạy được TTS.

## SDK core thêm gì để có giọng nói

Đúng **hai thứ**, tổng ~40 dòng:

| | |
|---|---|
| `innoedge_send_binary()` | gửi khung binary (opcode 0x02) |
| `innoedge_events_t.on_binary` / `.on_frame` | nhận binary, và frame text SDK không biết (`tts`, `stt`) |

Toàn bộ phần còn lại — I2S, đệm phát, push-to-talk, giao thức listen/tts — nằm
trong `components-hw/innoedge_audio` (250 dòng, driver mẫu). Đây là cách nền
tảng "đa năng" giữ lõi nhỏ: tính năng mới là component cắm vào, không phải SDK
phình ra. Thanh toán (06), AI agent (09), gia sư (10), giọng nói (11) — cùng
một SDK core, cùng một kênh WebSocket.

## Ba quyết định cố ý

**PCM thô, không Opus.** 16 kHz × 16 bit = 32 KB/s. Trên WiFi LAN đó là chuyện
nhỏ. Opus tốn CPU và thêm dependency; thêm khi thiết bị đi qua internet 4G.

**Push-to-talk, không wake word.** Wake word (ESP-SR WakeNet) là 200 KB model +
AFE + cấu hình theo mic. Nút bấm dạy được cùng kiến trúc mà không cần tuần
hiệu chỉnh. VIMATE có wake word — đó là lớp sản phẩm.

**Cloud gửi PCM đúng nhịp thật** (20 ms mỗi 20 ms). Đệm phát trên máy chỉ 1,5 s;
bắn cả clip một lúc là tràn và rơi tiếng. Test `TestVoiceVongTronDayDu` chốt
điều này.

## Chưa kiểm chứng trên phần cứng

Build OK, test phía server 4/4 với ASR/TTS/thiết bị giả. **Chưa cắm mic thật.**
Ba tham số gần như chắc chắn phải chỉnh khi có bo:

| Tham số | Vì sao |
|---|---|
| `IE_AUDIO_MIC_GAIN_SHIFT` (4) | INMP441 biên độ nhỏ; quá nhỏ → ASR không nghe, quá lớn → méo |
| `slot_mask` trái/phải | tuỳ chân L/R của mic nối GND hay 3V3 |
| Chiều dịch bit 24→16 | nếu tiếng rè toàn bộ, thử `>> 14` thay vì `<< 4 >> 16` |

Đây là loại việc không mô phỏng được — cần bo thật, mic thật, tai thật.

## Troubleshooting

| Triệu chứng | Nguyên nhân |
|---|---|
| `Qwen3-ASR ... connection refused :8000` | Voice stack chưa lên, hoặc chưa `--profile gpu`; xem `docker compose ps` |
| `VieNeu: ... sidecar có chạy ở localhost:8080 không?` | `cd tools/voice-stack && docker compose up -d vieneu-tts`, chờ `health/ready` |
| `VieNeu HTTP 401` | Sidecar bật auth: `export VIENEU_TTS_API_KEY=` đúng key |
| `VieNeu trả 24000 Hz, đã xin 16000` | Sidecar bản cũ không tôn trọng `sample_rate` — cập nhật sidecar |
| Giữ BOOT không thấy `listen start` | Máy chưa online, hoặc mic `-1` |
| `clip quá ngắn, bỏ` | Bấm dưới 200 ms |
| ASR trả chữ vô nghĩa | Gain sai, hoặc L/R sai kênh → mic thu toàn 0 |
| Tiếng đứt quãng | Mạng chậm hơn 32 KB/s; hoặc `đệm phát đầy` trong log → cloud gửi nhanh hơn nhịp |
| Không có tiếng | Amp DIN sai chân; MAX98357A cần SD nối 3V3 để bật |
