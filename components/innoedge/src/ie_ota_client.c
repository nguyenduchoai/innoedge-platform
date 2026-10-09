// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "ie_ota_client.h"
#include "ie_command_bus.h"
#include "ie_fault.h"
#include "ie_ota_policy.h"
#include "ie_ws_client.h"

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "ie.ota";

#define IE_OTA_MANIFEST_MAX 4096
#define IE_OTA_CHUNK 4096
#define IE_OTA_MAX_REDIRECTS 5

// Lần OTA gần nhất: tải bản `target` khi đang chạy `from`. Gặp lại đúng cặp đó =
// bản vừa cài vẫn báo version cũ → tải lại mãi (hay gặp khi đặt cfg.fw_version
// bằng tay mà quên tăng). Chặn lại và báo thay vì đốt flash + băng thông.
typedef struct {
    char target[32];
    char from[32];
} ota_last_t;
#define OTA_LAST_KEY "ota_last"

// Busy-check (application đăng ký): true = khách đang giữa giao dịch → HOÃN
// tải + reboot. NULL = cập nhật ngay.
static bool (*s_busy_check)(void);

// Phiên bản đang chạy. NULL = lấy từ mô tả app trong binary (PROJECT_VER) — một
// nguồn duy nhất cho hello, heartbeat, OTA. Trước đây heartbeat dùng
// cfg.fw_version còn OTA dùng Kconfig: hai con số lệch nhau, OTA tải lại mãi.
static const char *s_fw_override;

void ie_fw_version_set(const char *version)
{
    s_fw_override = (version && version[0]) ? version : NULL;
}

const char *ie_fw_version(void)
{
    return s_fw_override ? s_fw_override : esp_app_get_description()->version;
}

void ie_ota_client_set_busy_check(bool (*fn)(void))
{
    s_busy_check = fn;
}

static bool is_busy(void)
{
    return s_busy_check && s_busy_check();
}

// ── Manifest (POST /ota/v1/) ────────────────────────────────────────────────

typedef struct {
    char *buf;
    int len;
} rx_t;

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    rx_t *rx = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && rx) {
        int copy = evt->data_len;
        if (rx->len + copy > IE_OTA_MANIFEST_MAX - 1) {
            copy = IE_OTA_MANIFEST_MAX - 1 - rx->len;
        }
        if (copy > 0) {
            memcpy(rx->buf + rx->len, evt->data, copy);
            rx->len += copy;
            rx->buf[rx->len] = 0;
        }
    }
    return ESP_OK;
}

static void build_url(const char *base, char *out, size_t out_len)
{
    size_t n = strlen(base ? base : "");
    const char *slash = (n > 0 && base[n - 1] == '/') ? "" : "/";
    snprintf(out, out_len, "%s%sota/v1/", base ? base : CONFIG_INNOEDGE_SERVER_BASE_URL, slash);
}

// ie_ota_mark_valid: gọi SAU khi firmware mới chạy khỏe (WS kết nối được lần
// đầu). Nếu KHÔNG gọi (firmware mới crash/treo trước khi kết nối), bootloader
// tự rollback về slot cũ ở lần reboot — chống brick máy ngoài hiện trường.
void ie_ota_mark_valid(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!running) {
        return;
    }
    esp_ota_img_states_t st;
    if (esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
            ESP_LOGI(TAG, "OTA: app marked VALID, rollback cancelled");
        } else {
            ESP_LOGW(TAG, "OTA: mark_app_valid failed");
            ie_fault_set("ota_mark_valid_failed", "critical",
                         "Khong xac nhan duoc firmware moi - co the bi rollback");
        }
    }
}

// ── Tải + xác thực image ────────────────────────────────────────────────────

typedef struct {
    const char *version;
    const char *url;
    const char *sha256;
    size_t size;
} ota_image_t;

