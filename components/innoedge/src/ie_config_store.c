// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "ie_config_store.h"

#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ie.config";
// ponytail: tên namespace NVS giữ nguyên từ bản đầu — đổi là mồ côi WiFi/token
// trên mọi bo đã flash. Người dùng không bao giờ thấy chuỗi này.
static const char *NVS_NS = "gtek_cfg";

static void copy_str(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    snprintf(dst, dst_len, "%s", src ? src : "");
}

static esp_err_t read_string(nvs_handle_t nvs, const char *key, char *value, size_t value_len)
{
    size_t required = value_len;
    esp_err_t err = nvs_get_str(nvs, key, value, &required);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

static esp_err_t write_string(const char *key, const char *value)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, key, value ? value : "");
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

static void mac_to_device_id(char out[24])
{
    uint8_t mac[6] = {0};
    if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
        esp_fill_random(mac, sizeof(mac));
    }
    snprintf(out, 24, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void make_uuid(char out[40])
{
    uint8_t b[16] = {0};
    esp_fill_random(b, sizeof(b));
    b[6] = (b[6] & 0x0f) | 0x40;
    b[8] = (b[8] & 0x3f) | 0x80;
    snprintf(out, 40,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
             "%02x%02x%02x%02x%02x%02x",
             b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
             b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
}

static void set_defaults(ie_device_config_t *config)
{
    memset(config, 0, sizeof(*config));
    mac_to_device_id(config->device_id);
    make_uuid(config->client_id);
    copy_str(config->device_name, sizeof(config->device_name), "InnoEdge Device");
    copy_str(config->server_base_url, sizeof(config->server_base_url), CONFIG_INNOEDGE_SERVER_BASE_URL);
    copy_str(config->device_token, sizeof(config->device_token), CONFIG_INNOEDGE_FACTORY_TOKEN);
    copy_str(config->wifi_ssid, sizeof(config->wifi_ssid), CONFIG_INNOEDGE_WIFI_SSID);
    copy_str(config->wifi_password, sizeof(config->wifi_password), CONFIG_INNOEDGE_WIFI_PASSWORD);
    config->provisioned = config->wifi_ssid[0] != '\0';
}

esp_err_t ie_config_store_load(ie_device_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    set_defaults(config);

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }

    bool changed = false;
    char stored_device_id[24] = {0};
    char stored_client_id[40] = {0};
    read_string(nvs, "device_id", stored_device_id, sizeof(stored_device_id));
    read_string(nvs, "client_id", stored_client_id, sizeof(stored_client_id));
    if (stored_device_id[0] != '\0') {
        copy_str(config->device_id, sizeof(config->device_id), stored_device_id);
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, "device_id", config->device_id));
        changed = true;
    }
    if (stored_client_id[0] != '\0') {
        copy_str(config->client_id, sizeof(config->client_id), stored_client_id);
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_str(nvs, "client_id", config->client_id));
        changed = true;
    }

    read_string(nvs, "name", config->device_name, sizeof(config->device_name));
    read_string(nvs, "server_url", config->server_base_url, sizeof(config->server_base_url));
    read_string(nvs, "ws_url", config->websocket_url, sizeof(config->websocket_url));
    read_string(nvs, "dev_token", config->device_token, sizeof(config->device_token));
    read_string(nvs, "wifi_ssid", config->wifi_ssid, sizeof(config->wifi_ssid));
    read_string(nvs, "wifi_pass", config->wifi_password, sizeof(config->wifi_password));
    read_string(nvs, "static_qr", config->static_qr_payload, sizeof(config->static_qr_payload));
    read_string(nvs, "static_ref", config->static_qr_ref, sizeof(config->static_qr_ref));
    nvs_get_u64(nvs, "seq", &config->seq);

    uint8_t assigned = 0;
    nvs_get_u8(nvs, "assigned", &assigned);
    config->assigned = assigned == 1;
    config->provisioned = config->wifi_ssid[0] != '\0';

    if (changed) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);

    ESP_LOGI(TAG, "loaded device=%s client=%s server=%s assigned=%d seq=%llu",
             config->device_id, config->client_id, config->server_base_url,
             config->assigned ? 1 : 0, (unsigned long long)config->seq);
    return err;
}

esp_err_t ie_config_store_save_wifi(const char *ssid, const char *password)
{
    if (!ssid || ssid[0] == '\0' || strlen(ssid) >= 33) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password && strlen(password) >= 65) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, "wifi_ssid", ssid);
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "wifi_pass", password ? password : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t ie_config_store_save_device_name(const char *name)
{
    return write_string("name", name);
}

esp_err_t ie_config_store_save_server_base_url(const char *url)
{
    return write_string("server_url", url);
}

