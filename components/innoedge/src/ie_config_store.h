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

typedef struct {
    char device_id[24];
    char client_id[40];
    char device_name[64];
    char server_base_url[160];
    char websocket_url[192];
    char device_token[160];
    char wifi_ssid[33];
    char wifi_password[65];
    char static_qr_payload[512];
    char static_qr_ref[48];
    uint64_t seq;
    bool provisioned;
    bool assigned;
} ie_device_config_t;

esp_err_t ie_config_store_load(ie_device_config_t *config);
esp_err_t ie_config_store_save_wifi(const char *ssid, const char *password);
esp_err_t ie_config_store_save_device_name(const char *name);
esp_err_t ie_config_store_save_server_base_url(const char *url);
esp_err_t ie_config_store_save_token(const char *token);
esp_err_t ie_config_store_save_websocket_url(const char *url);
esp_err_t ie_config_store_save_static_qr(const char *payload, const char *ref_code);
esp_err_t ie_config_store_save_assigned(bool assigned);
esp_err_t ie_config_store_next_seq(uint64_t *seq);
// commandId lệnh động (command bus) cuối đã thực thi — sống qua reboot để dedupe
// lệnh server gửi lại sau reconnect (đặc biệt quan trọng với dispense/nhả tiền).
esp_err_t ie_config_store_save_last_command_id(int64_t command_id);
int64_t ie_config_store_last_command_id(void);
// Blob cố định kích thước (nhật ký lệnh "cmd_journal", nhật ký tiền QR
// "paid_journal"). key <= 15 ký tự. ESP_ERR_NOT_FOUND = chưa có; sai kích thước
// = ESP_ERR_INVALID_SIZE. save có commit.
esp_err_t ie_config_store_load_blob(const char *key, void *data, size_t len);
esp_err_t ie_config_store_save_blob(const char *key, const void *data, size_t len);

// ── Cấu hình vận hành (operational config) ──────────────────────────────────
// Chuỗi JSON thô của object `config` từ server (combos/pricing/dynamic...) +
// version. Lưu vào NVS để firmware chạy OFFLINE (đọc combo từ cache). json là
// object JSON hợp lệ; version là số phiên bản tăng dần do server cấp.
esp_err_t ie_config_store_set_op_config(const char *json, int version);
// Đọc cache: chép JSON vào buffer json (cắt nếu thiếu chỗ, luôn null-terminated)
// và trả version qua *version (0 nếu chưa có). Trả ESP_ERR_NOT_FOUND nếu chưa
// từng lưu. json/version có thể NULL nếu chỉ cần phần còn lại.
esp_err_t ie_config_store_get_op_config(char *json, size_t json_len, int *version);
// Tiện ích: chỉ lấy version cache hiện tại (0 nếu chưa có).
int ie_config_store_op_config_version(void);

#ifdef __cplusplus
}
#endif
