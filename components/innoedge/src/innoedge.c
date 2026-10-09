// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// InnoEdge SDK — hiện thực API công khai (xem include/innoedge.h).
//
// File này là lớp mỏng gói các component hạ tầng đã chạy thật trên fleet
// (config_store / wifi_manager / provisioning / net / payment_queue /
// command_bus). Mọi thứ liên quan màn hình, relay, nghiệp vụ đều KHÔNG ở đây.

#include "innoedge.h"

#include "ie_command_bus.h"
#include "ie_config_client.h"
#include "ie_config_store.h"
#include "ie_fault.h"
#include "ie_ota_client.h"
#include "ie_paid_guard.h"
#include "ie_payment_queue.h"
#include "ie_provisioning.h"
#include "ie_wifi_manager.h"
#include "ie_ws_client.h"
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

static ie_device_config_t s_dev;
static innoedge_config_t s_cfg;
static innoedge_events_t s_ev;
static bool s_inited;
static bool s_online_started;

// ── Tiện ích nội bộ ─────────────────────────────────────────────────────────

static uint32_t heartbeat_period_ms(void)
{
    return (s_cfg.heartbeat_sec ? s_cfg.heartbeat_sec : 30) * 1000U;
}

// OTA chạy nền theo chu kỳ; "kiểm tra ngay" (lệnh cloud ota_check hoặc
// innoedge_ota_check) chỉ ĐÁNH THỨC task này, không tải ngay trên task gọi —
// tải 1-2 MB trên task WS là chặn WS cả phút, cloud tưởng máy rớt mạng.
// ponytail: chu kỳ cố định 6 giờ; thêm Kconfig khi có khách cần khác.
#define IE_OTA_PERIOD_MS (6U * 60U * 60U * 1000U)
#define IE_OTA_BUSY_RETRY_MS (10U * 60U * 1000U)
static TaskHandle_t s_ota_task;

static void try_send_queue_head(void)
{
    if (!ie_ws_client_is_connected()) {
        return;
    }
    ie_payment_event_t ev = {0};
    if (ie_payment_queue_peek(&ev) == ESP_OK && ev.json[0] != '\0') {
        esp_err_t err = ie_ws_client_send_text(ev.json);
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
    ie_payment_queue_ack(seq);
    try_send_queue_head(); // ack xong là đẩy tiếp phần tử kế → drain nhanh
}

static void on_payment_ack(const ie_payment_ack_message_t *ack, void *ctx)
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
        ESP_ERROR_CHECK_WITHOUT_ABORT(ie_config_store_save_assigned(false));
        if (s_ev.on_unassigned) {
            s_ev.on_unassigned();
        }
    }
}

static void on_qr(const ie_qr_message_t *qr, void *ctx)
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

static void send_paid_ack(int64_t intent_id)
{
    char ack[64];
    snprintf(ack, sizeof(ack), "{\"type\":\"paid_ack\",\"intentId\":%lld}",
             (long long)intent_id);
    ie_ws_client_send_text(ack);
}

// on_paid là tín hiệu DUY NHẤT được giao hàng → phải tới application đúng MỘT
// lần mỗi intent, kể cả khi cloud gửi lại frame sau reconnect (xem ie_paid_guard.h).
static void on_payment_paid(int64_t intent_id, int64_t amount, void *ctx)
{
    (void)ctx;
    size_t slot = 0;
    switch (ie_paid_guard_begin(intent_id, &slot)) {
    case IE_PAID_DUPLICATE:
        ESP_LOGW(TAG, "payment_paid intent=%lld gửi lại — chỉ ack, KHÔNG giao lần hai",
                 (long long)intent_id);
        send_paid_ack(intent_id);
        return;
    case IE_PAID_UNCERTAIN: {
        // Ack để cloud thôi gửi lại, nhưng KHÔNG giao (không chắc đã giao hay chưa)
        // và báo người vận hành: khách có thể đã trả mà chưa nhận hàng.
        char msg[96];
        snprintf(msg, sizeof(msg), "QR intent %lld da tra, khong chac da giao - doi soat",
                 (long long)intent_id);
        ESP_LOGE(TAG, "%s", msg);
        send_paid_ack(intent_id);
        ie_ws_send_alert("paid_uncertain", "critical", msg, true);
        return;
    }
    case IE_PAID_REFUSE:
        ESP_LOGE(TAG, "payment_paid intent=%lld chưa ghi nhận được — chờ cloud gửi lại",
                 (long long)intent_id);
        return;
    case IE_PAID_DELIVER:
        break;
    }
    send_paid_ack(intent_id);
    ie_command_bus_hold(); // OTA không được reboot giữa lúc application đang giao hàng
    if (s_ev.on_paid) {
        s_ev.on_paid(intent_id, amount);
    }
    ie_paid_guard_done(slot);
    ie_command_bus_release();
}

