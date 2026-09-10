// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// InnoEdge SDK — hiện thực API công khai (xem include/innoedge.h).
//
// File này là lớp mỏng gói các component hạ tầng đã chạy thật trên fleet
// (config_store / wifi_manager / provisioning / net / payment_queue /
// command_bus). Mọi thứ liên quan màn hình, relay, nghiệp vụ đều KHÔNG ở đây.

#include "innoedge.h"

#include "gtek_command_bus.h"
#include "gtek_config_client.h"
#include "gtek_config_store.h"
#include "gtek_ota_client.h"
#include "gtek_payment_queue.h"
#include "gtek_provisioning.h"
#include "gtek_wifi_manager.h"
#include "gtek_ws_client.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_wifi_default.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const char *TAG = "innoedge";

#define IE_OP_CONFIG_BUF 4352 // = cap op_config (4096) + lề

static gtek_device_config_t s_dev;
static innoedge_config_t s_cfg;
static innoedge_events_t s_ev;
static bool s_inited;
static bool s_online_started;

// ── Tiện ích nội bộ ─────────────────────────────────────────────────────────

static uint32_t heartbeat_period_ms(void)
{
    return (s_cfg.heartbeat_sec ? s_cfg.heartbeat_sec : 30) * 1000U;
}

static const char *fw_version(void)
{
    return (s_cfg.fw_version && s_cfg.fw_version[0]) ? s_cfg.fw_version
                                                     : CONFIG_GTEK_FW_VERSION;
}

static void try_send_queue_head(void)
{
    if (!gtek_ws_client_is_connected()) {
        return;
    }
    gtek_payment_event_t ev = {0};
    if (gtek_payment_queue_peek(&ev) == ESP_OK && ev.json[0] != '\0') {
        esp_err_t err = gtek_ws_client_send_text(ev.json);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "gửi lại seq=%llu lỗi: %s",
                     (unsigned long long)ev.seq, esp_err_to_name(err));
        }
    }
}

// ── Callback WS → callback công khai ────────────────────────────────────────

static void on_ack(uint64_t seq, void *ctx)
{
    (void)ctx;
    gtek_payment_queue_ack(seq);
    try_send_queue_head(); // ack xong là đẩy tiếp phần tử kế → drain nhanh
}

static void on_payment_ack(const gtek_payment_ack_message_t *ack, void *ctx)
{
    (void)ctx;
    if (ack && s_ev.on_payment_ack) {
        s_ev.on_payment_ack(ack->method, ack->coins, ack->amount_vnd,
                            ack->rate_vnd, ack->duplicate);
    }
}

static void on_status(const char *state, void *ctx)
{
    (void)ctx;
    if (state && strcmp(state, "unassigned") == 0) {
        s_dev.assigned = false;
        ESP_ERROR_CHECK_WITHOUT_ABORT(gtek_config_store_save_assigned(false));
        if (s_ev.on_unassigned) {
            s_ev.on_unassigned();
        }
    }
}

static void on_qr(const gtek_qr_message_t *qr, void *ctx)
{
    (void)ctx;
    if (qr && s_ev.on_qr) {
        s_ev.on_qr(qr->qr_payload, qr->amount, qr->ref_code, qr->expires_sec,
                   qr->intent_id);
    }
}

static void on_qr_error(uint64_t seq, const char *message, void *ctx)
{
    (void)ctx;
    (void)seq;
    if (s_ev.on_qr_error) {
        s_ev.on_qr_error(message);
    }
}

static void on_payment_paid(int64_t intent_id, int64_t amount, void *ctx)
{
    (void)ctx;
    // Ack để cloud thôi gửi lại frame này.
    if (intent_id > 0 && gtek_ws_client_is_connected()) {
        char ack[64];
        snprintf(ack, sizeof(ack), "{\"type\":\"paid_ack\",\"intentId\":%lld}",
                 (long long)intent_id);
        gtek_ws_client_send_text(ack);
    }
    if (s_ev.on_paid) {
        s_ev.on_paid(intent_id, amount);
    }
}

static void on_static_qr(const char *payload, const char *ref_code, void *ctx)
{
    (void)ctx;
    if (!payload || payload[0] == '\0') {
        return;
    }
    gtek_config_store_save_static_qr(payload, ref_code);
    snprintf(s_dev.static_qr_payload, sizeof(s_dev.static_qr_payload), "%s", payload);
    snprintf(s_dev.static_qr_ref, sizeof(s_dev.static_qr_ref), "%s",
             ref_code ? ref_code : "");
    if (s_ev.on_static_qr) {
        s_ev.on_static_qr(s_dev.static_qr_payload, s_dev.static_qr_ref);
    }
}

