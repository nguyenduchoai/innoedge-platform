// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "ie_ota_client.h"
#include "ie_fault.h"
#include "ie_ws_client.h"

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ie.ota";
static char s_rx_buf[4096];
static int s_rx_len;

// Busy-check (board wiring đăng ký): true = khách đang giữa giao dịch → HOÃN
// tải+reboot OTA (check định kỳ sau sẽ thử lại lúc máy rảnh). Không đăng ký =
// hành vi cũ (update ngay).
static bool (*s_busy_check)(void);

void ie_ota_client_set_busy_check(bool (*fn)(void))
{
    s_busy_check = fn;
}

static esp_err_t on_http_event(esp_http_client_event_t *evt)
{
    if (evt->event_id == HTTP_EVENT_ON_CONNECTED) {
        s_rx_len = 0;
    } else if (evt->event_id == HTTP_EVENT_ON_DATA) {
        int copy = evt->data_len;
        if (s_rx_len + copy > (int)sizeof(s_rx_buf) - 1) {
            copy = (int)sizeof(s_rx_buf) - 1 - s_rx_len;
        }
        if (copy > 0) {
            memcpy(s_rx_buf + s_rx_len, evt->data, copy);
            s_rx_len += copy;
            s_rx_buf[s_rx_len] = 0;
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

static void parse_semver(const char *s, int *a, int *b, int *c)
{
    *a = *b = *c = 0;
    if (s) {
        sscanf(s, "%d.%d.%d", a, b, c);
    }
}

// is_newer_version: CHỈ true khi candidate > current (semver). Chống ANTI-ROLLBACK
// — server/MITM không ép được máy hạ về bản cũ có lỗ hổng, và không OTA lại bản
// bằng/cũ hơn gây loop.
static bool is_newer_version(const char *candidate)
{
    if (!candidate || !candidate[0]) {
        return false;
    }
    int ca, cb, cc, ra, rb, rc;
    parse_semver(candidate, &ca, &cb, &cc);
    parse_semver(CONFIG_INNOEDGE_FW_VERSION, &ra, &rb, &rc);
    if (ca != ra) {
        return ca > ra;
    }
    if (cb != rb) {
        return cb > rb;
    }
    return cc > rc;
}

// ie_ota_mark_valid: gọi SAU khi firmware mới chạy khỏe (WS kết nối được lần
// đầu). Nếu app đang ở trạng thái PENDING_VERIFY (vừa OTA), xác nhận hợp lệ +
// huỷ rollback. Nếu KHÔNG gọi (firmware mới crash/treo trước khi kết nối),
// bootloader tự rollback về slot cũ ở lần reboot — chống brick máy ngoài hiện trường.
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
            // Không xác nhận được bản mới → bootloader có thể tự rollback ở lần
            // reboot tới. Báo để kỹ thuật theo dõi (ie_fault.h cùng net).
            ie_fault_set("ota_mark_valid_failed", "critical",
                           "Khong xac nhan duoc firmware moi - co the bi rollback");
        }
    }
}

static esp_err_t maybe_run_ota(const char *version, const char *url)
{
    if (!is_newer_version(version) || !url || url[0] == '\0') {
        return ESP_OK;
    }
    if (strncmp(url, "https://", 8) != 0) {
        ESP_LOGW(TAG, "skip OTA URL without https: %s", url);
        return ESP_OK;
    }
    if (s_busy_check && s_busy_check()) {
        // Khách đang thao tác/thanh toán — không tải 1.7MB + reboot đè lên giao
        // dịch. Lần check sau (định kỳ/lệnh ota_check) sẽ update lúc máy rảnh.
        ESP_LOGW(TAG, "OTA %s sẵn sàng nhưng máy đang bận — hoãn", version);
        return ESP_OK;
    }
    ESP_LOGW(TAG, "new firmware %s available, current %s", version, CONFIG_INNOEDGE_FW_VERSION);
    esp_http_client_config_t http_cfg = {
        .url = url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 60000,
        .keep_alive_enable = true,
    };
    esp_https_ota_config_t ota_cfg = {
        .http_config = &http_cfg,
    };
    esp_err_t err = esp_https_ota(&ota_cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA success, rebooting");
        esp_restart();
    }
    ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(err));
    // OTA hỏng = máy kẹt ở firmware cũ trong im lặng → báo để đội kỹ thuật biết.
    ie_ws_send_alert("ota_update_failed", "warning",
                       "Cap nhat firmware that bai - may dang chay ban cu", true);
    return err;
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
             "\"mac_address\":\"%s\",\"panel_res\":\"480x480\"}",
             CONFIG_INNOEDGE_FW_VERSION, CONFIG_INNOEDGE_FW_VERSION, config->device_id);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = on_http_event,
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
    esp_http_client_set_post_field(cli, body, strlen(body));

    s_rx_len = 0;
    esp_err_t err = esp_http_client_perform(cli);
    int status = esp_http_client_get_status_code(cli);
    esp_http_client_cleanup(cli);
    if (err != ESP_OK || status < 200 || status >= 300) {
        ESP_LOGW(TAG, "OTA bootstrap failed err=%s status=%d", esp_err_to_name(err), status);
        return err == ESP_OK ? ESP_FAIL : err;
    }

    ESP_LOGI(TAG, "OTA bootstrap response: %.*s", s_rx_len, s_rx_buf);
    cJSON *root = cJSON_Parse(s_rx_buf);
    if (!root) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *data = cJSON_GetObjectItem(root, "data");
    if (!data) {
        data = root;
    }

    cJSON *ws = cJSON_GetObjectItem(data, "websocket");
    cJSON *ws_url = ws ? cJSON_GetObjectItem(ws, "url") : NULL;
    if (cJSON_IsString(ws_url) && ws_url->valuestring[0] != '\0') {
        snprintf(config->websocket_url, sizeof(config->websocket_url), "%s", ws_url->valuestring);
        ie_config_store_save_websocket_url(config->websocket_url);
        if (result) {
            result->websocket_url_updated = true;
        }
    }

    cJSON *assignment = cJSON_GetObjectItem(data, "assignment");
    cJSON *status_j = assignment ? cJSON_GetObjectItem(assignment, "status") : NULL;
    if (cJSON_IsString(status_j) && strcmp(status_j->valuestring, "unassigned") == 0) {
        config->assigned = false;
        if (result) {
            result->unassigned = true;
        }
    } else if (assignment) {
        config->assigned = true;
    }

    cJSON *fw = cJSON_GetObjectItem(data, "firmware");
    cJSON *ver = fw ? cJSON_GetObjectItem(fw, "version") : NULL;
    cJSON *fw_url = fw ? cJSON_GetObjectItem(fw, "url") : NULL;
    if (cJSON_IsString(ver) && cJSON_IsString(fw_url)) {
        maybe_run_ota(ver->valuestring, fw_url->valuestring);
    }

    cJSON_Delete(root);
    return ESP_OK;
}
