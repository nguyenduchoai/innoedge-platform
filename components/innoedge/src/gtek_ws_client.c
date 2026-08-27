// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "gtek_ws_client.h"

#include "cJSON.h"
#include "gtek_ota_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "gtek.ws";
static esp_websocket_client_handle_t s_client;
static bool s_connected;
static SemaphoreHandle_t s_send_lock;
static gtek_ws_handlers_t s_handlers;
static gtek_device_config_t s_config;
static char s_headers[512];
static char s_ws_url[192];

static void build_ws_url(const gtek_device_config_t *config, char *out, size_t out_len)
{
    if (config->websocket_url[0] != '\0') {
        snprintf(out, out_len, "%s", config->websocket_url);
        return;
    }
    const char *base = config->server_base_url[0] ? config->server_base_url : CONFIG_GTEK_SERVER_BASE_URL;
    const char *scheme = "wss://";
    const char *host = base;
    if (strncmp(base, "https://", 8) == 0) {
        scheme = "wss://";
        host = base + 8;
    } else if (strncmp(base, "http://", 7) == 0) {
        scheme = "ws://";
        host = base + 7;
    }
    size_t n = strlen(host);
    const char *slash = (n > 0 && host[n - 1] == '/') ? "" : "/";
    snprintf(out, out_len, "%s%s%sws/", scheme, host, slash);
}

