// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "gtek_config_client.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "gtek_fault.h"
#include "sdkconfig.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "gtek.cfg";

// Kích thước tối đa cache op_config (combos + pricing + dynamic). Đủ rộng cho
// vài combo nhiều bước nhưng vẫn khiêm tốn với ESP32.
#define GTEK_OP_CONFIG_MAX 4096

// Buffer nhận HTTP cấp động trong event handler (tránh tốn RAM tĩnh khi không dùng).
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

static void build_url(const char *base, char *out, size_t out_len)
{
    const char *b = (base && base[0]) ? base : CONFIG_GTEK_SERVER_BASE_URL;
    size_t n = strlen(b);
    const char *slash = (n > 0 && b[n - 1] == '/') ? "" : "/";
    snprintf(out, out_len, "%s%sapi/device/config", b, slash);
}

esp_err_t gtek_config_client_fetch(gtek_device_config_t *config, int *out_version)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    rx_ctx_t rx = {.buf = malloc(GTEK_OP_CONFIG_MAX + 512), .len = 0,
                   .cap = GTEK_OP_CONFIG_MAX + 512};
    if (!rx.buf) {
        return ESP_ERR_NO_MEM;
    }
    rx.buf[0] = '\0';

    char url[224];
    build_url(config->server_base_url, url, sizeof(url));

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .event_handler = on_http_event,
        .user_data = &rx,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t cli = esp_http_client_init(&http_cfg);
    if (!cli) {
        free(rx.buf);
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_set_header(cli, "Device-Id", config->device_id);
    if (config->device_token[0] != '\0') {
        char auth[192];
        snprintf(auth, sizeof(auth), "Bearer %s", config->device_token);
        esp_http_client_set_header(cli, "Authorization", auth);
    }

    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "fetch config thất bại err=%s status=%d (giữ cache cũ)",
                 esp_err_to_name(err), status);
        // Máy đang chạy theo cấu hình CŨ (giá/combo có thể đã đổi). Latch → tự
        // hết khi lấy được config mới; không spam khi server chỉ trục trặc thoáng.
        gtek_fault_set("config_fetch_failed", "warning",
                       "Khong tai duoc cau hinh moi - dang chay cau hinh cu");
        free(rx.buf);
        return err == ESP_OK ? ESP_FAIL : err;
    }
    gtek_fault_clear("config_fetch_failed");

    cJSON *root = cJSON_Parse(rx.buf);
    free(rx.buf);
    if (!root) {
        ESP_LOGW(TAG, "config JSON không hợp lệ (giữ cache cũ)");
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) {
        data = root; // server có thể trả thẳng {version,config}
    }
    cJSON *ver = cJSON_GetObjectItem(data, "version");
    cJSON *cfg = cJSON_GetObjectItem(data, "config");
    if (!cJSON_IsObject(cfg)) {
        cJSON_Delete(root);
        ESP_LOGW(TAG, "thiếu data.config (giữ cache cũ)");
        return ESP_ERR_INVALID_RESPONSE;
    }
    int version = cJSON_IsNumber(ver) ? (int)ver->valuedouble : 0;

    // Tên thiết bị admin đặt (data.name) — hiện trên màn thay MAC. Chỉ ghi NVS
    // khi đổi (đỡ mòn flash); đổi tên trong CMS → config_updated → fetch lại → áp.
    cJSON *name = cJSON_GetObjectItem(data, "name");
    if (cJSON_IsString(name) && name->valuestring[0] != '\0' &&
        strcmp(name->valuestring, config->device_name) != 0) {
        snprintf(config->device_name, sizeof(config->device_name), "%s",
                 name->valuestring);
        gtek_config_store_save_device_name(config->device_name);
        ESP_LOGI(TAG, "tên thiết bị: %s", config->device_name);
    }

    char *cfg_str = cJSON_PrintUnformatted(cfg);
    cJSON_Delete(root);
    if (!cfg_str) {
        return ESP_ERR_NO_MEM;
    }
    if (strlen(cfg_str) > GTEK_OP_CONFIG_MAX) {
        ESP_LOGW(TAG, "config (%u bytes) > %d — không lưu (giữ cache cũ)",
                 (unsigned)strlen(cfg_str), GTEK_OP_CONFIG_MAX);
        cJSON_free(cfg_str);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t serr = gtek_config_store_set_op_config(cfg_str, version);
    cJSON_free(cfg_str);
    if (serr == ESP_OK) {
        ESP_LOGI(TAG, "cập nhật op_config version=%d", version);
        if (out_version) {
            *out_version = version;
        }
    }
    return serr;
}

