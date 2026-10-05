# 07 — QR Payment (Instant Mobile Banking Payments)

[English](README.md) | [Tiếng Việt](README_vi.md)

Customers scan a dynamic QR code using banking or e-wallet apps; the hardware receives a certified payment notification within ~1–3 seconds.

## Hardware Requirements
Devkit board + BOOT button. In production, connect an LCD/OLED display or e-paper screen to render the QR code.

## Architecture Flow
```
Customer selects package
   ↓  innoedge_request_qr(20000)
Cloud creates intent + calls payment gateway (VietQR, PayOS, SePAY, Tingee)
   ↓  on_qr(payload, ...)
Device displays QR code on screen — Customer scans and transfers funds
   ↓  Instant bank webhook -> Cloud
   ↓  on_paid(intent_id, amount)
Device dispenses goods / activates relay
```

## Four Essential Rules

1. **Render `payload` VERBATIM.** Never parse, alter, or reconstruct the QR string. Depending on gateway configuration, the payload may be an EMV VietQR string, a direct wallet checkout URL, or a hosted image URL. The firmware must simply render whatever string it receives.
2. **Only `on_paid` permits dispensing.** Not "customer says they transferred", not "the QR code was displayed long enough". Only a cryptographically verified webhook unlocks hardware.
3. **Handle `on_qr_error` gracefully.** If the payment gateway is down and no backup provider is configured, the cloud **intentionally refuses to issue a QR code**. Showing a QR code that cannot be verified would result in real customer financial loss. Display the error message with a "Retry" button.
4. **Dynamic QR requires internet connectivity.** Unlike cash pulses (which buffer safely in the offline NVS queue), `innoedge_request_qr()` called while offline returns `ESP_ERR_INVALID_STATE` so the screen can instruct customers to pay with cash.

## Expected Output
```
I (15200) qr: QR 20000 VND · Ref GT012D00088 · Expires in 300s · intent=88
I (15210) qr: payload: 00020101021238570010A00000072701270006970422...
I (28400) qr: PAYMENT CONFIRMED: 20000 VND (intent=88) — Starting service
```

## Troubleshooting
| Symptom | Cause & Solution |
|---|---|
| `ESP_ERR_INVALID_STATE` | Device is unassigned or currently offline. |
| QR generated but `on_paid` never triggers | Gateway webhook URL is not pointing to your cloud server; verify gateway webhook configuration. |
| Banking app reports "Invalid QR code" | Firmware modified or truncated the payload instead of rendering it verbatim. |
| Frequent `on_qr_error` | Gateway API credentials expired or gateway service degraded. |
