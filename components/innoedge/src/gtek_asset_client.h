// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once
// Tải asset thương hiệu (logo G-TEK + logo đối tác) từ server dạng bitmap
// RGB565+alpha đã scale sẵn — endpoint GET /device-assets/header, format GLG1.
#include "esp_err.h"
#include "gtek_config_store.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t width;
    uint16_t height;
    const uint16_t *rgb565; // w*h pixel
    const uint8_t *alpha;   // w*h alpha 8-bit
} gtek_logo_asset_t;

// Tải + parse blob; logo lưu PSRAM, thay thế bản cũ. Lỗi mạng = giữ bản cũ.
esp_err_t gtek_asset_client_fetch(const gtek_device_config_t *config);

// slot 0 = trái (G-TEK), 1 = phải (đối tác). NULL nếu server chưa có logo đó.
const gtek_logo_asset_t *gtek_asset_logo(int slot);

#ifdef __cplusplus
}
#endif