// Tải vào partition dự phòng, kiểm ĐỦ ba thứ trước khi cho boot:
//   1. đúng số byte manifest khai (chặn file cụt / bị thay),
//   2. SHA-256 khớp manifest (chặn image bị sửa trên đường hoặc trên CDN),
//   3. mô tả app trong image: cùng project, và (khi phiên bản lấy từ PROJECT_VER)
//      đúng version manifest — image ghi sai version là nguồn của OTA loop.
// ESP-IDF tự kiểm định dạng image/chip/chữ ký ở esp_ota_end().
static esp_err_t download_and_stage(const ota_image_t *img)
{
    uint8_t expected[32], digest[32];
    if (!img->url || strncmp(img->url, "https://", 8) != 0) {
        ESP_LOGW(TAG, "bỏ qua URL không phải https");
        return ESP_ERR_INVALID_ARG;
    }
    if (!ie_ota_digest(img->sha256, expected)) {
        ESP_LOGW(TAG, "manifest thiếu/sai sha256 — từ chối cài");
        return ESP_ERR_INVALID_ARG;
    }
    const esp_partition_t *target = esp_ota_get_next_update_partition(NULL);
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (!target || target == running || img->size == 0 || img->size > target->size) {
        ESP_LOGW(TAG, "size=%u không vừa partition dự phòng", (unsigned)img->size);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_http_client_config_t cfg = {
        .url = img->url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };
    esp_http_client_handle_t http = esp_http_client_init(&cfg);
    if (!http) {
        return ESP_ERR_NO_MEM;
    }
    esp_ota_handle_t ota = 0;
    uint8_t *buf = NULL;
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);

    esp_err_t err = ESP_OK;
    int64_t content_length = -1;
    for (int hop = 0;; hop++) {
        err = esp_http_client_open(http, 0);
        if (err != ESP_OK) {
            goto done;
        }
        content_length = esp_http_client_fetch_headers(http);
        int code = esp_http_client_get_status_code(http);
        if (code != 301 && code != 302 && code != 303 && code != 307 && code != 308) {
            break;
        }
        // CDN / GitHub Release trả 302 sang nơi chứa file thật.
        char where[256] = {0};
        if (hop >= IE_OTA_MAX_REDIRECTS || esp_http_client_set_redirection(http) != ESP_OK ||
            esp_http_client_get_url(http, where, sizeof(where)) != ESP_OK ||
            strncmp(where, "https://", 8) != 0) {
            ESP_LOGW(TAG, "redirect không hợp lệ (lần %d, tới '%s')", hop + 1, where);
            err = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }
        esp_http_client_flush_response(http, NULL);
        esp_http_client_close(http);
    }
    if (esp_http_client_get_status_code(http) != 200 || content_length != (int64_t)img->size) {
        ESP_LOGW(TAG, "HTTP %d, content-length=%lld (cần %u)",
                 esp_http_client_get_status_code(http), (long long)content_length,
                 (unsigned)img->size);
        err = ESP_ERR_INVALID_RESPONSE;
        goto done;
    }
    err = esp_ota_begin(target, img->size, &ota);
    if (err != ESP_OK) {
        goto done;
    }
    buf = malloc(IE_OTA_CHUNK);
    if (!buf || mbedtls_sha256_starts(&hash, 0) != 0) {
        err = ESP_ERR_NO_MEM;
        goto done;
    }
    size_t received = 0;
    while (received < img->size) {
        size_t want = img->size - received;
        int n = esp_http_client_read(http, (char *)buf, want < IE_OTA_CHUNK ? want : IE_OTA_CHUNK);
        if (n <= 0) {
            err = ESP_ERR_INVALID_RESPONSE;
            goto done;
        }
        if (mbedtls_sha256_update(&hash, buf, n) != 0) {
            err = ESP_FAIL;
            goto done;
        }
        err = esp_ota_write(ota, buf, n);
        if (err != ESP_OK) {
            goto done;
        }
        received += n;
    }
    if (mbedtls_sha256_finish(&hash, digest) != 0 || memcmp(digest, expected, 32) != 0) {
        ESP_LOGE(TAG, "SHA-256 KHÔNG khớp manifest — huỷ");
        err = ESP_ERR_INVALID_CRC;
        goto done;
    }
    err = esp_ota_end(ota);
    ota = 0;
    if (err != ESP_OK) {
        goto done;
    }
    esp_app_desc_t desc;
    err = esp_ota_get_partition_description(target, &desc);
    if (err != ESP_OK) {
        goto done;
    }
    const esp_app_desc_t *self = esp_app_get_description();
    if (strncmp(desc.project_name, self->project_name, sizeof(desc.project_name)) != 0) {
        ESP_LOGE(TAG, "image của project '%s', máy đang chạy '%s' — huỷ",
                 desc.project_name, self->project_name);
        err = ESP_ERR_INVALID_VERSION;
        goto done;
    }
    if (!s_fw_override && strncmp(desc.version, img->version, sizeof(desc.version)) != 0) {
        ESP_LOGE(TAG, "image ghi version '%s' nhưng manifest nói '%s' — huỷ (tránh OTA lặp)",
                 desc.version, img->version);
        err = ESP_ERR_INVALID_VERSION;
        goto done;
    }
    if (is_busy()) { // khách bắt đầu giao dịch trong lúc tải
        err = ESP_ERR_INVALID_STATE;
        goto done;
    }
    err = esp_ota_set_boot_partition(target);

done:
    if (ota) {
        esp_ota_abort(ota);
    }
    free(buf);
    mbedtls_sha256_free(&hash);
    esp_http_client_close(http);
    esp_http_client_cleanup(http);
    return err;
}

static size_t json_size(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    return (cJSON_IsNumber(v) && v->valuedouble > 0) ? (size_t)v->valuedouble : 0;
}

static const char *json_str(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsString(v) ? v->valuestring : NULL;
}

