// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "gtek_voucher_client.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "gtek.voucher";

#define VOUCHER_RX_CAP 2048 // intent response ~1KB (qrPayload 512 + imageUrl/payUrl)

typedef struct {
    char *buf;
    int len;
    int cap;
} rx_ctx_t;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    rx_ctx_t *rx = (rx_ctx_t *)evt->user_data;
    if (!rx) {
        return ESP_OK;
    }
    if (evt->event_id == HTTP_EVENT_ON_CONNECTED) {
        rx->len = 0;
        if (rx->buf) {
            rx->buf[0] = '\0';
        }
    } else if (evt->event_id == HTTP_EVENT_ON_DATA && rx->buf) {
        int copy = evt->data_len;
        if (rx->len + copy > rx->cap - 1) {
            copy = rx->cap - 1 - rx->len;
        }
        if (copy > 0) {
            memcpy(rx->buf + rx->len, evt->data, copy);
            rx->len += copy;
            rx->buf[rx->len] = '\0';
        }
    }
    return ESP_OK;
}

// POST JSON device-auth. rx nhận body (kể cả body lỗi 4xx để lấy message).
// Trả ESP_OK chỉ khi transport OK và status 2xx; *out_status luôn được điền.
static esp_err_t call_json(const gtek_device_config_t *config,
                           esp_http_client_method_t method, const char *path,
                           const char *body, char *rx_buf, int rx_cap, int *out_status)
{
    const char *base = (config->server_base_url[0]) ? config->server_base_url
                                                    : CONFIG_GTEK_SERVER_BASE_URL;
    size_t n = strlen(base);
    char url[288];
    snprintf(url, sizeof(url), "%s%s%s", base, (n > 0 && base[n - 1] == '/') ? "" : "/",
             path);

    rx_ctx_t rx = {.buf = rx_buf, .len = 0, .cap = rx_cap};
    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = method,
        .event_handler = on_http_event,
        .user_data = &rx,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&http_cfg);
    if (!cli) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(cli, "Device-Id", config->device_id);
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    if (config->device_token[0] != '\0') {
        char auth[192];
        snprintf(auth, sizeof(auth), "Bearer %s", config->device_token);
        esp_http_client_set_header(cli, "Authorization", auth);
    }
    if (body) {
        esp_http_client_set_post_field(cli, body, (int)strlen(body));
    }

    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    if (out_status) {
        *out_status = status;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "HTTP %s lỗi mạng: %s", path, esp_err_to_name(err));
        return err;
    }
    return (status >= 200 && status < 300) ? ESP_OK : ESP_FAIL;
}

static esp_err_t post_json(const gtek_device_config_t *config, const char *path,
                           const char *body, char *rx_buf, int rx_cap, int *out_status)
{
    return call_json(config, HTTP_METHOD_POST, path, body, rx_buf, rx_cap, out_status);
}

// Chép root["error"] (message tiếng Việt từ server) vào out nếu có.
static void copy_error(const char *rx, char *out, size_t out_cap)
{
    if (!out || out_cap == 0) {
        return;
    }
    out[0] = '\0';
    cJSON *root = cJSON_Parse(rx);
    if (!root) {
        return;
    }
    cJSON *e = cJSON_GetObjectItem(root, "error");
    if (cJSON_IsString(e) && e->valuestring[0] != '\0') {
        snprintf(out, out_cap, "%s", e->valuestring);
    }
    cJSON_Delete(root);
}

// Server bọc {"data":{...}}; phòng hờ trả thẳng object.
static cJSON *unwrap_data(cJSON *root)
{
    cJSON *data = cJSON_GetObjectItem(root, "data");
    return cJSON_IsObject(data) ? data : root;
}

static int64_t json_i64(cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(v) ? (int64_t)v->valuedouble : 0;
}