// Lệnh "cứng" của nền tảng (không qua registry): gán máy, reboot, ota, đổi WiFi.
static void on_command(const char *command, const char *auth_token,
                       const char *raw_json, void *ctx)
{
    (void)ctx;
    (void)raw_json;
    if (!command) {
        return;
    }
    ESP_LOGI(TAG, "lệnh nền tảng: %s", command);
    if (strcmp(command, "activation_complete") == 0) {
        if (auth_token && auth_token[0] != '\0' &&
            gtek_config_store_save_token(auth_token) == ESP_OK) {
            snprintf(s_dev.device_token, sizeof(s_dev.device_token), "%s", auth_token);
        }
        s_dev.assigned = true;
        ESP_ERROR_CHECK_WITHOUT_ABORT(gtek_config_store_save_assigned(true));
        if (s_ev.on_assigned) {
            s_ev.on_assigned();
        }
    } else if (strcmp(command, "reboot") == 0) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else if (strcmp(command, "ota_check") == 0) {
        gtek_ota_client_check_once(&s_dev, NULL);
    } else if (strcmp(command, "ble_provision") == 0) {
        gtek_provisioning_start(&s_dev);
    }
}

static void on_dynamic_command(int64_t command_id, const char *action,
                               const char *params_json, void *ctx)
{
    (void)ctx;
    gtek_command_bus_dispatch(command_id, action, params_json);
}

// ── Task nền ────────────────────────────────────────────────────────────────

static void heartbeat_task(void *arg)
{
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(heartbeat_period_ms()));
        if (gtek_ws_client_is_connected()) {
            gtek_ws_client_send_heartbeat(fw_version(), gtek_wifi_manager_rssi(),
                                          (unsigned)gtek_payment_queue_count(),
                                          (int)esp_reset_reason());
        }
    }
}

static void queue_sender_task(void *arg)
{
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(3000));
        try_send_queue_head();
    }
}

static void ota_task(void *arg)
{
    (void)arg;
    gtek_ota_result_t res = {0};
    if (gtek_ota_client_check_once(&s_dev, &res) == ESP_OK && res.unassigned &&
        s_ev.on_unassigned) {
        s_ev.on_unassigned();
    }
    vTaskDelete(NULL);
}

static void config_task(void *arg)
{
    (void)arg;
    int version = 0;
    if (gtek_config_client_fetch(&s_dev, &version) != ESP_OK) {
        version = gtek_config_store_op_config_version(); // lỗi mạng → dùng cache
        ESP_LOGW(TAG, "tải cấu hình lỗi, dùng cache version=%d", version);
    }
    if (s_ev.on_config) {
        s_ev.on_config(version);
    }
    vTaskDelete(NULL);
}

static void start_online_services(void)
{
    if (s_online_started) {
        return;
    }
    s_online_started = true;

    if (!s_cfg.disable_ntp) {
        esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
        sntp.start = true;
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_sntp_init(&sntp));
    }

    gtek_ws_handlers_t handlers = {
        .on_ack = on_ack,
        .on_payment_ack = on_payment_ack,
        .on_command = on_command,
        .on_dynamic_command = on_dynamic_command,
        .on_status = on_status,
        .on_qr = on_qr,
        .on_qr_error = on_qr_error,
        .on_payment_paid = on_payment_paid,
        .on_static_qr = on_static_qr,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(gtek_ws_client_start(&s_dev, &handlers));

    xTaskCreate(heartbeat_task, "ie_hb", 4096, NULL, 4, NULL);
    xTaskCreate(queue_sender_task, "ie_qsend", 4096, NULL, 4, NULL);
    if (!s_cfg.disable_ota) {
        // OTA chạy NỀN: mạng yếu mà tải đồng bộ = máy như treo lúc boot.
        gtek_ota_client_set_busy_check(s_cfg.busy_check);
        xTaskCreate(ota_task, "ie_ota", 8192, NULL, 4, NULL);
    }
    if (!s_cfg.disable_config_fetch) {
        xTaskCreate(config_task, "ie_cfg", 6144, NULL, 4, NULL);
    }
}

// WiFi đã lưu nhưng lên chậm (sóng yếu/router chậm): chờ nền rồi mới chạy dịch
// vụ — KHÔNG bắt người dùng cài lại WiFi.
static void wifi_resume_task(void *arg)
{
    (void)arg;
    if (gtek_wifi_manager_wait_connected(0) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi lên muộn — khởi động dịch vụ online");
        start_online_services();
    }
    vTaskDelete(NULL);
}

// ── API công khai ───────────────────────────────────────────────────────────

