# 14 — Digital Signage & Smart Billboard: Interactive Display

[English](README.md) | [Tiếng Việt](README_vi.md)

A complete commercial application that turns LED matrix panels (HUB75), SPI TFT displays, or HDMI television screens into a **cloud-managed Smart Digital Signage Billboard** powered by the InnoEdge Cloud.

---

## Key Features

1. **Centralized Remote CMS:**
   * Distribute playlists, text tickers, and media schedules remotely over WebSocket.
   * Instant updates without physical USB drive swaps.
2. **Proof of Play (POP) Telemetry:**
   * Each time an advertisement slot completes its duration, the device publishes an authenticated `proof_of_play` event to the cloud for advertiser billing and verification audits.
3. **Emergency Priority Override:**
   * An emergency command (`signage_emergency`) from the cloud immediately preempts commercial ad loops to display public security alerts, evacuation sirens, or missing child notifications.
4. **Self-Service Ad Booking:**
   * Onlookers can scan an on-screen QR code (e.g., 50,000 VND) to purchase a 5-minute personal ad slot (birthday greeting, proposal message, or local business promotion) directly on the screen!

---

## Hardware Options

* **Microcontroller:** ESP32-S3 DevKit driving a HUB75 LED Matrix or ST7789 TFT screen.
* **Or Single Board Computer (SBC):** Raspberry Pi 4/5 or Banana Pi driving a 32–65 inch TV via HDMI (using `linux/python/examples/pi_digital_signage.py`).
* **Onboard Buttons/LEDs:** BOOT button (GPIO0) simulates user ad booking; Status LED on GPIO2.

---

## How to Run

### Step 1: Start Mock-Cloud
```bash
cd tools/mock-cloud && go run .
```

### Step 2: Build & Flash
```bash
cd examples/14-digital-signage
idf.py set-target esp32s3
idf.py menuconfig
idf.py build flash monitor
```

The screen rotates through scheduled media slots and periodically dispatches Proof-of-Play telemetry back to the cloud console.