static const char *json_str(cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

esp_err_t gtek_voucher_verify(const gtek_device_config_t *config, const char *code,
                              int64_t amount_vnd, gtek_voucher_verify_result_t *out)
{
    if (!config || !code || code[0] == '\0' || amount_vnd <= 0 || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    char *rx = malloc(VOUCHER_RX_CAP);
    if (!rx) {
        return ESP_ERR_NO_MEM;
    }
    rx[0] = '\0';
    char body[96];
    snprintf(body, sizeof(body),
             "{\"code\":\"%s\",\"method\":\"qr\",\"amountVnd\":%lld}", code,
             (long long)amount_vnd);
    int status = 0;
    esp_err_t err = post_json(config, "api/device/voucher/verify", body, rx,
                              VOUCHER_RX_CAP, &status);
    if (err != ESP_OK) {
        copy_error(rx, out->error, sizeof(out->error));
        free(rx);
        return err;
    }
    cJSON *root = cJSON_Parse(rx);
    free(rx);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *data = unwrap_data(root);
    out->discount_vnd = json_i64(data, "discountVnd");
    out->final_vnd = json_i64(data, "finalVnd");
    cJSON_Delete(root);
    ESP_LOGI(TAG, "verify %s: giảm %lld còn %lld", code,
             (long long)out->discount_vnd, (long long)out->final_vnd);
    return ESP_OK;
}

esp_err_t gtek_voucher_create_intent(const gtek_device_config_t *config,
                                     int64_t amount_vnd, const char *code,
                                     gtek_voucher_intent_result_t *out)
{
    if (!config || !code || code[0] == '\0' || amount_vnd <= 0 || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    char *rx = malloc(VOUCHER_RX_CAP);
    if (!rx) {
        return ESP_ERR_NO_MEM;
    }
    rx[0] = '\0';
    char body[96];
    snprintf(body, sizeof(body), "{\"amount\":%lld,\"voucher\":\"%s\"}",
             (long long)amount_vnd, code);
    int status = 0;
    esp_err_t err = post_json(config, "api/device/payment/intents", body, rx,
                              VOUCHER_RX_CAP, &status);
    if (err != ESP_OK) {
        copy_error(rx, out->error, sizeof(out->error));
        free(rx);
        return err;
    }
    cJSON *root = cJSON_Parse(rx);
    free(rx);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *data = unwrap_data(root);
    out->intent_id = json_i64(data, "intentId");
    out->amount = json_i64(data, "amount");
    out->discount_vnd = json_i64(data, "voucherDiscountVnd");
    out->expires_sec = (int)json_i64(data, "expiresSec");
    out->paid = strcmp(json_str(data, "status"), "paid") == 0;
    snprintf(out->ref_code, sizeof(out->ref_code), "%s", json_str(data, "refCode"));
    snprintf(out->qr_payload, sizeof(out->qr_payload), "%s", json_str(data, "qrPayload"));
    cJSON_Delete(root);
    if (!out->paid && out->qr_payload[0] == '\0') {
        return ESP_ERR_INVALID_RESPONSE; // pending mà không có QR thì vô dụng
    }
    ESP_LOGI(TAG, "intent voucher id=%lld amount=%lld giảm=%lld paid=%d",
             (long long)out->intent_id, (long long)out->amount,
             (long long)out->discount_vnd, out->paid ? 1 : 0);
    return ESP_OK;
}

esp_err_t gtek_voucher_cancel_intent(const gtek_device_config_t *config,
                                     int64_t intent_id, bool *out_paid)
{
    if (!config || intent_id <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (out_paid) {
        *out_paid = false;
    }
    char *rx = malloc(VOUCHER_RX_CAP);
    if (!rx) {
        return ESP_ERR_NO_MEM;
    }
    rx[0] = '\0';
    char path[64];
    snprintf(path, sizeof(path), "api/device/payment/intents/%lld/cancel",
             (long long)intent_id);
    int status = 0;
    esp_err_t err = post_json(config, path, "{}", rx, VOUCHER_RX_CAP, &status);
    if (err == ESP_OK && out_paid) {
        cJSON *root = cJSON_Parse(rx);
        if (root) {
            *out_paid = strcmp(json_str(unwrap_data(root), "status"), "paid") == 0;
            cJSON_Delete(root);
        }
    }
    free(rx);
    ESP_LOGI(TAG, "cancel intent %lld: %s status=%d paid=%d", (long long)intent_id,
             esp_err_to_name(err), status, (out_paid && *out_paid) ? 1 : 0);
    return err;
}

// Poll trạng thái intent — LƯỚI ĐỠ khi WS đứt đúng lúc khách quét trả: server đã
// mark paid nhưng lệnh payment_paid không tới máy. Màn QR gọi định kỳ (ui_app).
esp_err_t gtek_voucher_intent_status(const gtek_device_config_t *config,
                                     int64_t intent_id, bool *out_paid,
                                     int64_t *out_amount)
{
    if (!config || intent_id <= 0 || !out_paid) {
        return ESP_ERR_INVALID_ARG;
    }
    *out_paid = false;
    if (out_amount) {
        *out_amount = 0;
    }
    char *rx = malloc(VOUCHER_RX_CAP);
    if (!rx) {
        return ESP_ERR_NO_MEM;
    }
    rx[0] = '\0';
    char path[64];
    snprintf(path, sizeof(path), "api/device/payment/intents/%lld",
             (long long)intent_id);
    esp_err_t err = call_json(config, HTTP_METHOD_GET, path, NULL, rx,
                              VOUCHER_RX_CAP, NULL);
    if (err == ESP_OK) {
        cJSON *root = cJSON_Parse(rx);
        if (root) {
            cJSON *data = unwrap_data(root);
            *out_paid = strcmp(json_str(data, "status"), "paid") == 0;
            if (out_amount) {
                *out_amount = json_i64(data, "amount");
            }
            cJSON_Delete(root);
        }
    }
    free(rx);
    return err;
}
