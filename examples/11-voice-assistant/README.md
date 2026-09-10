# 11 — Voice Assistant: nói với máy, máy trả lời bằng giọng

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
Việt đang chạy thật trên sản phẩm VIMATE Edu. Claude vẫn là bộ não.

```bash
# Terminal 1 — cloud giả có AI + giọng nói
export ANTHROPIC_API_KEY=sk-ant-...        # Claude: hiểu và trả lời
export DASHSCOPE_API_KEY=sk-...            # Qwen3-ASR cloud (DashScope)
export VIENEU_TTS_API_KEY=...              # key của sidecar VieNeu (nếu sidecar bật auth)
go run ./tools/mock-cloud -ai -voice       # cần VieNeu sidecar ở localhost:8080

# Terminal 2 — thiết bị
cd examples/11-voice-assistant
idf.py set-target esp32s3 && idf.py menuconfig   # cloud base URL → http://<IP mock>:8080
idf.py flash monitor
```

Giữ BOOT, nói *"bật đèn"*, thả ra.

### VieNeu TTS sidecar — chạy ở đâu

VieNeu là giọng Việt self-host (CPU, ONNX INT8), không có bản cloud. Sidecar
Docker nằm trong repo VIMATE Edu (`Go-Xiaozhi/server/services/vieneu-tts`):

```bash
cd Go-Xiaozhi/server
VIENEU_TTS_API_KEY=abc docker compose -f docker-compose.ai-local.yml --profile vieneu-tts up -d
# sẵn sàng khi: curl localhost:8080/health/ready → 200 (lần đầu tải model, vài phút)
```

Sidecar chạy trên máy khác (server Edu chẳng hạn): `-tts-url http://<ip>:8080/v1`.

Hợp đồng: `POST /v1/audio/speech` `{model, input, voice, style, response_format:"pcm",
sample_rate}` → PCM16 mono kèm header `X-Audio-Sample-Rate`. mock-cloud **xin
thẳng 16 kHz** nên không resample; header sai → từ chối rõ ràng, không ra tiếng rè.

### Qwen3-ASR — cloud hay self-host

| | Lệnh |
|---|---|
| DashScope cloud (mặc định) | `export DASHSCOPE_API_KEY=...` — model `qwen3-asr-flash` |
| DashScope Bắc Kinh | `-asr-url https://dashscope.aliyuncs.com/compatible-mode/v1` |
| vLLM self-host (GPU) | `-asr-url http://<ip>:8000/v1 -asr-model Qwen/Qwen3-ASR-1.7B -asr-audio-field audio_url` — profile `qwen-asr` trong compose của Edu |

Qwen3 nhận audio qua `/chat/completions` (content-part), **không** phải
`/audio/transcriptions`. Model đôi khi bọc kết quả trong `<asr_text>` hoặc lọt
chữ Hán — mock-cloud dọn giống VIMATE.

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

## Dùng Whisper / OpenAI thay thế

Không có DashScope hay sidecar VieNeu? Chuyển sang API dạng OpenAI — chỉ cần
một key:

```bash
export OPENAI_API_KEY=sk-...
go run ./tools/mock-cloud -ai -voice -asr whisper -tts openai
```

Hoặc server local cùng giao diện (faster-whisper-server, openedai-speech, LocalAI):

```bash
go run ./tools/mock-cloud -ai -voice -asr whisper -asr-url http://localhost:8000/v1 -asr-key x \
    -tts openai -tts-url http://localhost:8001/v1 -tts-key x
```

Chất lượng tiếng Việt: Qwen3 + VieNeu tốt hơn rõ rệt — đó là lý do chúng là
mặc định và là thứ VIMATE Edu dùng cho trẻ em.

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
| `-asr qwen3 cần ... DASHSCOPE_API_KEY` | Thiếu key DashScope; self-host thì đặt `-asr-url` |
| `VieNeu: ... sidecar có chạy ở localhost:8080 không?` | Chưa `docker compose --profile vieneu-tts up` bên repo Edu, hoặc chưa ready |
| `VieNeu HTTP 401` | Sidecar bật auth: `export VIENEU_TTS_API_KEY=` đúng key |
| `VieNeu trả 24000 Hz, đã xin 16000` | Sidecar bản cũ không tôn trọng `sample_rate` — cập nhật sidecar |
| Giữ BOOT không thấy `listen start` | Máy chưa online, hoặc mic `-1` |
| `clip quá ngắn, bỏ` | Bấm dưới 200 ms |
| ASR trả chữ vô nghĩa | Gain sai, hoặc L/R sai kênh → mic thu toàn 0 |
| Tiếng đứt quãng | Mạng chậm hơn 32 KB/s; hoặc `đệm phát đầy` trong log → cloud gửi nhanh hơn nhịp |
| Không có tiếng | Amp DIN sai chân; MAX98357A cần SD nối 3V3 để bật |
