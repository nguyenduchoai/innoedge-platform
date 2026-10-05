# 02 — Telemetry: Money & Alerts

[English](README.md) | [Tiếng Việt](README_vi.md)

Dispatch financial transactions and hardware alerts to the cloud with crash-safe NVS persistence against network drops and sudden power loss.

## Hardware Requirements
Only a devkit board — uses the onboard **BOOT button (GPIO0)**.

| Action | Meaning |
|---|---|
| Quick press BOOT | Customer inserts 1 coin → `innoedge_publish_payment()` |
| Hold BOOT > 2s | Coin mechanism jammed → `innoedge_alert(..., true)` |
| Release BOOT | Coin jam cleared → `innoedge_alert(..., false)` |

## Build & Flash
```bash
idf.py set-target esp32s3 && idf.py flash monitor
```

## The Crucial Test: Zero Lost Money on Network Drop

1. When the device is online, press BOOT → Log shows `cloud acknowledged payment`, `queue_depth=0`.
2. **Turn off your WiFi router** (or unplug network cable).
3. Press BOOT 5 times → Log shows `queue_depth=1,2,3,4,5` — no cloud acknowledgement.
4. **Turn WiFi back on**.
5. Within ~3s: 5 acknowledgment logs stream back from the cloud, and `queue_depth` returns to 0.
6. Unplug the USB cable during Step 3 and plug back in → The payment queue remains completely intact inside NVS flash memory.

## 4 Supported Payment Kinds
```c
innoedge_publish_payment(INNOEDGE_PAY_COIN,     3, 0);      // Customer inserted 3 coins
innoedge_publish_payment(INNOEDGE_PAY_CASH,     0, 20000);  // Bill acceptor: 20,000 VND
innoedge_publish_payment(INNOEDGE_PAY_TICKET,  25, 0);      // Redeem 25 prize tickets
innoedge_publish_payment(INNOEDGE_PAY_COIN_OUT, 10, 0);     // Hopper dispensed 10 coins
```
*Note:* `TICKET` and `COIN_OUT` do not increment revenue — they are used for financial audits and hopper balance tracking.

## Hardware Alert Rules
- `code` is a machine-readable identifier (`coin_jam`, `coin_empty`, `paper_empty`, `no_water`, etc.).
- The cloud deduplicates on `(device_id, code)`: exactly 1 active alert per code; repeated triggers simply increment the counter.
- `active=false` marks the alert as resolved. **Always remember to clear alerts** once the hardware condition returns to normal.
- Safe to call while offline (records state safely without crashing).

## Under the Hood: What the SDK Handles for You
| Feature | Details |
|---|---|
| Monotonic `seq` counter | Persisted to NVS, used by cloud for idempotency |
| NVS write before transmission | Zero lost money on sudden power failure |
| Automatic retry until ACK | Retries every 3s + immediately upon previous ACK |
| Queue near capacity | Automatically raises `payment_queue_backlog_high` alert (80% threshold) |
| Queue overflow safety | Automatically raises `payment_queue_overflow_drop` **critical** alert — never drops money silently |

## Troubleshooting
| Symptom | Cause & Solution |
|---|---|
| No cloud acknowledgment while online | Device not yet assigned to partner — cloud rejects unassigned payments (intended design; money stays safely in queue). |
| Queue depth grows and never decreases | Check device assignment status from Example 01. |
| Alert still shows on app after hardware fix | Alert was triggered with `active=true` but not cleared with `active=false`. |
