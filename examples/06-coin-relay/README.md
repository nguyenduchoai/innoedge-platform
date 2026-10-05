# 06 — Coin & Relay (Complete 2-Way Coin-Op Machine)

[English](README.md) | [Tiếng Việt](README_vi.md)

Money in (coin/bill pulses) and money out (solenoid/relay pulse credit) — a complete bidirectional commercial machine.

## Wiring Diagram

| ESP32 Pin | Connects to | Notes |
|---|---|---|
| GPIO4 | COIN pulse pin of coin acceptor | Active-low, requires pull-up |
| GND | GND of coin acceptor | **Common ground mandatory** |
| GPIO42 | IN pin of relay module | Active-high / active-low selectable |
| 12V DC | 12V power supply for acceptor | Separate power supply, DO NOT power from ESP32 5V |

Configure GPIO pins in `sdkconfig.defaults` or `menuconfig → InnoEdge HW drivers`.

> **Caution:** Coin acceptors run on 12V DC, while the ESP32 operates at 3.3V logic. Connecting a 12V pulse directly to a GPIO pin will **permanently burn the chip**. Always use an optocoupler (e.g., PC817) or voltage divider.

## Build & Flash
```bash
idf.py set-target esp32s3
idf.py flash monitor
```
*Note:* You can test without hardware by setting GPIO pins to `-1`, which runs the driver in simulation log mode.

## Expected Output
Customer inserts 3 coins:
```
I (12400) coinop: Customer inserted 3 coins
I (12900) telemetry: Cloud recorded payment: method=coin count=3 amount=3000 unit_price=1000
```
Cloud dispatches dispense command `{"action":"dispense","params":{"amountVnd":20000}}`:
```
I (30100) innoedge: Dynamic command id=112 action=dispense
I (30400) coinop: Dispensed 2 pulses to relay
```

## Three Golden Rules of Monetized Hardware

1. **Firmware NEVER multiplies prices.** Send **raw coin counts** to the cloud; let the cloud compute fiat currency. Different operators set different pricing in their mobile apps — hardcoding prices into firmware requires re-flashing your entire fleet whenever prices change.
2. **Always enforce hardware safety ceilings.** `GTEK_DISPENSE_MAX_PULSES` prevents a malicious or buggy cloud payload from emptying your entire hopper.
3. **Rely on SDK idempotency.** The SDK saves the high-watermark `commandId` **in NVS before** firing the relay. Sudden power loss during dispensing will safely reject duplicate retries upon reboot.

## Hardware Pulse Debounce
Mechanical coin acceptors produce noisy electrical pulses. Adjust these two parameters for your specific mechanism:

| Parameter | Meaning | Adjustment Rule |
|---|---|---|
| `GTEK_PULSE_MIN_MS` (35ms) | Shorter pulses are ignored as noise | If phantom coins are counted → increase |
| `GTEK_PULSE_GAP_MS` (300ms) | Silence duration marking the end of a coin burst | If 5 coins register as 2 + 3 → increase |