int gtek_config_device_index(const char *name)
{
    if (!name) {
        return -1;
    }
    if (strcmp(name, "water") == 0) return 0;
    if (strcmp(name, "foam") == 0) return 1;
    if (strcmp(name, "air") == 0) return 2;
    if (strcmp(name, "vacuum") == 0) return 3;
    return -1;
}

// So khớp id combo: hỗ trợ id dạng số (combos[].id = 7) hoặc chuỗi ("combo7").
static bool combo_id_match(cJSON *combo, const char *want)
{
    cJSON *id = cJSON_GetObjectItem(combo, "id");
    if (!id || !want) {
        return false;
    }
    if (cJSON_IsString(id)) {
        return strcmp(id->valuestring, want) == 0;
    }
    if (cJSON_IsNumber(id)) {
        char buf[24];
        snprintf(buf, sizeof(buf), "%lld", (long long)id->valuedouble);
        return strcmp(buf, want) == 0;
    }
    return false;
}

// Điền budgets từ mảng payload.steps[] = {device, seconds}.
static void fill_from_steps(cJSON *steps, int budgets_sec[4])
{
    cJSON *step = NULL;
    cJSON_ArrayForEach(step, steps) {
        cJSON *dev = cJSON_GetObjectItem(step, "device");
        cJSON *sec = cJSON_GetObjectItem(step, "seconds");
        if (!cJSON_IsString(dev) || !cJSON_IsNumber(sec)) {
            continue;
        }
        int idx = gtek_config_device_index(dev->valuestring);
        if (idx >= 0) {
            int s = (int)sec->valuedouble;
            if (s > 0) {
                budgets_sec[idx] += s; // cộng dồn nếu combo có nhiều bước cùng thiết bị
            }
        }
    }
}

esp_err_t gtek_config_lookup_combo(const char *combo_id, int budgets_sec[4])
{
    if (!combo_id || !budgets_sec) {
        return ESP_ERR_INVALID_ARG;
    }
    budgets_sec[0] = budgets_sec[1] = budgets_sec[2] = budgets_sec[3] = 0;

    char *json = malloc(GTEK_OP_CONFIG_MAX + 1);
    if (!json) {
        return ESP_ERR_NO_MEM;
    }
    int ver = 0;
    esp_err_t err = gtek_config_store_get_op_config(json, GTEK_OP_CONFIG_MAX + 1, &ver);
    if (err != ESP_OK || json[0] == '\0') {
        free(json);
        ESP_LOGW(TAG, "chưa có op_config cache để tra combo");
        return ESP_ERR_NOT_FOUND;
    }

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *combos = cJSON_GetObjectItem(root, "combos");
    esp_err_t ret = ESP_ERR_NOT_FOUND;
    if (cJSON_IsArray(combos)) {
        cJSON *combo = NULL;
        cJSON_ArrayForEach(combo, combos) {
            if (!combo_id_match(combo, combo_id)) {
                continue;
            }
            cJSON *payload = cJSON_GetObjectItem(combo, "payload");
            cJSON *steps = payload ? cJSON_GetObjectItem(payload, "steps") : NULL;
            if (cJSON_IsArray(steps)) {
                fill_from_steps(steps, budgets_sec);
                ret = ESP_OK;
            }
            break;
        }
    }
    cJSON_Delete(root);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "combo %s -> water=%d foam=%d air=%d vacuum=%d", combo_id,
                 budgets_sec[0], budgets_sec[1], budgets_sec[2], budgets_sec[3]);
    }
    return ret;
}
