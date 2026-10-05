# 01 — Hello Device

[English](README.md) | [Tiếng Việt](README_vi.md)

Bring a clean ESP32 online to the InnoEdge Cloud. This is the first example you should run.

## Requirements
- ESP-IDF ≥ 5.1
- Any ESP32-S3 board (no external screen or relay needed)
- A cloud endpoint — **no account needed**, use the included local mock-cloud

## Step 1: Run the Local Mock-Cloud (Separate Terminal)
```bash
go run ../../tools/mock-cloud
```
The terminal prints the local LAN IP to use in the next step. See [`tools/mock-cloud`](../../tools/mock-cloud) for details.

## Step 2: Build & Flash
```bash
idf.py set-target esp32s3
idf.py menuconfig      # InnoEdge SDK → Cloud base URL → http://<mock-cloud-ip>:8080
idf.py flash monitor
```

## Configuration
| Location | Parameter | Description |
|---|---|---|
| `../sdkconfig.defaults` | `CONFIG_GTEK_SERVER_BASE_URL` | Cloud server URL (default placeholder MUST be updated) |
| `menuconfig` | InnoEdge SDK | All infrastructure parameters |

## Expected Output

First boot (device has no WiFi credentials):
```
W (2100) hello: WAITING FOR WIFI PROVISIONING — Open app or web to configure
```

Three ways to configure WiFi — **no mobile app required**:

| Method | How-To | Best For |
|---|---|---|
| **Web Browser (BLE)** | Open Chrome/Edge to `http://localhost:8080/provision/` (or [`tools/web-provision`](../../tools/web-provision)), click "Scan & Connect" via Web Bluetooth | Demos, fast testing, no install |
| **Captive Portal** | Connect to WiFi network `GTEK-Setup-XX:XX`, browse to `http://192.168.4.1`, submit credentials | Classroom, offline setups |
| **Pre-compiled WiFi** | `menuconfig → InnoEdge SDK → Factory WiFi SSID/password` | Test benches, automated CI |

The device runs **both SoftAP and BLE simultaneously**. Once credentials are received, it automatically reboots and connects.

After WiFi connects:
```
I (1200) innoedge: SDK 1.0.0 · device=AABBCCDDEEFF · fw=0.1.0 · assigned=0
I (1210) hello: device_id (MAC) = AABBCCDDEEFF — use this ID to assign device
W (4300) hello: Device NOT yet assigned to a partner — scan QR in app to activate
I (5000) hello: online=yes  assigned=no  queue_depth=0
```
→ Assign the device in the Web Dashboard (`http://localhost:8080/`) using the `device_id` above → Device activates:
```
I (9100) innoedge: platform command: activation_complete
I (9110) hello: Device assigned to partner — ready to serve customers
```

The device is now online on the dashboard, sends heartbeats every 30s, and is ready to receive commands and OTA updates.

## Troubleshooting
| Symptom | Cause & Solution |
|---|---|
| `CONFIG_GTEK_SERVER_BASE_URL is still placeholder` | You forgot to set your mock-cloud LAN IP in `menuconfig`. |
| `WIFI_EVENT_STA_DISCONNECTED` | Wrong WiFi SSID/password, or 5GHz WiFi used (ESP32 only supports 2.4GHz). |
| BLE provisioning not found | Check if Bluetooth is enabled on your computer or phone. |
