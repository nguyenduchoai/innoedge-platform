# 04 — Device Config (Dynamic Cloud Pricing & Offline Cache)

[English](README.md) | [Tiếng Việt](README_vi.md)

Operators update prices or combo packages in the mobile app → The device adopts new settings immediately without rebooting, and remains operable at correct prices even during internet outages.

## Architecture Flow
```
Operator modifies price in app
   ↓
Cloud sends {"action":"config_updated","params":{"version":4}}
   ↓
SDK triggers your callback → innoedge_config_reload()
   ↓
GET /api/device/config  →  Saves raw JSON + version to NVS flash
   ↓
Device reads persistent cache. Works 100% offline.
```

## Build & Flash
```bash
idf.py set-target esp32s3 && idf.py flash monitor
```

## Expected Output
```
W (900)  config: No cached config in NVS — falling back to compile-time defaults
I (6100) config: Configuration ready, version=3
I (6110) config: ── Current Config (v3) ──
I (6120) config:   Unit price: 1,000 VND / coin
I (6130) config:   Combo: Standard Wash — 40,000 VND
```
Update pricing on dashboard → Within a few seconds:
```
I (41200) innoedge: Dynamic command id=91 action=config_updated
I (41800) config: Configuration ready, version=4
```

## Three Essential Rules

1. **Read cache BEFORE connecting.** Call `dump_config()` immediately after `innoedge_init()`, before `innoedge_start()`. The machine must be ready to serve customers on second one without waiting for WiFi.
2. **Network errors must NEVER clear existing cache.** The SDK preserves old cached parameters when fetch fails. Never write fallback logic that resets prices to zero.
3. **Always ACK "ok" on network reload errors.** The old cache is still valid; returning an error only causes pointless retry spam.

## Configuration Schema Example
```jsonc
{
  "pricing": { "coinPerBillVnd": 1000, "coinPerQrVnd": 1000 },
  "combos": [
    {
      "id": "A",
      "name": "Standard Wash",
      "priceVnd": 40000,
      "payload": { "steps": [{ "device": "water", "seconds": 120 }] }
    }
  ],
  "dynamic": { "ui_language": "en" }
}
```
*Note:* The configuration payload has a ceiling of **4096 bytes**. Large assets (audio/video files) should be downloaded from dedicated asset endpoints.
