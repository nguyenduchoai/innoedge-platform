# 05 — OTA (Over-The-Air Firmware Updates)

[English](README.md) | [Tiếng Việt](README_vi.md)

Your application **writes zero lines of OTA transport code**. You simply provide one callback: "Is the machine currently busy serving a customer?".

## What the SDK Does Automatically
```
Receive manifest → Compare versions → Download HTTPS stream → Verify SHA-256
   → Write passive partition → Reboot → Authenticate with cloud → Validate (cancel rollback)
                                       ↘ Panic / Crash → Bootloader rolls back to previous app
```

## What Your Application Implements
```c
static bool is_busy(void) { return currently_serving_customer; }

innoedge_config_t cfg = { .busy_check = is_busy };
```
If you omit `busy_check`, a device might reboot while a customer is actively inserting coins or scanning a QR code.

## Build & Testing
```bash
idf.py set-target esp32s3 && idf.py flash monitor
```
1. Increment `CONFIG_GTEK_FW_VERSION` to `"0.1.1"` in `menuconfig` or `sdkconfig.defaults`, then run `idf.py build`.
2. Upload `build/ie-ota.bin` to your cloud as `gtek-fw-0.1.1.bin`.
3. The device checks for updates periodically (or triggers immediately upon command `ota_check`).

While `is_busy()` returns true, logs will confirm that the update and reboot are **safely postponed**.

## Two Mandatory Partition Table Flags
```ini
CONFIG_PARTITION_TABLE_TWO_OTA=y        # Requires 2 application slots
CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y # Roll back if new version crashes
```
These are enabled by default in `examples/sdkconfig.defaults`.

## Why OTA Runs in the Background
Legacy IoT implementations checked OTA synchronously on boot: a slow 2G/3G network caused a 2-minute freeze before the screen could render, leading customers to believe the machine was broken. InnoEdge offloads OTA checking and downloading to a low-priority background task so the user interface launches instantly.

## Golden Rule: Never call `esp_ota_mark_app_valid_cancel_rollback()` manually
The SDK calls this function automatically only after the WebSocket connection successfully establishes with the cloud. Calling it earlier (such as in `app_main`) disables the rollback mechanism, meaning a buggy firmware unable to connect to the cloud will permanently brick the device in the field.

## Troubleshooting
| Symptom | Cause & Solution |
|---|---|
| Device reboots after OTA and reverts to old version | Working as designed — the new firmware panicked or failed to connect to the cloud. |
| Device never downloads new version | Cloud version ≤ running version, or device is outside staged rollout percentage. |
| Download finishes but device never reboots | `busy_check` returns `true` continuously — ensure your busy flag is cleared when service completes. |
