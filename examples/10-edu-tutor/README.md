# 10 — Edu Tutor: Smart Interactive Learning Assistant

[English](README.md) | [Tiếng Việt](README_vi.md)

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
