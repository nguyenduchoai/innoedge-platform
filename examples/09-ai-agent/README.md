# 09 — AI Agent: AI Decides, Hardware Executes

[English](README.md) | [Tiếng Việt](README_vi.md)

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
