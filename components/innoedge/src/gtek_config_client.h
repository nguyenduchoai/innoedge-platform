// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#pragma once

#include "esp_err.h"
#include "gtek_config_store.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Tải cấu hình vận hành động từ server ────────────────────────────────────
//
// HTTP GET <server_base_url>/api/device/config
//   headers: Device-Id: <mac>, Authorization: Bearer <device_token>
//   trả về: {status:"ok", data:{version:int, config:{combos:[...],pricing,dynamic}}}
//
// Lưu chuỗi JSON thô của `config` + version vào NVS (config_store) để firmware
// chạy OFFLINE. Gọi khi boot (sau khi WS auth) VÀ khi nhận lệnh config_updated.
// Tha thứ lỗi mạng: trả lỗi nhưng KHÔNG xoá cache cũ (vẫn dùng được offline).

// Tải + lưu cache. Trả ESP_OK nếu lấy & lưu thành công; lỗi mạng/parse → mã lỗi
// (cache cũ giữ nguyên). out_version (nếu != NULL) nhận version mới khi thành công.
esp_err_t gtek_config_client_fetch(gtek_device_config_t *config, int *out_version);

// Tra combo theo id trong cache → điền budgets_sec[4] (water,foam,air,vacuum, giây).
// Đọc combos[].id (so khớp dạng số HOẶC chuỗi) và combos[].payload.steps[] =
// {device:"water|foam|air|vacuum", seconds:int}. Trả ESP_OK nếu tìm thấy combo;
// ESP_ERR_NOT_FOUND nếu không có combo khớp / chưa có cache. budgets_sec dài 4.
esp_err_t gtek_config_lookup_combo(const char *combo_id, int budgets_sec[4]);

// Biến tên thiết bị ("water"/"foam"/"air"/"vacuum") → index 0..3; -1 nếu lạ.
int gtek_config_device_index(const char *name);

#ifdef __cplusplus
}
#endif
