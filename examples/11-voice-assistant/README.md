# 11 — Voice Assistant: Speak to Hardware, Hear It Reply

[English](README.md) | [Tiếng Việt](README_vi.md)

A Xiaozhi-style voice assistant built on the InnoEdge core. Hold the button to speak; release to hear the spoken response.

```
   Hold BOOT                                  Release BOOT
      │                                            │
      ▼  listen start                              ▼  listen stop
   ESP32 ──PCM16 16 kHz, 20 ms/frame──▶  mock-cloud -ai -voice
                                               │ ASR (Qwen3/Whisper) → "turn on the light"
                                               │ Claude              → led(on) → Device → ACK
                                               │ TTS (VieNeu)        → PCM audio stream
   Speaker ◀──tts start · streamed PCM frames · tts stop──┘
   LED lights up while the machine is speaking
```

## Hardware Setup (~$5 total)

| Component | ESP32-S3 Pin | Notes |
|---|---|---|
| **INMP441** (I2S Microphone) SCK | GPIO4 | I2S Clock |
| INMP441 WS | GPIO5 | Word Select |
| INMP441 SD | GPIO6 | Serial Data |
| INMP441 L/R | GND | Left channel |
| **MAX98357A** (I2S Amplifier) BCLK | GPIO15 | Bit Clock |
| MAX98357A LRC | GPIO16 | Left/Right Clock |
| MAX98357A DIN | GPIO7 | Data In |
| 4Ω 3W Speaker | MAX98357A +/− | Terminal |
| VDD (both modules) | 3V3 | Regulated 3.3V |

Configure GPIO pins in `sdkconfig.defaults`. Setting pins to `-1` disables the channel, allowing the project to build and run without audio hardware.

## How to Run

### 1. Launch Voice Pipeline Stack (Terminal 1)
```bash
# In tools/voice-stack (Docker Compose with GPU ASR and CPU TTS)
cd tools/voice-stack && docker compose up -d
```

### 2. Start AI Voice Mock-Cloud (Terminal 2)
```bash
export ANTHROPIC_API_KEY=sk-ant-...
go run ./tools/mock-cloud -ai -voice
```

### 3. Flash Firmware (Terminal 3)
```bash
cd examples/11-voice-assistant
idf.py set-target esp32s3 && idf.py menuconfig
idf.py flash monitor
```

Hold the BOOT button, say *"turn on the light"*, and release. The LED turns on and the speaker announces the confirmation!