esp_err_t innoedge_init(const innoedge_config_t *cfg)
{
    if (s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    s_cfg = cfg ? *cfg : (innoedge_config_t){0};
    s_ev = (s_cfg.events) ? *s_cfg.events : (innoedge_events_t){0};

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_RETURN_ON_ERROR(err, TAG, "nvs init");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init");
    ESP_RETURN_ON_ERROR(esp_event_loop_create_default(), TAG, "event loop");
    esp_netif_create_default_wifi_sta();

    ESP_RETURN_ON_ERROR(gtek_config_store_load(&s_dev), TAG, "nạp cấu hình thiết bị");
    ESP_RETURN_ON_ERROR(gtek_payment_queue_init(), TAG, "hàng đợi giao dịch");
    ESP_RETURN_ON_ERROR(gtek_command_bus_init(), TAG, "command bus");
    if (s_ev.on_provisioning) {
        gtek_provisioning_set_ui_notify(s_ev.on_provisioning);
    }

    // Quên đổi cloud URL là lỗi first-run phổ biến nhất. Nói thẳng thay vì để
    // dev ngồi đoán tại sao WebSocket không bao giờ kết nối.
    if (strstr(s_dev.server_base_url, "example.com") != NULL) {
        ESP_LOGE(TAG, "CONFIG_GTEK_SERVER_BASE_URL vẫn là placeholder (%s).",
                 s_dev.server_base_url);
        ESP_LOGE(TAG, "Sửa: idf.py menuconfig → InnoEdge SDK → cloud base URL");
        return ESP_ERR_INVALID_STATE;
    }

    s_inited = true;
    ESP_LOGI(TAG, "SDK %s · device=%s · fw=%s · assigned=%d",
             INNOEDGE_SDK_VERSION, s_dev.device_id, fw_version(), s_dev.assigned);
    return ESP_OK;
}

esp_err_t innoedge_start(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_dev.provisioned) {
        ESP_LOGI(TAG, "chưa có WiFi — mở provisioning");
        return gtek_provisioning_start(&s_dev);
    }

    esp_err_t err = gtek_wifi_manager_start(&s_dev);
    if (err != ESP_OK) {
        // WiFi ĐÃ lưu nhưng chưa lên kịp: mở provisioning làm lưới đỡ (đổi WiFi
        // vẫn được) VÀ chờ nền — driver tự retry, có mạng là dịch vụ tự chạy.
        ESP_LOGW(TAG, "WiFi chưa lên (%s) — chờ nền", esp_err_to_name(err));
        gtek_provisioning_start(&s_dev);
        xTaskCreate(wifi_resume_task, "ie_wifiwait", 8192, NULL, 4, NULL);
        return ESP_OK;
    }

    start_online_services();
    return ESP_OK;
}

bool innoedge_is_online(void) { return gtek_ws_client_is_connected(); }
bool innoedge_is_assigned(void) { return s_dev.assigned; }
const char *innoedge_device_id(void) { return s_dev.device_id; }
uint32_t innoedge_queue_depth(void) { return gtek_payment_queue_count(); }

esp_err_t innoedge_publish_payment(innoedge_payment_kind_t kind, int count,
                                   int64_t amount_vnd)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t seq = 0;
    esp_err_t err = gtek_config_store_next_seq(&seq);
    if (err != ESP_OK) {
        // Không cấp được seq = tiền đã thu mà không ghi nhận được → phải báo.
        innoedge_alert("seq_alloc_failed", "critical",
                       "Khong cap duoc ma giao dich - co the mat tien", true);
        return err;
    }

    char json[192];
    long long ts = (long long)time(NULL);
    switch (kind) {
    case INNOEDGE_PAY_COIN:
        snprintf(json, sizeof(json),
                 "{\"type\":\"coin\",\"coins\":%d,\"seq\":%llu,\"ts\":%lld}",
                 count, (unsigned long long)seq, ts);
        break;
    case INNOEDGE_PAY_TICKET:
        snprintf(json, sizeof(json),
                 "{\"type\":\"ticket\",\"tickets\":%d,\"seq\":%llu,\"ts\":%lld}",
                 count, (unsigned long long)seq, ts);
        break;
    case INNOEDGE_PAY_COIN_OUT:
        snprintf(json, sizeof(json),
                 "{\"type\":\"payment\",\"method\":\"coin_out\",\"coins\":%d,"
                 "\"seq\":%llu,\"ts\":%lld}",
                 count, (unsigned long long)seq, ts);
        break;
    case INNOEDGE_PAY_CASH:
    default:
        snprintf(json, sizeof(json),
                 "{\"type\":\"payment\",\"method\":\"cash\",\"amount\":%lld,"
                 "\"seq\":%llu,\"ts\":%lld}",
                 (long long)amount_vnd, (unsigned long long)seq, ts);
        break;
    }

    bool dropped = false;
    err = gtek_payment_queue_append(seq, json, &dropped);
    if (err != ESP_OK) {
        innoedge_alert("payment_enqueue_failed", "critical",
                       "Khong luu duoc giao dich da thu - co the mat tien", true);
        return err;
    }
    if (dropped) {
        // Hàng đợi đầy → đã ghi đè giao dịch cũ nhất = MẤT TIỀN. Không im lặng.
        innoedge_alert("payment_queue_overflow_drop", "critical",
                       "Hang doi day - mot giao dich da thu bi mat", true);
    }
    if (gtek_payment_queue_count() >= (uint32_t)(CONFIG_GTEK_PAYMENT_QUEUE_CAP * 8 / 10)) {
        innoedge_alert("payment_queue_backlog_high", "warning",
                       "Nhieu giao dich chua gui duoc len server", true);
    }
    try_send_queue_head();
    return ESP_OK;
}