static void on_static_qr(const char *payload, const char *ref_code, void *ctx)
{
    (void)ctx;
    if (!payload || payload[0] == '\0') {
        return;
    }
    ie_config_store_save_static_qr(payload, ref_code);
    snprintf(s_dev.static_qr_payload, sizeof(s_dev.static_qr_payload), "%s", payload);
    snprintf(s_dev.static_qr_ref, sizeof(s_dev.static_qr_ref), "%s",
             ref_code ? ref_code : "");
    if (s_ev.on_static_qr) {
        s_ev.on_static_qr(s_dev.static_qr_payload, s_dev.static_qr_ref);
    }
}

static void on_binary(const uint8_t *data, size_t len, void *ctx)
{
    (void)ctx;
    if (s_ev.on_binary) {
        s_ev.on_binary(data, len);
    }
}

static void on_frame(const char *type, const char *raw, void *ctx)
{
    (void)ctx;
    if (s_ev.on_frame) {
        s_ev.on_frame(type, raw);
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
            ie_config_store_save_token(auth_token) == ESP_OK) {
            snprintf(s_dev.device_token, sizeof(s_dev.device_token), "%s", auth_token);
        }
        s_dev.assigned = true;
        ESP_ERROR_CHECK_WITHOUT_ABORT(ie_config_store_save_assigned(true));
        if (s_ev.on_assigned) {
            s_ev.on_assigned();
        }
    } else if (strcmp(command, "reboot") == 0) {
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else if (strcmp(command, "ota_check") == 0) {
        innoedge_ota_check();
    } else if (strcmp(command, "ble_provision") == 0) {
        ie_provisioning_start(&s_dev);
    }
}

static void on_dynamic_command(int64_t command_id, const char *action,
                               const char *params_json, void *ctx)
{
    (void)ctx;
    ie_command_bus_dispatch(command_id, action, params_json);
}

// ── Task nền ────────────────────────────────────────────────────────────────

static void heartbeat_task(void *arg)
{
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(heartbeat_period_ms()));
        if (ie_ws_client_is_connected()) {
            ie_ws_client_send_heartbeat(ie_fw_version(), ie_wifi_manager_rssi(),
                                          (unsigned)ie_payment_queue_count(),
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
    bool first = true;
    while (true) {
        bool was_assigned = s_dev.assigned;
        ie_ota_result_t res = {0};
        esp_err_t err = ie_ota_client_check_once(&s_dev, &res);
        // Báo "chưa gán" khi mới boot hoặc khi vừa bị gỡ — không lặp mỗi chu kỳ.
        if (err == ESP_OK && res.unassigned && (first || was_assigned) && s_ev.on_unassigned) {
            s_ev.on_unassigned();
        }
        first = false;
        uint32_t wait_ms = res.deferred ? IE_OTA_BUSY_RETRY_MS : IE_OTA_PERIOD_MS;
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(wait_ms));
    }
}

