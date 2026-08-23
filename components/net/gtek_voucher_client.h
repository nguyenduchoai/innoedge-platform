// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// Client HTTP mã giảm giá (voucher) — device-auth như config_client.
// 3 lệnh: verify (xem trước, KHÔNG đốt lượt) · tạo intent kèm mã · hủy intent cũ.
#pragma once

#include "esp_err.h"
#include "gtek_config_store.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int64_t discount_vnd; // số tiền được giảm
    int64_t final_vnd;    // số tiền CUỐI khách phải trả
    char error[96];       // thông báo lỗi tiếng Việt từ server (HTTP 400)
} gtek_voucher_verify_result_t;

typedef struct {
    int64_t intent_id;
    int64_t amount;       // số tiền CUỐI (sau giảm)
    int64_t discount_vnd; // voucherDiscountVnd
    int expires_sec;
    bool paid;            // voucher 100% → intent paid ngay, KHÔNG có qrPayload
    char ref_code[64];
    char qr_payload[512];
    char error[96];
} gtek_voucher_intent_result_t;

// POST /api/device/voucher/verify {"code","method":"qr","amountVnd"}.
// ESP_OK → out->discount/final; lỗi → out->error (nếu server có message).
esp_err_t gtek_voucher_verify(const gtek_device_config_t *config, const char *code,
                              int64_t amount_vnd, gtek_voucher_verify_result_t *out);

// POST /api/device/payment/intents {"amount","voucher"} — amount là số GỐC,
// server tự trừ. out->paid=true (voucher 100%) → coi như đã thanh toán.
esp_err_t gtek_voucher_create_intent(const gtek_device_config_t *config,
                                     int64_t amount_vnd, const char *code,
                                     gtek_voucher_intent_result_t *out);

// POST /api/device/payment/intents/:id/cancel. *out_paid=true khi server báo
// intent ĐÃ paid trước khi kịp hủy (máy phải hiện màn thành công, không tạo mới).
esp_err_t gtek_voucher_cancel_intent(const gtek_device_config_t *config,
                                     int64_t intent_id, bool *out_paid);

// GET /api/device/payment/intents/:id — poll trạng thái (lưới đỡ khi WS đứt
// đúng lúc khách trả; ui_app gọi định kỳ trên màn QR).
esp_err_t gtek_voucher_intent_status(const gtek_device_config_t *config,
                                     int64_t intent_id, bool *out_paid,
                                     int64_t *out_amount);

#ifdef __cplusplus
}
#endif