esp_err_t innoedge_publish_event(const char *name, const char *data_json)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!name || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    // Khung này vào HÀNG ĐỢI BỀN (NVS, FIFO). Một khung JSON hỏng ở đầu hàng
    // sẽ không bao giờ được cloud ack → chặn mọi giao dịch tiền phía sau, sống
    // qua reboot. Nên kiểm nghiêm ở đây, KHÔNG tin caller.
    for (const char *c = name; *c; c++) {
        bool ok = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') ||
                  (*c >= '0' && *c <= '9') || *c == '_' || *c == '.' || *c == '-';
        if (!ok) {
            ESP_LOGW(TAG, "event name '%s' có ký tự không hợp lệ — bỏ", name);
            return ESP_ERR_INVALID_ARG;
        }
    }
    if (!data_json || data_json[0] == '\0') {
        data_json = "{}";
    }
    cJSON *probe = cJSON_Parse(data_json);
    bool is_object = probe && cJSON_IsObject(probe);
    cJSON_Delete(probe);
    if (!is_object) {
        ESP_LOGW(TAG, "event %s: data không phải JSON object — bỏ", name);
        return ESP_ERR_INVALID_ARG;
    }
    uint64_t seq = 0;
    esp_err_t err = gtek_config_store_next_seq(&seq);
    if (err != ESP_OK) {
        return err;
    }
    char json[192]; // = sức chứa một phần tử hàng đợi
    int n = snprintf(json, sizeof(json),
                     "{\"type\":\"event\",\"name\":\"%s\",\"seq\":%llu,\"ts\":%lld,\"data\":%s}",
                     name, (unsigned long long)seq, (long long)time(NULL), data_json);
    if (n < 0 || n >= (int)sizeof(json)) {
        ESP_LOGW(TAG, "event %s quá dài (%d byte) — bỏ", name, n);
        return ESP_ERR_INVALID_SIZE;
    }
    bool dropped = false;
    err = gtek_payment_queue_append(seq, json, &dropped);
    if (err != ESP_OK) {
        return err;
    }
    if (dropped) {
        innoedge_alert("payment_queue_overflow_drop", "critical",
                       "Hang doi day - mot su kien da bi mat", true);
    }
    try_send_queue_head();
    return ESP_OK;
}

esp_err_t innoedge_alert(const char *code, const char *severity,
                         const char *message, bool active)
{
    return gtek_ws_send_alert(code, severity, message, active);
}

esp_err_t innoedge_register_command(const char *action, innoedge_command_fn fn)
{
    return gtek_command_bus_register(action, fn);
}

void innoedge_reboot_after_ack(void)
{
    gtek_command_bus_request_reboot();
}

esp_err_t innoedge_request_qr(int64_t amount_vnd)
{
    if (!s_dev.assigned || !gtek_ws_client_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t seq = 0;
    ESP_RETURN_ON_ERROR(gtek_config_store_next_seq(&seq), TAG, "cấp seq QR");
    return gtek_ws_client_request_qr(seq, amount_vnd);
}

esp_err_t innoedge_config_json(char *out, size_t out_len, int *version)
{
    return gtek_config_store_get_op_config(out, out_len, version);
}

esp_err_t innoedge_config_reload(void)
{
    int version = 0;
    esp_err_t err = gtek_config_client_fetch(&s_dev, &version);
    if (err == ESP_OK && s_ev.on_config) {
        s_ev.on_config(version);
    }
    return err;
}

esp_err_t innoedge_ota_check(void)
{
    return gtek_ota_client_check_once(&s_dev, NULL);
}
