// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GTEK_TAC_HEX_LEN 64

// Khởi tạo hệ thống mật mã với khoá bảo mật thiết bị (Hardware key hoặc bootstrap key)
esp_err_t gtek_crypto_init(const uint8_t *key, size_t key_len);

// Sinh mã xác thực giao dịch TAC (Transaction Authentication Code) bằng HMAC-SHA256
esp_err_t gtek_crypto_sign_transaction(const char *device_id, uint32_t seq,
                                       int kind, int count, int64_t amount_vnd,
                                       char *tac_out, size_t tac_len);

// Xác thực tính toàn vẹn của mã TAC (chống sửa đổi sổ cái flash NVS)
bool gtek_crypto_verify_transaction(const char *device_id, uint32_t seq,
                                    int kind, int count, int64_t amount_vnd,
                                    const char *expected_tac);

// Hàm băm SHA-256 tiêu chuẩn (dùng cho OTA và viễn trắc an toàn)
void gtek_sha256(const uint8_t *data, size_t len, uint8_t hash_out[32]);

#ifdef __cplusplus
}
#endif
