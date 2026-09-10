#!/usr/bin/env python3
"""Exercise Qwen3-ASR's real WAV/data-URI path after readiness."""

import base64
import io
import json
import math
import os
import struct
import wave
from urllib.request import Request, urlopen


def test_wav() -> bytes:
    sample_rate = 16_000
    samples = bytearray()
    for index in range(sample_rate * 2):
        envelope = min(1.0, index / 800, (sample_rate * 2 - index) / 800)
        value = int(1200 * envelope * math.sin(2 * math.pi * 440 * index / sample_rate))
        samples.extend(struct.pack("<h", value))
    output = io.BytesIO()
    with wave.open(output, "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(sample_rate)
        wav.writeframes(samples)
    return output.getvalue()


model = os.getenv("QWEN3_ASR_SERVED_MODEL") or os.getenv(
    "QWEN3_ASR_MODEL", "Qwen/Qwen3-ASR-1.7B"
)
audio = base64.b64encode(test_wav()).decode("ascii")
payload = {
    "model": model,
    "messages": [
        {
            "role": "user",
            "content": [
                {
                    "type": "audio_url",
                    "audio_url": {"url": f"data:audio/wav;base64,{audio}"},
                },
                {"type": "text", "text": "Đây là phép thử ASR. Chỉ trả transcript."},
            ],
        }
    ],
    "max_tokens": 32,
    "temperature": 0,
}
request = Request(
    "http://127.0.0.1:8000/v1/chat/completions",
    data=json.dumps(payload).encode("utf-8"),
    headers={"Content-Type": "application/json"},
)
api_key = os.getenv("QWEN3_ASR_API_KEY", "")
api_key_file = os.getenv("QWEN3_ASR_API_KEY_FILE", "")
if not api_key and api_key_file:
    with open(api_key_file, encoding="utf-8") as secret:
        api_key = secret.read().strip()
if api_key:
    request.add_header("Authorization", f"Bearer {api_key}")

with urlopen(request, timeout=180) as response:
    body = json.load(response)
if not body.get("choices"):
    raise SystemExit(f"warmup response has no choices: {body}")
print("qwen3-asr warmup ok: real audio request returned choices")
