# voice-stack — Qwen3-ASR + VieNeu TTS trên server local

Hai sidecar tiếng Việt cho chế độ `-voice` của mock-cloud (và là hai provider
mà cloud InnoEdge thật cũng dùng). Lấy từ VIMATE Edu — đang chạy thật cho trẻ em.

| Sidecar | Việc | Cần | Port |
|---|---|---|---|
| **vieneu-tts** | Chữ → giọng Việt tự nhiên | CPU (ONNX INT8), ~2 GB RAM, ~1 GB tải model | 8080 |
| **qwen3-asr** | Giọng → chữ (tiếng Việt) | **NVIDIA GPU ≥ 10 GB VRAM**, ~4 GB tải model | 8000 |

## Chạy

```bash
cd tools/voice-stack

# Chỉ TTS — máy nào cũng chạy được, kể cả laptop không GPU
docker compose up -d vieneu-tts

# Cả hai — server có NVIDIA + nvidia-container-toolkit
docker compose --profile gpu up -d
```

Lần đầu build tải model từ Hugging Face (VieNeu vài phút, Qwen3 vài chục phút
tuỳ mạng). Sau đó runtime **hoàn toàn offline** — model nằm trong image / cache.

Kiểm tra sẵn sàng:

```bash
curl -s localhost:8080/health/ready && echo " ← VieNeu OK"
curl -s localhost:8000/health         && echo " ← Qwen3 OK"
```

Rồi chạy mock-cloud không cần cờ gì thêm — mặc định đã trỏ vào hai cổng này:

```bash
export ANTHROPIC_API_KEY=sk-ant-...
go run ./tools/mock-cloud -ai -voice
```

Sidecar chạy trên máy khác: `-tts-url http://<ip>:8080/v1 -asr-url http://<ip>:8000/v1`.

## Không có GPU?

Qwen3-ASR qua vLLM bắt buộc NVIDIA. Hai đường:

| | Lệnh |
|---|---|
| DashScope cloud (Alibaba, có `qwen3-asr-flash`) | `export DASHSCOPE_API_KEY=...` rồi `-asr dashscope` |
| Whisper (OpenAI hoặc faster-whisper-server local, CPU được) | `-asr whisper -asr-url ... -asr-key ...` |

VieNeu thì CPU là đủ — không có đường cloud vì nó không có bản cloud.

## Bật xác thực khi server có người khác dùng

```bash
VIENEU_TTS_API_KEY=abc VIENEU_REQUIRE_API_KEY=true QWEN3_ASR_API_KEY=xyz docker compose --profile gpu up -d
# mock-cloud:  -tts-key abc -asr-key xyz   (hoặc $VIENEU_TTS_API_KEY / $QWEN3_ASR_API_KEY)
```

Máy dev một mình thì để trống — mặc định không bắt auth.

## Thay đổi so với bản Edu

Chỉ compose: publish port ra localhost, bỏ giới hạn CPU/RAM production, bỏ
`read_only`/`cap_drop` của Qwen3 (chạy trên máy dev), VieNeu mặc định không GPU
reservation (laptop chạy được). **Mã sidecar giữ nguyên byte** — sửa gì thì sửa
ở Edu rồi chép lại, đừng để hai bản trôi.

Hợp đồng HTTP của từng sidecar: `vieneu-tts/README.md`, và Qwen3 là
`/v1/chat/completions` chuẩn vLLM với audio content-part `audio_url`.
