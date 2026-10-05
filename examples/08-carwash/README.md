# 08 — Car Wash (Multi-Relay Timed Sessions & Budget Quotas)

[English](README.md) | [Tiếng Việt](README_vi.md)

A complete production product pattern: remote cloud commands + dynamic pricing configuration + multi-relay control + hardware safety interlocks.

## Operational Model
A wash combo is **not** a rigid automated sequence. It is a **flexible second budget allocated across devices**, allowing customers to switch between tools at will:

```
Combo "Standard Wash" = { Water: 120s, Foam: 60s, Air: 60s, Vacuum: 0s }

Customer presses WATER  → Water relay turns ON, water budget decrements
Customer presses FOAM   → Water relay turns OFF, foam relay turns ON, foam budget decrements
Water budget reaches 0  → WATER button locks out, other available tools remain usable
All budgets reach 0     → Session closes, all relays immediately turn OFF
```

**Mutual Hardware Exclusion:** Only **one high-power pump/relay can run at any given moment** to prevent voltage drop and pump burnout.

## Wiring Diagram

| ESP32 Pin | Connected Device |
|---|---|
| GPIO4 | High-pressure water pump relay |
| GPIO5 | Snow foam pump relay |
| GPIO6 | Compressed air solenoid relay |
| GPIO7 | Vacuum motor relay |
| GPIO15 | Water pressure switch (Optional, active-low) |

*Simulation:* Setting GPIOs to `-1` executes the state machine in simulation mode with console logging.

## Build & Flash
```bash
idf.py set-target esp32s3 && idf.py flash monitor
```

## Testing via Cloud Command
Send a session start command from the Web Console:
```jsonc
{"type":"command","commandId":150,"action":"start_wash","params":{"combo":"A"}}
```
Console output:
```
I (30100) carwash: Session opened: water=120s foam=60s air=60s vacuum=0s
I (35100) carwash: Remaining: water=120s foam=60s air=60s · session_timer=240s
I (50100) carwash: Demo: Customer pressed WATER button → Activated
I (55100) carwash: Remaining: water=115s foam=60s air=60s · session_timer=225s
```

## Four Layers of Hardware Safety Interlocks

1. **Mutual Relay Exclusion:** Turning ON any relay automatically turns OFF all other competing relays.
2. **Session Timeout Watchdog:** A global countdown timer forces all relays OFF even if a customer abandons the bay mid-session.
3. **No-Water Pressure Sensor Alert:** If the water pressure sensor triggers active-low during pump operation, the pump stops immediately and an `alert_no_water` is dispatched to the cloud.
4. **Offline Cache Continuity:** Combos and duration budgets are cached in NVS so existing sessions never freeze during internet drops.