esp_err_t ie_config_store_save_token(const char *token)
{
    return write_string("dev_token", token);
}

esp_err_t ie_config_store_save_websocket_url(const char *url)
{
    return write_string("ws_url", url);
}

esp_err_t ie_config_store_save_static_qr(const char *payload, const char *ref_code)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_str(nvs, "static_qr", payload ? payload : "");
    if (err == ESP_OK) {
        err = nvs_set_str(nvs, "static_ref", ref_code ? ref_code : "");
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t ie_config_store_save_assigned(bool assigned)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_u8(nvs, "assigned", assigned ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

esp_err_t ie_config_store_next_seq(uint64_t *seq)
{
    if (!seq) {
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    uint64_t current = 0;
    nvs_get_u64(nvs, "seq", &current);
    current++;
    err = nvs_set_u64(nvs, "seq", current);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        *seq = current;
    }
    return err;
}

esp_err_t ie_config_store_save_last_command_id(int64_t command_id)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_i64(nvs, "last_cmd", command_id);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

int64_t ie_config_store_last_command_id(void)
{
    nvs_handle_t nvs;
    if (nvs_open(NVS_NS, NVS_READONLY, &nvs) != ESP_OK) {
        return 0;
    }
    int64_t v = 0;
    nvs_get_i64(nvs, "last_cmd", &v);
    nvs_close(nvs);
    return v;
}

esp_err_t ie_config_store_load_blob(const char *key, void *data, size_t len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &nvs);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return ESP_ERR_NOT_FOUND; // namespace chưa từng ghi = máy mới
    }
    if (err != ESP_OK) {
        return err;
    }
    size_t stored_len = len;
    err = nvs_get_blob(nvs, key, data, &stored_len);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        err = ESP_ERR_NOT_FOUND;
    } else if (err == ESP_OK && stored_len != len) {
        err = ESP_ERR_INVALID_SIZE;
    }
    nvs_close(nvs);
    return err;
}

esp_err_t ie_config_store_save_blob(const char *key, const void *data, size_t len)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_set_blob(nvs, key, data, len);
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    return err;
}

// Cấu hình vận hành lưu dạng blob (có thể lớn hơn giới hạn chuỗi NVS).
esp_err_t ie_config_store_set_op_config(const char *json, int version)
{
    if (!json) {
        json = "";
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    // Lưu cả dấu '\0' để đọc ra dùng trực tiếp như chuỗi C.
    err = nvs_set_blob(nvs, "opcfg", json, strlen(json) + 1);
    if (err == ESP_OK) {
        err = nvs_set_i32(nvs, "opcfg_ver", (int32_t)version);
    }
    if (err == ESP_OK) {
        err = nvs_commit(nvs);
    }
    nvs_close(nvs);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "lưu op_config version=%d (%u bytes)", version, (unsigned)strlen(json));
    }
    return err;
}

esp_err_t ie_config_store_get_op_config(char *json, size_t json_len, int *version)
{
    if (version) {
        *version = 0;
    }
    if (json && json_len > 0) {
        json[0] = '\0';
    }
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(NVS_NS, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        return err;
    }
    int32_t ver = 0;
    if (nvs_get_i32(nvs, "opcfg_ver", &ver) == ESP_OK && version) {
        *version = (int)ver;
    }

    size_t stored_len = 0;
    err = nvs_get_blob(nvs, "opcfg", NULL, &stored_len);
    if (err != ESP_OK || stored_len == 0) {
        nvs_close(nvs);
        return err == ESP_OK ? ESP_ERR_NVS_NOT_FOUND : err;
    }
    if (!json || json_len == 0) {
        nvs_close(nvs); // chỉ hỏi version
        return ESP_OK;
    }
    if (stored_len <= json_len) {
        err = nvs_get_blob(nvs, "opcfg", json, &stored_len);
        json[json_len - 1] = '\0';
    } else {
        // Buffer thiếu chỗ: đọc tạm rồi cắt (vẫn cố trả phần đầu hợp lệ nếu được).
        ESP_LOGW(TAG, "op_config (%u bytes) > buffer (%u) — bị cắt",
                 (unsigned)stored_len, (unsigned)json_len);
        char *tmp = malloc(stored_len);
        if (!tmp) {
            nvs_close(nvs);
            return ESP_ERR_NO_MEM;
        }
        err = nvs_get_blob(nvs, "opcfg", tmp, &stored_len);
        if (err == ESP_OK) {
            snprintf(json, json_len, "%s", tmp);
        }
        free(tmp);
    }
    nvs_close(nvs);
    return err;
}

int ie_config_store_op_config_version(void)
{
    int v = 0;
    ie_config_store_get_op_config(NULL, 0, &v);
    return v;
}
