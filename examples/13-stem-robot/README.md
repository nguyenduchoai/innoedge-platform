# 13 — Autonomous STEM Robot: Smart Delivery & Service Rover

[English](README.md) | [Tiếng Việt](README_vi.md)

A comprehensive STEM educational project bridging **fundamental robotics** (motors, ultrasonic distance sensors, servo mechanisms) with **commercial IoT services** (mobile banking payments, cloud telemetry, hardware safety).

---

## Project Concept

Rather than a simple toy car driving aimlessly, this STEM robot is an **autonomous mini-delivery rover** designed for schools, classrooms, or cafes:

1. **Summon & Order:** A customer presses the button or scans in the app to request an order (10,000 VND).
2. **Instant QR Payment:** Customer transfers via bank app; webhooks authenticate funds.
3. **Robot Receives `on_paid`:**
   * Automatically departs toward the destination table.
   * HC-SR04 ultrasonic distance sensor continuously scans for obstacles — if an obstacle or pedestrian is detected (<15cm), it automatically emergency brakes to prevent collisions.
   * Upon arrival, an SG90 servo automatically lifts the parcel hatch so the customer can collect their item.
   * After 4 seconds, the hatch closes and the rover dispatches a completion telemetry report to the cloud!

---

## Component List (~$10–$15)

| Component | ESP32-S3 Pin | Purpose |
|---|---|---|
| **ESP32-S3 DevKitC-1** | — | Core MCU with WiFi/BLE |
| **L298N / TB6612 Dual H-Bridge** | IN1 (GPIO 4), IN2 (GPIO 5) | Left motor control |
| | IN3 (GPIO 6), IN4 (GPIO 7) | Right motor control |
| **HC-SR04 Ultrasonic Sensor** | TRIG (GPIO 15), ECHO (GPIO 16) | Collision avoidance |
| **SG90 Micro Servo** | PWM (GPIO 18) | Cargo hatch open/close (0°–90°) |
| **2WD Robot Chassis Kit** | — | Acrylic frame, DC gearmotors, caster |
| **Power Supply (2x 18650 Li-ion)** | LM2596 step-down to 5V | Powers motors and ESP32 |

---

## How to Test

### Step 1: Start Mock-Cloud
```bash
cd tools/mock-cloud
go run .
```

### Step 2: Flash Firmware
* **Browser Option (Zero Install):** Open [Web 1-Click Flasher](../../tools/web-flasher/index.html) in Chrome/Edge, plug in the USB cable, and click **"Flash Firmware"** (30s).
* **CLI Option:**
  ```bash
  cd examples/13-stem-robot
  idf.py set-target esp32s3
  idf.py menuconfig
  idf.py build flash monitor
  ```

Press the **BOOT** button to summon the rover, trigger payment, and watch the obstacle avoidance and servo actuation in action!