static void config_task(void *arg)
{
    (void)arg;
    int version = 0;
    if (ie_config_client_fetch(&s_dev, &version) != ESP_OK) {
        version = ie_config_store_op_config_version(); // lỗi mạng → dùng cache
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

    ie_ws_handlers_t handlers = {
        .on_ack = on_ack,
        .on_payment_ack = on_payment_ack,
        .on_command = on_command,
        .on_dynamic_command = on_dynamic_command,
        .on_status = on_status,
        .on_qr = on_qr,
        .on_qr_error = on_qr_error,
        .on_payment_paid = on_payment_paid,
        .on_static_qr = on_static_qr,
        .on_binary = on_binary,
        .on_frame = on_frame,
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(ie_ws_client_start(&s_dev, &handlers));

    xTaskCreate(heartbeat_task, "ie_hb", 4096, NULL, 4, NULL);
    xTaskCreate(queue_sender_task, "ie_qsend", 4096, NULL, 4, NULL);
    if (!s_cfg.disable_ota) {
        // OTA chạy NỀN: mạng yếu mà tải đồng bộ = máy như treo lúc boot.
        ie_ota_client_set_busy_check(s_cfg.busy_check);
        xTaskCreate(ota_task, "ie_ota", 8192, NULL, 4, &s_ota_task);
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
    if (ie_wifi_manager_wait_connected(0) == ESP_OK) {
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

    ie_fw_version_set(s_cfg.fw_version);
    ESP_RETURN_ON_ERROR(ie_config_store_load(&s_dev), TAG, "nạp cấu hình thiết bị");
    ESP_RETURN_ON_ERROR(ie_payment_queue_init(), TAG, "hàng đợi giao dịch");
    ESP_RETURN_ON_ERROR(ie_command_bus_init(), TAG, "command bus");
    ie_paid_guard_init();
    if (s_ev.on_provisioning) {
        ie_provisioning_set_ui_notify(s_ev.on_provisioning);
    }

    // Quên đổi cloud URL là lỗi first-run phổ biến nhất. Nói thẳng thay vì để
    // dev ngồi đoán tại sao WebSocket không bao giờ kết nối.
    if (strstr(s_dev.server_base_url, "example.com") != NULL) {
        ESP_LOGE(TAG, "CONFIG_INNOEDGE_SERVER_BASE_URL vẫn là placeholder (%s).",
                 s_dev.server_base_url);
        ESP_LOGE(TAG, "Sửa: idf.py menuconfig → InnoEdge SDK → cloud base URL");
        return ESP_ERR_INVALID_STATE;
    }

    s_inited = true;
    ESP_LOGI(TAG, "SDK %s · device=%s · fw=%s · assigned=%d",
             INNOEDGE_SDK_VERSION, s_dev.device_id, ie_fw_version(), s_dev.assigned);
    return ESP_OK;
}

esp_err_t innoedge_start(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!s_dev.provisioned) {
        ESP_LOGI(TAG, "chưa có WiFi — mở provisioning");
        return ie_provisioning_start(&s_dev);
    }

    esp_err_t err = ie_wifi_manager_start(&s_dev);
    if (err != ESP_OK) {
        // WiFi ĐÃ lưu nhưng chưa lên kịp: mở provisioning làm lưới đỡ (đổi WiFi
        // vẫn được) VÀ chờ nền — driver tự retry, có mạng là dịch vụ tự chạy.
        ESP_LOGW(TAG, "WiFi chưa lên (%s) — chờ nền", esp_err_to_name(err));
        ie_provisioning_start(&s_dev);
        xTaskCreate(wifi_resume_task, "ie_wifiwait", 8192, NULL, 4, NULL);
        return ESP_OK;
    }

    start_online_services();
    return ESP_OK;
}

bool innoedge_is_online(void) { return ie_ws_client_is_connected(); }
bool innoedge_is_assigned(void) { return s_dev.assigned; }
const char *innoedge_device_id(void) { return s_dev.device_id; }
uint32_t innoedge_queue_depth(void) { return ie_payment_queue_count(); }

esp_err_t innoedge_publish_payment(innoedge_payment_kind_t kind, int count,
                                   int64_t amount_vnd)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t seq = 0;
    esp_err_t err = ie_config_store_next_seq(&seq);
    if (err != ESP_OK) {
        // Không cấp được seq = tiền đã thu mà không ghi nhận được → phải báo.
        ie_fault_set("seq_alloc_failed", "critical",
                       "Khong cap duoc ma giao dich - co the mat tien");
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
    err = ie_payment_queue_append(seq, json, &dropped);
    if (err != ESP_OK) {
        ie_fault_set("payment_enqueue_failed", "critical",
                       "Khong luu duoc giao dich da thu - co the mat tien");
        return err;
    }
    if (dropped) {
        // Hàng đợi đầy → đã ghi đè giao dịch cũ nhất = MẤT TIỀN. Không im lặng.
        ie_fault_set("payment_queue_overflow_drop", "critical",
                       "Hang doi day - mot giao dich da thu bi mat");
    }
    if (ie_payment_queue_count() >= (uint32_t)(CONFIG_INNOEDGE_PAYMENT_QUEUE_CAP * 8 / 10)) {
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
    esp_err_t err = ie_config_store_next_seq(&seq);
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
    err = ie_payment_queue_append(seq, json, &dropped);
    if (err != ESP_OK) {
        return err;
    }
    if (dropped) {
        ie_fault_set("payment_queue_overflow_drop", "critical",
                       "Hang doi day - mot su kien da bi mat");
    }
    try_send_queue_head();
    return ESP_OK;
}

esp_err_t innoedge_send_binary(const uint8_t *data, size_t len)
{
    return ie_ws_client_send_binary(data, len);
}

esp_err_t innoedge_alert(const char *code, const char *severity,
                         const char *message, bool active)
{
    return ie_ws_send_alert(code, severity, message, active);
}

esp_err_t innoedge_register_command(const char *action, innoedge_command_fn fn)
{
    return ie_command_bus_register(action, fn);
}

void innoedge_reboot_after_ack(void)
{
    ie_command_bus_request_reboot();
}

esp_err_t innoedge_request_qr(int64_t amount_vnd)
{
    if (!s_dev.assigned || !ie_ws_client_is_connected()) {
        return ESP_ERR_INVALID_STATE;
    }
    uint64_t seq = 0;
    ESP_RETURN_ON_ERROR(ie_config_store_next_seq(&seq), TAG, "cấp seq QR");
    return ie_ws_client_request_qr(seq, amount_vnd);
}

esp_err_t innoedge_config_json(char *out, size_t out_len, int *version)
{
    return ie_config_store_get_op_config(out, out_len, version);
}

esp_err_t innoedge_config_reload(void)
{
    int version = 0;
    esp_err_t err = ie_config_client_fetch(&s_dev, &version);
    if (err == ESP_OK && s_ev.on_config) {
        s_ev.on_config(version);
    }
    return err;
}

esp_err_t innoedge_ota_check(void)
{
    if (s_ota_task) {
        xTaskNotifyGive(s_ota_task); // task OTA tự kiểm, đúng một chỗ tải
        return ESP_OK;
    }
    // disable_ota: application tự chọn thời điểm → kiểm ngay trên task gọi.
    return ie_ota_client_check_once(&s_dev, NULL);
}