static esp_err_t send_locked(const char *text)
{
    if (!s_client || !s_connected || !esp_websocket_client_is_connected(s_client)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_send_lock && xSemaphoreTake(s_send_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    int sent = esp_websocket_client_send_text(s_client, text, strlen(text), pdMS_TO_TICKS(1000));
    if (s_send_lock) {
        xSemaphoreGive(s_send_lock);
    }
    return sent > 0 ? ESP_OK : ESP_FAIL;
}

static void send_hello(void)
{
    char msg[256];
    snprintf(msg, sizeof(msg),
             "{\"type\":\"hello\",\"transport\":\"websocket\","
             "\"firmware\":\"%s\",\"local_ip\":\"0.0.0.0\","
             "\"features\":{\"payments\":true,\"qr\":true,\"heartbeat\":true}}",
             CONFIG_GTEK_FW_VERSION);
    (void)send_locked(msg);
}

static const char *json_string(cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

static int64_t json_i64(cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(v) ? (int64_t)v->valuedouble : 0;
}

static uint64_t json_u64(cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(v) ? (uint64_t)v->valuedouble : 0;
}

static bool json_bool(cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsBool(v) && cJSON_IsTrue(v);
}

static bool is_direct_command(const char *type)
{
    return strcmp(type, "ble_provision") == 0 ||
           strcmp(type, "set_volume") == 0 ||
           strcmp(type, "set_display") == 0 ||
           strcmp(type, "activation_complete") == 0 ||
           strcmp(type, "ota_check") == 0 ||
           strcmp(type, "reboot") == 0;
}

static void handle_text(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "invalid JSON frame");
        return;
    }
    const char *type = json_string(root, "type");
    char raw[768];
    int raw_len = len < (int)sizeof(raw) - 1 ? len : (int)sizeof(raw) - 1;
    memcpy(raw, data, raw_len);
    raw[raw_len] = '\0';
    if (strcmp(type, "coin_ack") == 0) {
        uint64_t seq = json_u64(root, "seq");
        if (seq > 0 && s_handlers.on_ack) {
            s_handlers.on_ack(seq, s_handlers.ctx);
        }
        if (seq > 0 && s_handlers.on_payment_ack) {
            gtek_payment_ack_message_t ack = {
                .seq = seq,
                .coins = (int)json_i64(root, "coins"),
                .amount_vnd = json_i64(root, "amountVND"),
                .rate_vnd = json_i64(root, "rateVND"),
                .duplicate = json_bool(root, "duplicate"),
            };
            snprintf(ack.method, sizeof(ack.method), "%s", json_string(root, "method"));
            s_handlers.on_payment_ack(&ack, s_handlers.ctx);
        }
    } else if (strcmp(type, "command") == 0) {
        cJSON *action = cJSON_GetObjectItem(root, "action");
        if (cJSON_IsString(action) && action->valuestring[0] != '\0') {
            // Lệnh động (command bus). Tách params thành chuỗi JSON cho handler.
            int64_t command_id = json_i64(root, "commandId");
            cJSON *params = cJSON_GetObjectItem(root, "params");
            char *params_str = NULL;
            if (params) {
                params_str = cJSON_PrintUnformatted(params);
            }
            if (s_handlers.on_dynamic_command) {
                s_handlers.on_dynamic_command(command_id, action->valuestring,
                                              params_str ? params_str : "{}",
                                              s_handlers.ctx);
            }
            if (params_str) {
                cJSON_free(params_str);
            }
        } else {
            // Lệnh hệ thống cũ: {"type":"command","command":"..."}.
            const char *command = json_string(root, "command");
            const char *auth_token = json_string(root, "authToken");
            if (s_handlers.on_command) {
                s_handlers.on_command(command, auth_token, raw, s_handlers.ctx);
            }
        }
    } else if (is_direct_command(type)) {
        const char *auth_token = json_string(root, "authToken");
        if (s_handlers.on_command) {
            s_handlers.on_command(type, auth_token, raw, s_handlers.ctx);
        }
    } else if (strcmp(type, "status") == 0) {
        const char *state = json_string(root, "state");
        if (s_handlers.on_status) {
            s_handlers.on_status(state, s_handlers.ctx);
        }
    } else if (strcmp(type, "qr") == 0) {
        gtek_qr_message_t qr = {0};
        qr.seq = json_u64(root, "seq");
        qr.intent_id = json_i64(root, "intentId");
        qr.amount = json_i64(root, "amount");
        qr.expires_sec = (int)json_i64(root, "expiresSec");
        snprintf(qr.ref_code, sizeof(qr.ref_code), "%s", json_string(root, "refCode"));
        snprintf(qr.qr_payload, sizeof(qr.qr_payload), "%s", json_string(root, "qrPayload"));
        if (s_handlers.on_qr) {
            s_handlers.on_qr(&qr, s_handlers.ctx);
        }
    } else if (strcmp(type, "qr_error") == 0) {
        if (s_handlers.on_qr_error) {
            s_handlers.on_qr_error(json_u64(root, "seq"), json_string(root, "message"),
                                   s_handlers.ctx);
        }
    } else if (strcmp(type, "payment_paid") == 0) {
        if (s_handlers.on_payment_paid) {
            s_handlers.on_payment_paid(json_i64(root, "intentId"), json_i64(root, "amount"),
                                       s_handlers.ctx);
        }
    } else if (strcmp(type, "set_static_qr") == 0) {
        if (s_handlers.on_static_qr) {
            s_handlers.on_static_qr(json_string(root, "payload"), json_string(root, "refCode"),
                                    s_handlers.ctx);
        }
    } else if (strcmp(type, "hello") != 0) {
        ESP_LOGD(TAG, "ignored frame type=%s", type);
    }
    cJSON_Delete(root);
}

static void on_ws_event(void *handler_args, esp_event_base_t base, int32_t id, void *event_data)
{
    esp_websocket_event_data_t *e = (esp_websocket_event_data_t *)event_data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "connected");
        // Kết nối WS thành công = firmware vừa OTA chạy KHỎE → xác nhận hợp lệ +
        // huỷ rollback. Bản OTA lỗi (không bao giờ tới đây) sẽ bị bootloader tự
        // rollback về slot cũ ở lần reboot — chống brick máy ngoài hiện trường.
        gtek_ota_mark_valid();
        vTaskDelay(pdMS_TO_TICKS(200));
        send_hello();
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "disconnected");
        break;
    case WEBSOCKET_EVENT_DATA:
        if (e->op_code == 0x01) {
            handle_text((const char *)e->data_ptr, e->data_len);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "error");
        break;
    default:
        break;
    }
}

