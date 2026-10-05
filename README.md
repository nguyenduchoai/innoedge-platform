# InnoEdge Platform

**Open-source Edge AI & IoT monetization platform — from ESP32 & Arduino to Raspberry Pi & Linux.**

[English](README.md) | [Tiếng Việt](README_vi.md)

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-%E2%89%A55.1-red.svg)](https://docs.espressif.com/projects/esp-idf/)
[![Linux SBC](https://img.shields.io/badge/Linux%20SBC-Raspberry%20Pi%20%7C%20Banana%20Pi-orange.svg)](linux/)
[![Arduino](https://img.shields.io/badge/Arduino%20%26%20PlatformIO-Supported-teal.svg)](arduino/)
[![MicroPython](https://img.shields.io/badge/MicroPython-STEM%20Robot-yellow.svg)](micropython/)
[![Protocol](https://img.shields.io/badge/Protocol-v1%20Open-green.svg)](docs/PROTOCOL-v1.md)
[![Examples](https://img.shields.io/badge/Examples-15%20Ready-brightgreen.svg)](examples/)
[![Cookbooks](https://img.shields.io/badge/Cookbooks-9%20Hardware%20Designs-purple.svg)](docs/COMMUNITY-COOKBOOKS.md)

---

InnoEdge solves the heavy, mission-critical IoT infrastructure that every commercial hardware team has to reinvent from scratch:

* **Zero lost revenue on network drops:** Every coin, bill, and transaction pulse is written to persistent NVS storage before cloud dispatch. Automatic de-duplication guarantees no dropped or double-counted money.
* **Zero double-dispensing:** An idempotent command bus tracks high-watermark `commandId`s in NVS *before* firing relays. Survives sudden power cuts and reboots without repeat actuation.
* **Brick-proof OTA updates:** Background firmware updates with SHA-256 verification and automatic rollback if the new app cannot reach the cloud. Postpones reboots while customers are paying.
* **Universal cross-platform runtime:** Identical WebSocket protocol across ultra-low-cost microcontrollers ($2 ESP32 via ESP-IDF C & Arduino) and high-performance single-board computers (Raspberry Pi, Banana Pi, Orange Pi via Python SDK & Go Daemon).
* **Native Edge AI & visual coding:** Built-in MCP server for AI coding agents (Claude Code, Cursor), local voice pipeline (Qwen3-ASR + VieNeu TTS), and Scratch 3.0 drag-and-drop extension for STEM education.

```c
// ESP32 (ESP-IDF C) — Ready in 10 minutes
#include "innoedge.h"

void app_main(void)
{
    innoedge_config_t cfg = { .fw_version = "1.0.0" };
    innoedge_init(&cfg);
    innoedge_start();
}
```

```python
# Raspberry Pi & Banana Pi (Python SDK)
from innoedge import InnoEdge

app = InnoEdge(cloud_url="wss://cloud.innoedge.io/ws", fw_version="1.0.0")

@app.on_paid
def on_paid(intent_id, amount):
    print(f"Payment confirmed: {amount:,} VND -> Dispense item / Start service")

app.start()
```

---

## Table of Contents

1. [Cross-Platform Architecture](#cross-platform-architecture)
2. [Quick Start in 10 Minutes](#quick-start-in-10-minutes)
3. [15 Production-Ready Examples](#15-production-ready-examples)
4. [9 Hardware Reference Cookbooks](#9-hardware-reference-cookbooks)
5. [Zero-Install Web Developer Tools](#zero-install-web-developer-tools)
6. [Public C API](#public-c-api)
7. [The Hard Engineering Problems InnoEdge Solves](#the-hard-engineering-problems-innoedge-solves)
8. [Directory Layout](#directory-layout)
9. [Automated Testing](#automated-testing)
10. [Open Core Business Model & Licensing](#open-core-business-model--licensing)

---

## Cross-Platform Architecture

InnoEdge maintains a strict separation of concerns: **Standardized Infrastructure & Transport** ↔ **Custom Business Logic & Actuators**.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                          INNOEDGE CLOUD PLATFORM                         │
│  Multi-Tenant CMS · Automated Bank Reconciliation · Fleet Management    │
│  VietQR & Banking Webhooks (SePAY, PayOS, Tingee) · Staged OTA Rollout  │
└────────────────────────────────────┬────────────────────────────────────┘
                                     │ WebSocket / TLS (Protocol v1)
                                     ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    CROSS-PLATFORM DEVICE RUNTIME                        │
├──────────────────────────┬──────────────────────────┬───────────────────┤
│   Microcontrollers (MCU) │   Single Board (SBC)     │   STEM Education  │
├──────────────────────────┼──────────────────────────┼───────────────────┤
│ • ESP32 / ESP32-S3 (C)   │ • Raspberry Pi (3B/4/5)  │ • MicroPython     │
│ • Arduino / PlatformIO   │ • Banana Pi / Orange Pi  │   InnoBot HAL     │
│ • Cost-optimized ($2-5)  │ • Python SDK + libgpiod  │ • Scratch 3.0     │
│ • Ideal for: Relays,     │ • Go Background Agent    │   BlockStudio     │
│   Vending, EV, Laundromat│ • Ideal for: Kiosks,     │ • Ideal for:      │
│                          │   Signage, Multi-Zone IP │   Classroom, DIY  │
└──────────────────────────┴──────────────────────────┴───────────────────┘
```

For platform comparison and pinout details, see [`docs/CROSS-PLATFORM-SBC.md`](docs/CROSS-PLATFORM-SBC.md).

---

## Quick Start in 10 Minutes

**Requirements:** A computer (macOS, Linux, or Windows), an ESP32-S3 devkit (or Raspberry Pi), and no account registration required.

### Step 1: Start the Local Mock-Cloud & Web Dashboard (Terminal 1)

The repo includes a self-contained Go mock-cloud with bank webhook simulators (SePAY, PayOS, Tingee, Pay2S), real-time Web Console, and AI Assistant:

```bash
git clone https://github.com/nguyenduchoai/innoedge-platform.git
cd innoedge-platform
go run ./tools/mock-cloud
```

* Open your browser to `http://localhost:8080/` to access the **Real-Time Web Dashboard**.
* Inspect connected devices, send remote dispense commands, and test payment webhooks.

### Step 2: Flash Firmware (Terminal 2)

```bash
cd examples/01-hello-device
idf.py set-target esp32s3
idf.py menuconfig        # InnoEdge SDK -> Cloud base URL -> set to your computer's LAN IP
idf.py flash monitor
```

*(Alternatively, flash directly from Chrome/Edge with zero local tools using the [Web Serial Flasher](tools/web-flasher/)).*

### Step 3: Zero-App WiFi Provisioning via Web Bluetooth

Open Chrome to `http://localhost:8080/provision/` (or open [`tools/web-provision/index.html`](tools/web-provision/index.html) directly), click **"Scan & Connect"**, enter your WiFi SSID and password, and click Send. The device connects to WiFi, syncs NTP time, and immediately appears online on your Web Dashboard!

---

## 15 Production-Ready Examples

Every example includes complete source code, wiring diagrams, expected serial output, and a dedicated **Troubleshooting** guide:

| # | Example | Platform | Real-World Use Case | Required Hardware |
|---|---|---|---|---|
| **01** | [hello-device](examples/01-hello-device) | ESP32-S3 | Web Bluetooth provisioning, cloud connection, heartbeat | Devkit |
| **02** | [telemetry](examples/02-telemetry) | ESP32-S3 | Coin/cash counting ledger, crash-safe NVS queue | Devkit |
| **03** | [remote-command](examples/03-remote-command) | ESP32-S3 | Remote actuator commands, reboot-safe de-duplication | Devkit |
| **04** | [device-config](examples/04-device-config) | ESP32-S3 | Dynamic pricing & operational config, offline cache | Devkit |
| **05** | [ota](examples/05-ota) | ESP32-S3 | Secure TLS OTA updates, busy-state delay, auto-rollback | Devkit |
| **06** | [coin-relay](examples/06-coin-relay) | ESP32-S3 | 2-way coin-op machine: pulse reading & relay dispensing | Coin acceptor, Relay |
| **07** | [qr-payment](examples/07-qr-payment) | ESP32-S3 | Dynamic VietQR generation, instant bank webhook dispense | Devkit + Display |
| **08** | [carwash](examples/08-carwash) | ESP32-S3 | Multi-relay timed wash bays (foam, high-pressure, vacuum) | 4-Relay board |
| **09** | [ai-agent](examples/09-ai-agent) | ESP32-S3 | LLM tool-calling (Claude) makes autonomous actuator decisions | Devkit + Claude |
| **10** | [edu-tutor](examples/10-edu-tutor) | ESP32-S3 | Bidirectional AI tutor: spoken questions & interactive buttons | Devkit + 3 Buttons |
| **11** | [voice-assistant](examples/11-voice-assistant) | ESP32-S3 | Self-hosted voice AI: Push-to-talk -> Qwen3-ASR -> LLM -> VieNeu TTS | I2S Mic + Amp |
| **12** | [muse-gadget](examples/12-muse-gadget) | ESP32-S3 | Meta Muse AI Avatar Kiosk + QR-based automated vending | Devkit + LCD + Relay |
| **13** | [stem-robot](examples/13-stem-robot) | ESP32 / Pi | Autonomous delivery robot: ultrasonic obstacle avoidance, QR hatch | 2 Motors, HC-SR04, Servo |
| **14** | [digital-signage](examples/14-digital-signage) | ESP32 / Pi | Smart billboard: Proof-of-Play telemetry, emergency override, ad buy | HDMI / LCD Screen |
| **15** | [central-audio](examples/15-central-audio) | ESP32 / Pi | Multi-zone IP audio: BGM stream, priority paging, fire alarm, jukebox | I2S Amp / 3.5mm AUX |

---

## 9 Hardware Reference Cookbooks

The community hardware reference guide [`docs/COMMUNITY-COOKBOOKS.md`](docs/COMMUNITY-COOKBOOKS.md) provides complete schematics, opto-isolated bill of materials (BOM), and production firmware recipes for 9 industries:

1. **Smart Locker Systems:** Multi-compartment solenoid control, storage time pricing, QR unlocking.
2. **EV & E-Bike Charging Stations:** High-power relay control, energy metering (PZEM-004T), pay-per-kWh.
3. **24/7 Commercial Laundromat:** Opto-isolated industrial washer triggers, quick-wash / dry cycle selection.
4. **Self-Service Carwash Stations:** Digital timer countdown, high-pressure pump, foam cannon, vacuum control.
5. **AI Interactive Kiosks:** Touchscreen interface, conversational AI avatar customer service, contactless payments.
6. **Autonomous STEM Delivery Robots:** Smart delivery rovers navigating indoor environments and unlocking storage on payment.
7. **Coffee & Beverage Vending Machines:** Motorized spiral spirals, drop detection beam sensors to prevent coin traps.
8. **Digital Signage & Smart Billboards:** Proof-of-play (POW) advertiser audit trails, emergency broadcast interruptions.
9. **Centralized Multi-Zone IP Audio:** Facility-wide background music, priority paging announcements, 100% volume fire siren override.

See detailed schematic diagrams and isolation circuits at [`docs/HARDWARE-REFERENCE.md`](docs/HARDWARE-REFERENCE.md).

---

## Zero-Install Web Developer Tools

Developers can configure, test, and program InnoEdge devices right from modern web browsers:

| Tool | Directory | Capabilities |
|---|---|---|
| **InnoEdge BlockStudio** | [`tools/scratch/`](tools/scratch/) | Visual Scratch 3.0 drag-and-drop extension for STEM education and beginners. |
| **Web 1-Click Flasher** | [`tools/web-flasher/`](tools/web-flasher/) | Flash embedded firmware directly from Chrome or Edge via Web Serial API. |
| **Web Bluetooth Provisioning** | [`tools/web-provision/`](tools/web-provision/) | Install-free PWA to configure device WiFi via Bluetooth Low Energy (BLE). |
| **Mock-Cloud & Web Console** | [`tools/mock-cloud/`](tools/mock-cloud/) | Full protocol v1 emulator, live WebSocket monitor, and automated bank webhook triggers. |
| **InnoEdge Cloud Lite** | [`tools/cloud-lite/`](tools/cloud-lite/) | Complete Docker Compose bundle with Caddy auto-SSL for self-hosting. |
| **MCP Server for AI Coding** | [`tools/mcp/`](tools/mcp/) | Model Context Protocol server giving AI IDEs (Claude Code, Cursor) full SDK context. |

---

## Public C API

The core SDK provides **16 concise, standardized functions** declared in [`components/innoedge/include/innoedge.h`](components/innoedge/include/innoedge.h):

```c
// Lifecycle
esp_err_t   innoedge_init(const innoedge_config_t *cfg);
esp_err_t   innoedge_start(void);
bool        innoedge_is_online(void);
bool        innoedge_is_assigned(void);
const char *innoedge_device_id(void);

// Transactions & Telemetry (Crash-safe NVS queue)
esp_err_t   innoedge_publish_payment(innoedge_pay_kind_t kind, uint32_t count, int64_t amount_vnd);
esp_err_t   innoedge_publish_event(const char *name, const char *data_json);
esp_err_t   innoedge_send_binary(const void *data, size_t len);
uint32_t    innoedge_queue_depth(void);
esp_err_t   innoedge_alert(const char *code, innoedge_alert_severity_t severity, const char *message, bool active);

// Actuator Commands & Payments
esp_err_t   innoedge_register_command(const char *action, innoedge_command_fn fn);
void        innoedge_reboot_after_ack(void);
esp_err_t   innoedge_request_qr(int64_t amount_vnd);
esp_err_t   innoedge_config_json(char *out, size_t len, int *version);
esp_err_t   innoedge_config_reload(void);
esp_err_t   innoedge_ota_check(void);
```

---

## The Hard Engineering Problems InnoEdge Solves

* **Money is Never Dropped:** Coin/bill pulses are written to NVS *before* attempting WebSocket dispatch. The cloud deduplicates on `(device_id, seq)` so retries never create duplicate ledger records.
* **Actuators Never Fire Twice:** The cloud frequently retries commands over shaky cellular networks. InnoEdge records high-watermark `commandId`s in NVS *before* executing the hardware handler. A sudden reboot during a dispense operation will safely drop the duplicate retry.
* **The Golden Safety Rule:** Only the cryptographic `on_paid` event (certified by bank webhooks) is permitted to dispense inventory or activate high-power relays.
* **Brick-Proof OTA Upgrades:** New firmware is only validated after the device successfully authenticates with the cloud. Any panic or boot failure triggers automatic bootloader rollback to the previous partition.

---

## Directory Layout

```
.
├── components/innoedge/     # Core ESP-IDF C SDK
├── arduino/InnoEdge/        # Arduino & PlatformIO C++ library wrapper
├── micropython/             # InnoBot HAL & MicroPython for STEM robotics
├── linux/                   # Raspberry Pi & Banana Pi (Python SDK + Go Daemon)
├── components-hw/           # Hardware sample drivers (coin acceptor, relays, I2S)
├── examples/                # 15 runnable examples (01-hello to 15-central-audio)
├── tools/                   # Mock-cloud, Web Flasher, Web Provision, Scratch, MCP
├── docs/                    # PROTOCOL-v1, HARDWARE-REFERENCE, COMMUNITY-COOKBOOKS
└── tests/run.sh             # Host test runner (runs without ESP-IDF or hardware)
```

---

## Automated Testing

InnoEdge includes an automated test runner that validates the entire stack locally without requiring hardware:

```bash
./tests/run.sh
```

Runs 6 test suites across multiple languages:
1. **C Command Bus:** Registry validation and reboot-safe de-duplication.
2. **Arduino C++ Wrapper:** Syntax checking and API compatibility.
3. **MicroPython InnoBot:** STEM robotics logic and sensor calculations.
4. **Linux SBC Python SDK:** Client state machine and idempotent command verification.
5. **Linux SBC Agent (Go):** Background daemon compilation check.
6. **Mock-Cloud & MCP Server:** Protocol v1 frame validation.

---

## Open Core Business Model & Licensing

InnoEdge adopts the proven **Open Core** business model used by industry-leading infrastructure platforms (e.g., ESPHome, Docker, MongoDB, Linux Foundation):

### 1. Open Source Foundation — Apache License 2.0 (Free Forever)
* Applies to: Core SDK (`components/innoedge`), Arduino library, MicroPython InnoBot, Linux Python SDK, 15 Examples, 9 Cookbooks, Mock-Cloud, and Protocol specifications.
* Commercial Rights: OEM manufacturers, system integrators, and independent developers can freely build commercial devices and embed the firmware without paying royalties or open-sourcing their proprietary product logic.

### 2. Commercial Monetization Strategies
The InnoEdge ecosystem enables 4 highly scalable revenue streams:

1. **Enterprise Cloud SaaS Subscription:**
   * Provide the commercial InnoEdge Cloud Enterprise for operators managing fleets of 100 to 10,000+ vending machines, laundromats, or charging stations.
   * Subscription pricing: **$1 – $3 / device / month**.
   * Key enterprise features: Multi-tenant tenant isolation, real-time revenue analytics, mobile owner app (iOS/Android), automated bank reconciliation, staged fleet OTA rollout, and 99.9% uptime SLA.
2. **Payment Revenue Sharing:**
   * Integrated payment gateways (VietQR, instant banking, mobile wallets).
   * Micro-fee per successful transaction (e.g., **0.5% – 1%** or fixed transaction fees).
3. **Standardized Hardware Reference Kits:**
   * Commercial production of certified InnoEdge Core Shields (isolated power, industrial optocouplers, 4G LTE/WiFi, terminal blocks) and STEM educational kits for schools.
4. **Enterprise Custom Solutions & Dedicated SLAs:**
   * On-premise private cloud deployments and 24/7 dedicated engineering support for large corporations.

---

## Contributing & Community

Contributions are welcome! Please report field bugs or submit pull requests:
* **Bug Reports:** Open an issue with your chip target, ESP-IDF version, and serial monitor log.
* **Pull Requests:** Keep PRs focused. Always verify that `./tests/run.sh` passes before submitting.
* **Security:** Review [SECURITY.md](SECURITY.md) for vulnerability disclosure.
