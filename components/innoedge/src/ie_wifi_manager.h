// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

#include "esp_err.h"
#include "ie_config_store.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ie_wifi_manager_start(const ie_device_config_t *config);
esp_err_t ie_wifi_manager_start_provisioning(void);
// Chặn tới khi STA kết nối (driver tự retry nền). timeout_ms=0 = chờ vô hạn.
// Dùng cho nhánh boot-timeout: WiFi đã lưu nhưng lên chậm → resume dịch vụ khi có mạng.
esp_err_t ie_wifi_manager_wait_connected(uint32_t timeout_ms);
int ie_wifi_manager_rssi(void);

#ifdef __cplusplus
}
#endif