static void maybe_run_ota(const cJSON *fw, ie_ota_result_t *result)
{
    ota_image_t img = {
        .version = json_str(fw, "version"),
        .url = json_str(fw, "url"),
        .sha256 = json_str(fw, "sha256"),
        .size = json_size(fw, "size"),
    };
    bool allow_downgrade = cJSON_IsTrue(cJSON_GetObjectItem(fw, "allowDowngrade"));
    if (!img.version || !ie_ota_version_allowed(img.version, ie_fw_version(), allow_downgrade)) {
        return; // cùng/cũ hơn bản đang chạy, hoặc version không phải X.Y.Z
    }
    if (is_busy()) {
        ESP_LOGW(TAG, "OTA %s sẵn sàng nhưng máy đang bận — hoãn", img.version);
        if (result) {
            result->deferred = true;
        }
        return;
    }
    ota_last_t last = {0};
    if (ie_config_store_load_blob(OTA_LAST_KEY, &last, sizeof(last)) == ESP_OK &&
        strncmp(last.target, img.version, sizeof(last.target)) == 0 &&
        strncmp(last.from, ie_fw_version(), sizeof(last.from)) == 0) {
        ESP_LOGE(TAG, "đã cài %s lúc đang chạy %s mà vẫn báo %s — dừng để tránh OTA lặp",
                 img.version, last.from, ie_fw_version());
        ie_fault_set("ota_loop", "critical",
                     "Da cai ban OTA nay ma may van chay ban cu (rollback/version khong doi) - kiem tra");
        return;
    }
    ESP_LOGW(TAG, "bản mới %s (đang chạy %s) — tải", img.version, ie_fw_version());
    esp_err_t err = download_and_stage(&img);
    if (err == ESP_OK) {
        snprintf(last.target, sizeof(last.target), "%s", img.version);
        snprintf(last.from, sizeof(last.from), "%s", ie_fw_version());
        ie_config_store_save_blob(OTA_LAST_KEY, &last, sizeof(last));
        // Chờ lệnh / on_paid đang chạy xong rồi mới reboot (khoá không bao giờ trả).
        ie_command_bus_hold();
        ESP_LOGI(TAG, "OTA xong, khởi động lại");
        esp_restart();
    }
    if (err == ESP_ERR_INVALID_STATE) {
        if (result) {
            result->deferred = true;
        }
        return;
    }
    ESP_LOGE(TAG, "OTA thất bại: %s", esp_err_to_name(err));
    ie_ws_send_alert("ota_update_failed", "warning",
                     "Cap nhat firmware that bai - may dang chay ban cu", true);
}

esp_err_t ie_ota_client_check_once(ie_device_config_t *config, ie_ota_result_t *result)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    if (result) {
        memset(result, 0, sizeof(*result));
    }

    char url[192];
    build_url(config->server_base_url, url, sizeof(url));
    char body[256];
    snprintf(body, sizeof(body),
             "{\"version\":\"%s\",\"application\":{\"version\":\"%s\"},"
             "\"mac_address\":\"%s\"}",
             ie_fw_version(), ie_fw_version(), config->device_id);

    rx_t rx = {.buf = calloc(1, IE_OTA_MANIFEST_MAX)};
    if (!rx.buf) {
        return ESP_ERR_NO_MEM;
    }
    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
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
    esp_http_client_set_header(cli, "Content-Type", "application/json");
    if (config->device_token[0] != '\0') {
        char auth[192];
        snprintf(auth, sizeof(auth), "Bearer %s", config->device_token);
        esp_http_client_set_header(cli, "Authorization", auth);
    }
    esp_http_client_set_post_field(cli, body, strlen(body));

    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "OTA bootstrap failed err=%s status=%d", esp_err_to_name(err), status);
        free(rx.buf);
        return err == ESP_OK ? ESP_FAIL : err;
    }

    cJSON *root = cJSON_Parse(rx.buf);
    free(rx.buf);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) {
        data = root;
    }

    cJSON *ws = cJSON_GetObjectItem(data, "websocket");
    const char *ws_url = ws ? json_str(ws, "url") : NULL;
    if (ws_url && ws_url[0] != '\0') {
        snprintf(config->websocket_url, sizeof(config->websocket_url), "%s", ws_url);
        ie_config_store_save_websocket_url(config->websocket_url);
        if (result) {
            result->websocket_url_updated = true;
        }
    }

    cJSON *assignment = cJSON_GetObjectItem(data, "assignment");
    const char *assign_status = assignment ? json_str(assignment, "status") : NULL;
    if (assign_status && strcmp(assign_status, "unassigned") == 0) {
        config->assigned = false;
        if (result) {
            result->unassigned = true;
        }
    } else if (assignment) {
        config->assigned = true;
    }

    cJSON *fw = cJSON_GetObjectItem(data, "firmware");
    if (cJSON_IsObject(fw)) {
        maybe_run_ota(fw, result);
    }

    cJSON_Delete(root);
    return ESP_OK;
}
