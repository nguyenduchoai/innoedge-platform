# 15 — Centralized Multi-Zone IP Audio: Public Address & BGM

[English](README.md) | [Tiếng Việt](README_vi.md)

A commercial application for Public Address (PA) and multi-zone Background Music (BGM) distribution across retail chains, supermarkets, factories, schools, and enterprise facilities.

---

## Key Features

1. **Multi-Zone Management:**
   * Target broadcasts by zone: Zone 1 (Lobby / Checkout), Zone 2 (Showroom / Aisles), Zone ALL (Entire facility).
   * Play distinct content in designated areas without interrupting ongoing activities in other zones.
2. **Priority Paging Override:**
   * When an `audio_paging` command is received, the system automatically:
     - Dips or pauses the background music stream.
     - Plays a two-tone "Ding-Dong" chime.
     - Boosts volume to an authoritative paging level (85%).
     - Broadcasts the announcement audio.
     - Smoothly restores background music at the previous volume once the announcement concludes!
3. **Emergency Fire Alarm Siren:**
   * A critical `audio_emergency` command overrides all zones at 100% volume with an evacuation alarm sound.
4. **On-Demand Commercial Jukebox:**
   * Diners in a restaurant or cafe can scan an on-table VietQR code (10,000 VND) to request their favorite track to play over the venue's sound system!

---

## Hardware Options

* **Microcontroller:** ESP32-S3 connected to an I2S DAC/Amp (MAX98357A, PCM5102, or ES8311 codec).
* **Or Linux SBC:** Raspberry Pi 4/5 or Banana Pi connected to facility amplifiers via 3.5mm AUX, HDMI, or USB audio (using `linux/python/examples/pi_central_audio.py`).
* **Speakers:** Ceiling-mount 70V/100V line speakers or low-impedance PA speakers.

---

## How to Run

### Step 1: Start Mock-Cloud
```bash
cd tools/mock-cloud && go run .
```

### Step 2: Build & Flash
```bash
cd examples/15-central-audio
idf.py set-target esp32s3
idf.py menuconfig
idf.py build flash monitor
```

The device streams background music, accepts multi-zone paging overrides, and handles jukebox payment requests seamlessly.