esp_err_t gtek_ws_client_start(const gtek_device_config_t *config,
                               const gtek_ws_handlers_t *handlers)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_send_lock) {
        s_send_lock = xSemaphoreCreateMutex();
        if (!s_send_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    if (s_client) {
        if (s_connected) {
            return ESP_OK;
        }
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }

    s_config = *config;
    if (handlers) {
        s_handlers = *handlers;
    } else {
        memset(&s_handlers, 0, sizeof(s_handlers));
    }
    build_ws_url(config, s_ws_url, sizeof(s_ws_url));
    if (config->device_token[0] != '\0') {
        snprintf(s_headers, sizeof(s_headers),
                 "Authorization: Bearer %s\r\nDevice-Id: %s\r\nClient-Id: %s\r\n"
                 "User-Agent: gtek-fw/%s\r\n",
                 config->device_token, config->device_id, config->client_id, CONFIG_GTEK_FW_VERSION);
    } else {
        snprintf(s_headers, sizeof(s_headers),
                 "Device-Id: %s\r\nClient-Id: %s\r\nUser-Agent: gtek-fw/%s\r\n",
                 config->device_id, config->client_id, CONFIG_GTEK_FW_VERSION);
    }

    esp_websocket_client_config_t ws_cfg = {
        .uri = s_ws_url,
        .headers = s_headers,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .reconnect_timeout_ms = 3000,
        .network_timeout_ms = 15000,
        .ping_interval_sec = 30,
        .pingpong_timeout_sec = 10,
        .buffer_size = 4096,
        .task_stack = 8192,
        .task_prio = 5,
    };
    s_client = esp_websocket_client_init(&ws_cfg);
    if (!s_client) {
        return ESP_ERR_NO_MEM;
    }
    esp_websocket_register_events(s_client, WEBSOCKET_EVENT_ANY, on_ws_event, NULL);
    esp_err_t err = esp_websocket_client_start(s_client);
    if (err != ESP_OK) {
        esp_websocket_client_destroy(s_client);
        s_client = NULL;
    }
    ESP_LOGI(TAG, "start %s -> %s", s_ws_url, esp_err_to_name(err));
    return err;
}

esp_err_t gtek_ws_client_stop(void)
{
    if (!s_client) {
        return ESP_OK;
    }
    esp_err_t err = esp_websocket_client_stop(s_client);
    esp_websocket_client_destroy(s_client);
    s_client = NULL;
    s_connected = false;
    return err;
}

bool gtek_ws_client_is_connected(void)
{
    return s_connected && s_client && esp_websocket_client_is_connected(s_client);
}

esp_err_t gtek_ws_client_send_text(const char *text)
{
    if (!text) {
        return ESP_ERR_INVALID_ARG;
    }
    return send_locked(text);
}

esp_err_t gtek_ws_client_send_heartbeat(const char *fw_version, int rssi,
                                        unsigned queue_depth, int reset_reason)
{
    char msg[320];
    snprintf(msg, sizeof(msg),
             "{\"type\":\"heartbeat\",\"fw_version\":\"%s\","
             "\"uptime_sec\":%lld,\"rssi\":%d,\"heap_internal_free\":%u,"
             "\"heap_internal_min\":%u,\"queue_depth\":%u,\"reset_reason\":%d}",
             fw_version ? fw_version : CONFIG_GTEK_FW_VERSION,
             (long long)(esp_timer_get_time() / 1000000LL), rssi,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
             queue_depth, reset_reason);
    return send_locked(msg);
}

esp_err_t gtek_ws_client_request_qr(uint64_t seq, int64_t amount)
{
    char msg[128];
    snprintf(msg, sizeof(msg), "{\"type\":\"qr_request\",\"amount\":%lld,\"seq\":%llu}",
             (long long)amount, (unsigned long long)seq);
    return send_locked(msg);
}

esp_err_t gtek_ws_client_send_command_ack(int64_t command_id, const char *status,
                                          const char *message, const char *result_json)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "command_ack");
    cJSON_AddNumberToObject(root, "commandId", (double)command_id);
    cJSON_AddStringToObject(root, "status", status ? status : "ok");
    if (message && message[0] != '\0') {
        cJSON_AddStringToObject(root, "message", message);
    }
    if (result_json && result_json[0] != '\0') {
        cJSON *result = cJSON_Parse(result_json);
        // result_json đã là object JSON hợp lệ → gắn thẳng; nếu parse fail thì bỏ field.
        if (result) {
            cJSON_AddItemToObject(root, "result", result);
        }
    }
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) {
        return ESP_ERR_NO_MEM;
    }
    esp_err_t err = send_locked(text);
    cJSON_free(text);
    return err;
}

esp_err_t gtek_ws_send_alert(const char *code, const char *severity,
                             const char *message, bool active)
{
    if (!code || code[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return ESP_ERR_NO_MEM;
    }
    cJSON_AddStringToObject(root, "type", "alert");
    cJSON_AddStringToObject(root, "code", code);
    cJSON_AddStringToObject(root, "severity", severity && severity[0] ? severity : "warning");
    if (message && message[0] != '\0') {
        cJSON_AddStringToObject(root, "message", message);
    }
    cJSON_AddBoolToObject(root, "active", active);
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "alert code=%s severity=%s active=%d", code,
             severity && severity[0] ? severity : "warning", active ? 1 : 0);
    esp_err_t err = send_locked(text);
    cJSON_free(text);
    return err;
}
