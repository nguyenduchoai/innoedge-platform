// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

#include "esp_err.h"
#include "ie_config_store.h"

#ifdef __cplusplus
extern "C" {
#endif

// Thông báo UI "đang chờ cài đặt" — board wiring (main) đăng ký; provisioning
// không phụ thuộc ui_app/board nào (hiến pháp I). NULL = bỏ qua.
typedef void (*ie_provisioning_ui_fn)(void);
void ie_provisioning_set_ui_notify(ie_provisioning_ui_fn fn);

esp_err_t ie_provisioning_start(const ie_device_config_t *config);

#ifdef __cplusplus
}
#endif
