# VIMATE VieNeu-TTS sidecar

Internal-only adapter for VieNeu-TTS v3 Turbo. It owns the Python SDK and ONNX
runtime; the Go server only sees a stable HTTP contract returning raw PCM16LE,
mono, 24 kHz.

Runtime guarantees:

- CPU ONNX INT8 (`backend=onnx`, `precision=int8`), no CUDA allocation.
- Exactly one model executor and one model instance.
- Bounded FIFO queue; overload returns HTTP 429.
- Readiness becomes healthy only after model load and a real warm-up synthesis.
- Input, response duration, request time and queue depth are bounded.
- Bearer authentication for every `/v1/*` endpoint in the production Compose
  profile; `VIENEU_TTS_API_KEY` is required before Compose will start it.
- Model downloads are forced to exact Hugging Face commit revisions.
- Model and codec artifacts are downloaded while building the image; runtime
  is offline and cold-start does not require Internet egress.

Pinned upstream versions:

- Python `3.12.11-slim-bookworm`, multi-platform image digest
  `sha256:519591d6871b7bc437060736b9f7456b8731f1499a57e22e6c285135ae657bf7`.
- `vieneu==3.2.3`, source commit
  `452bf58485a37772d8963a7dfb9e13b0d8288a50`.
- `pnnbao-ump/VieNeu-TTS-v3-Turbo` revision
  `75ff82a72f54d55ed389e1eeb12041d3c4bac7d4`.
- `OpenMOSS-Team/MOSS-Audio-Tokenizer-Nano-ONNX` revision
  `ceff0d0749bfb3fa2d61149794ec6feef0d1e1ae`.

## HTTP contract

```http
GET /health/live
GET /health/ready
GET /v1/models
GET /v1/voices
POST /v1/audio/speech
Authorization: Bearer <VIENEU_API_KEY>
Content-Type: application/json

{
  "model": "vieneu-v3-turbo",
  "input": "Xin chào con.",
  "voice": "Phạm Tuyên",
  "style": "tu_nhien",
  "response_format": "pcm",
  "sample_rate": 24000
}
```

Success is `application/octet-stream` with headers
`X-Audio-Format=pcm_s16le`, `X-Audio-Sample-Rate=24000` and
`X-Audio-Channels=1`.

## Important environment variables

| Variable | Default |
|---|---|
| `VIENEU_API_KEY` | required by production Compose |
| `VIENEU_REQUIRE_API_KEY` | `false`; production Compose forces `true` |
| `VIENEU_QUEUE_SIZE` | `8` |
| `VIENEU_THREADS` | `8` |
| `VIENEU_REQUEST_TIMEOUT_SECONDS` | `75` |
| `VIENEU_MAX_TEXT_CHARS` | `600` |
| `VIENEU_MAX_AUDIO_SECONDS` | `60` |
| `VIENEU_DEFAULT_VOICE` | `Phạm Tuyên` |
| `VIENEU_DEFAULT_STYLE` | `tu_nhien` |

Do not publish port 8080 on the host. Attach this service and the Go server to
the same private Compose network, then configure the provider base URL as
`http://vieneu-tts:8080/v1`.

Start only this CPU sidecar with the dedicated profile; do not activate the
Qwen GPU profile on a host that already runs another ASR engine:

```sh
VIENEU_TTS_API_KEY='<secret>' docker compose \
  -f docker-compose.yml \
  -f docker-compose.prod.yml \
  -f docker-compose.ai-local.yml \
  --profile vieneu-tts up -d --build server vieneu-tts
```

Changing either model revision requires rebuilding the image. The build writes
an immutable revision manifest next to the model cache; readiness fails if
runtime configuration does not match the baked artifacts.
