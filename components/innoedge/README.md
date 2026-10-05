# InnoEdge SDK for ESP32

[![License: Apache 2.0](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://github.com/nguyenduchoai/innoedge-platform/blob/main/LICENSE)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-%E2%89%A55.1-red.svg)](https://docs.espressif.com/projects/esp-idf/)
[![Protocol](https://img.shields.io/badge/Protocol-v1%20Open-green.svg)](https://github.com/nguyenduchoai/innoedge-platform/blob/main/docs/PROTOCOL-v1.md)
[![Web Tools](https://img.shields.io/badge/Web%20Portal-Zero--Install-brightgreen.svg)](https://nguyenduchoai.github.io/innoedge-platform/)

**Open-source IoT infrastructure SDK for coin-operated, vending, car-wash, EV charging, and automated payment devices.**

---

## Overview

InnoEdge SDK solves the mission-critical, heavy IoT infrastructure that commercial hardware teams have to reinvent from scratch:

* **Zero Lost Revenue on Network Drops:** Coin and pulse transactions are written to persistent NVS storage before cloud dispatch. Automatic de-duplication on `(device_id, seq)` guarantees no dropped or double-counted money.
* **Actuators Never Fire Twice:** An idempotent command bus tracks monotonic `commandId`s in NVS *before* firing relays. Survives sudden power cuts and reboots without repeat actuation.
* **Brick-Proof OTA Updates:** Background firmware updates with SHA-256 verification and automatic rollback if the new app cannot reach the cloud. Postpones reboots while customers are paying.
* **Deterministic Zero-Fragmentation Memory:** Pre-allocated static block pools and lockless power-of-two circular ring buffers eliminate heap fragmentation for 24/7/365 uninterrupted uptime.
* **Multi-WAN Failover (WiFi ↔ 4G LTE):** Automatic connection health tracking and seamless switchover to secondary cellular uplink upon network degradation.
* **Fleet Clustering (Master-Worker Mesh):** Bridge up to 32 worker subnodes (washers, EV bays) via ESP-NOW/RS485 through a single master gateway.
* **Cryptographic Transaction Signing (TAC):** Tamper-proof HMAC-SHA256 Transaction Authentication Codes prevent NVS flash tampering.

---

## Installation

Add this component to your ESP-IDF project using the ESP Component Manager:

```bash
idf.py add-dependency "nguyenduchoai/innoedge^0.1.1"
```

Or add directly to your `main/idf_component.yml`:

```yaml
dependencies:
  nguyenduchoai/innoedge: "^0.1.1"
```

---

## Quick Start (C)

```c
#include "innoedge.h"
#include "esp_log.h"

static const char *TAG = "app";

// Handle remote actuator command from Cloud
static esp_err_t on_dispense(cJSON *params, char *result, size_t rl, char *msg, size_t ml)
{
    ESP_LOGI(TAG, "Dispensing item...");
    // Trigger relay GPIO here
    snprintf(result, rl, "{\"pulses\":2}");
    snprintf(msg, ml, "Dispensed 2 items");
    return ESP_OK;
}

void app_main(void)
{
    // 1. Initialize SDK
    innoedge_config_t cfg = {
        .fw_version = "1.0.0",
        .heartbeat_sec = 30,
    };
    ESP_ERROR_CHECK(innoedge_init(&cfg));

    // 2. Register idempotent command handlers
    innoedge_register_command("dispense", on_dispense);

    // 3. Start networking & cloud connection
    ESP_ERROR_CHECK(innoedge_start());

    // 4. Record payment (crash-safe, saved to NVS first)
    innoedge_publish_payment(INNOEDGE_PAY_COIN, 2, 20000);
}
```

---

## Public C API Summary

Declared in [`include/innoedge.h`](https://github.com/nguyenduchoai/innoedge-platform/blob/main/components/innoedge/include/innoedge.h):

### Lifecycle
```c
esp_err_t   innoedge_init(const innoedge_config_t *cfg);
esp_err_t   innoedge_start(void);
bool        innoedge_is_online(void);
bool        innoedge_is_assigned(void);
const char *innoedge_device_id(void);
```

### Transactions & Telemetry
```c
esp_err_t   innoedge_publish_payment(innoedge_payment_kind_t kind, int count, int64_t amount_vnd);
esp_err_t   innoedge_publish_event(const char *name, const char *data_json);
esp_err_t   innoedge_send_binary(const uint8_t *data, size_t len);
uint32_t    innoedge_queue_depth(void);
esp_err_t   innoedge_alert(const char *code, const char *severity, const char *message, bool active);
```

### Actuator Commands & Dynamic QR
```c
esp_err_t   innoedge_register_command(const char *action, innoedge_command_fn fn);
void        innoedge_reboot_after_ack(void);
esp_err_t   innoedge_request_qr(int64_t amount_vnd);
esp_err_t   innoedge_config_json(char *out, size_t out_len, int *version);
esp_err_t   innoedge_config_reload(void);
esp_err_t   innoedge_ota_check(void);
```

### Enterprise Resiliency & Diagnostics
```c
esp_err_t   innoedge_blackbox_record(const char *tag, const char *details);
esp_err_t   innoedge_blackbox_get_report(char *out, size_t out_len);
innoedge_net_interface_t innoedge_net_active_interface(void);
esp_err_t   innoedge_net_report_link(innoedge_net_interface_t iface, bool is_up);
esp_err_t   innoedge_cluster_init(innoedge_cluster_role_t role);
esp_err_t   innoedge_crypto_sign_tx(uint32_t seq, int kind, int count, int64_t amount_vnd, char *tac_out, size_t out_len);
bool        innoedge_crypto_verify_tx(uint32_t seq, int kind, int count, int64_t amount_vnd, const char *expected_tac);
```

---

## Ecosystem & Tools

* **Zero-Install Web Portal:** Test Web Serial Flasher & Web BLE Provisioning at [https://nguyenduchoai.github.io/innoedge-platform/](https://nguyenduchoai.github.io/innoedge-platform/)
* **Full Protocol Specification:** [PROTOCOL-v1.md](https://github.com/nguyenduchoai/innoedge-platform/blob/main/docs/PROTOCOL-v1.md)
* **10 Hardware Reference Cookbooks:** [COMMUNITY-COOKBOOKS.md](https://github.com/nguyenduchoai/innoedge-platform/blob/main/docs/COMMUNITY-COOKBOOKS.md)
* **GitHub Repository:** [nguyenduchoai/innoedge-platform](https://github.com/nguyenduchoai/innoedge-platform)

---

## License

Apache License 2.0 — Free forever for commercial and open-source devices.
