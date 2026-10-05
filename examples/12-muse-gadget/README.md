# 12 — Meta Muse Gadget: AI Avatar Kiosk & Autonomous Vending

[English](README.md) | [Tiếng Việt](README_vi.md)

Integrating the **Meta Muse Gadget SDK** ([facebookincubator/muse-gadget-sdk](https://github.com/facebookincubator/muse-gadget-sdk)) with the **InnoEdge Platform**.

This example demonstrates transforming an ordinary ESP32-S3 into a **Smart Interactive AI Vending Kiosk** featuring an expressive conversational Avatar combined with industrial-grade bank payment infrastructure and financial audit trails.

---

## Why Combine Meta Muse Gadget with InnoEdge?

| Dimension | Meta Muse Gadget SDK | InnoEdge Platform | The Synergy |
|---|---|---|---|
| **Core Focus** | Conversational AI, speech, expressive avatars | Real-money payment infrastructure & IoT safety | **Full-Stack Commercial AI Kiosk** |
| **User Interaction** | Natural voice dialogue, face rendering | Dynamic QR codes, audible chimes | Speak to order; screen displays dynamic payment QR |
| **Financial Integrity** | ❌ None | Persistent NVS ledger, idempotent command bus (`commandId`) | **Zero dropped funds, zero double dispenses** |
| **Payment Gateways** | ❌ None | VietQR, PayOS, SePAY, Tingee webhooks | Instant banking settlements |
| **Actuator Safety** | ❌ None | Relay duration watchdog (`RELAY_MAX_SECONDS`) | Prevents burnt coils and overflowing pumps |

```
    Customer: "I'd like an iced coffee, please."
                       │
                       ▼
       [ Meta Muse Gadget (Mic / ASR) ]
                       │
                       ▼
       [ Meta Muse Cloud AI Agent ]
         → Reasons intent & calls tool:
           request_payment(amount_vnd=25000, item="Iced Coffee")
                       │
                       ▼ (Command Bus)
         [ InnoEdge SDK on ESP32 ]
           → innoedge_request_qr(25000)
                       │
                       ▼
         [ InnoEdge Cloud / VietQR Gateways ]
           → Generates dynamic QR code
                       │
                       ▼ on_qr()
         [ Screen renders QR code alongside the Muse Avatar ]
                       │
             Customer scans with mobile banking app
                       │
                       ▼ Verified Webhook
         [ on_paid() received by ESP32 ]
           → Actuates relay to pour coffee (7s watchdog)
           → Commits transaction to persistent NVS ledger
           → Muse Avatar expresses joy and announces: "Thank you! Enjoy your coffee!"
```

## Recommended Hardware
* **Microcontroller:** ESP32-S3 DevKitC-1 (N8R8 or N16R8 with PSRAM).
* **Display:** ST7789 240x240 LCD, GC9A01 circular display, or SPI/QPSI AMOLED.
* **Relay Module:** 2-channel optocoupled relay (Channel 1: Black Coffee, Channel 2: Milk Coffee).
* **Audio Setup:** INMP441 I2S microphone + MAX98357A amplifier.

## Build & Flash
```bash
cd examples/12-muse-gadget
idf.py set-target esp32s3
idf.py menuconfig
idf.py flash monitor
```
