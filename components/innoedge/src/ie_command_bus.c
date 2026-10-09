// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "ie_command_bus.h"

#include "ie_command_journal.h"
#include "ie_config_store.h"
#include "ie_fault.h"
#include "ie_ws_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "ie.cmdbus";

// ── REGISTRY (runtime) ──────────────────────────────────────────────────────
// SDK không biết trước nghiệp vụ nào — application đăng ký handler lúc khởi
// động bằng ie_command_bus_register(). command_bus thuần hạ tầng: dispatch +
// chống trùng + ack.
#ifndef IE_CMD_REGISTRY_MAX
#define IE_CMD_REGISTRY_MAX 24
#endif

static ie_command_entry_t s_registry[IE_CMD_REGISTRY_MAX];
static size_t s_registry_len;

// ── NHẬT KÝ LỆNH (chống trùng) ──────────────────────────────────────────────
// Xem ie_command_journal.h. Giữ khoá suốt handler để hai lệnh không bao giờ đan
// xen giữa lúc ghi RUNNING và lúc ghi kết quả.
static ie_command_journal_t s_journal;
static bool s_journal_ready;
static int64_t s_legacy_watermark; // last_cmd: id cao nhất từng BẮT ĐẦU chạy
static SemaphoreHandle_t s_lock;
#define JOURNAL_KEY "cmd_journal"

// Journal mới: mọi id <= retired_through là "quá cũ, không chắc" — không bao giờ
// chạy lại lệnh mà bản firmware trước (chỉ có watermark) có thể đã chạy.
static void journal_fresh(int64_t retired_through)
{
    memset(&s_journal, 0, sizeof(s_journal));
    s_journal.version = IE_COMMAND_JOURNAL_VERSION;
    s_journal.retired_through = retired_through > 0 ? retired_through : 0;
}

static void journal_load(void)
{
    s_legacy_watermark = ie_config_store_last_command_id();
    esp_err_t err = ie_config_store_load_blob(JOURNAL_KEY, &s_journal, sizeof(s_journal));
    if (err == ESP_ERR_NOT_FOUND) {
        journal_fresh(s_legacy_watermark);
        return;
    }
    if (err != ESP_OK || !ie_command_journal_valid(&s_journal)) {
        // Chặn luôn command bus thì máy không nhận được cả lệnh vá lỗi. Dựng lại
        // từ watermark là an toàn: watermark được ghi TRƯỚC mỗi handler.
        ESP_LOGE(TAG, "journal hỏng (%s) — dựng lại từ last_cmd=%lld",
                 esp_err_to_name(err), (long long)s_legacy_watermark);
        ie_fault_set("command_journal_reset", "warning",
                     "Nhat ky lenh hong, da dung lai - lenh cu can doi soat");
        journal_fresh(s_legacy_watermark);
        return;
    }
    // last_cmd vượt mọi id journal biết = bản firmware cũ (chỉ có watermark) đã
    // chạy lệnh trong lúc rollback → id tới đó mà journal không biết là "không
    // chắc". Bình thường last_cmd <= max id của journal (journal ghi trước), và
    // nâng retired_through lúc đó sẽ bỏ nhầm lệnh tới lệch thứ tự sau reboot.
    if (s_legacy_watermark > ie_command_journal_max_id(&s_journal)) {
        s_journal.retired_through = s_legacy_watermark;
    }
    for (size_t i = 0; i < IE_COMMAND_JOURNAL_SLOTS; i++) {
        if (s_journal.entries[i].state == IE_COMMAND_RUNNING) {
            ESP_LOGE(TAG, "commandId=%lld bị ngắt giữa chừng (mất điện?)",
                     (long long)s_journal.entries[i].id);
            ie_fault_set("command_interrupted", "critical",
                         "Lenh bi ngat khi mat dien - can doi soat, khong tu chay lai");
        }
    }
}

// Ghi RUNNING (và watermark cho bản firmware cũ nếu bị rollback) TRƯỚC handler.
static esp_err_t journal_begin(int64_t command_id, size_t *slot)
{
    *slot = ie_command_journal_begin(&s_journal, command_id);
    esp_err_t err = ie_config_store_save_blob(JOURNAL_KEY, &s_journal, sizeof(s_journal));
    if (err == ESP_OK && command_id > s_legacy_watermark) {
        err = ie_config_store_save_last_command_id(command_id);
        if (err == ESP_OK) {
            s_legacy_watermark = command_id;
        }
    }
    return err;
}

esp_err_t ie_command_bus_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    // Registry về rỗng: init() là "bắt đầu lại từ đầu" — handler phải đăng ký
    // SAU init (xem ie_command_bus.h).
    memset(s_registry, 0, sizeof(s_registry));
    s_registry_len = 0;
    journal_load();
    s_journal_ready = true;
    ESP_LOGI(TAG, "init: journal sẵn sàng, retired_through=%lld",
             (long long)s_journal.retired_through);
    return ESP_OK;
}

void ie_command_bus_hold(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

void ie_command_bus_release(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

// Reboot trễ: handler "reboot" của application yêu cầu, dispatcher vẫn kịp gửi
// ack rồi máy mới khởi động lại.
static void deferred_reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

void ie_command_bus_request_reboot(void)
{
    xTaskCreate(deferred_reboot_task, "ie_reboot", 2048, NULL, 5, NULL);
}

esp_err_t ie_command_bus_register(const char *action, ie_command_handler_fn handler)
{
    if (!action || action[0] == '\0' || !handler) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < s_registry_len; i++) {
        if (strcmp(s_registry[i].action, action) == 0) {
            s_registry[i].handler = handler; // đăng ký lại = ghi đè (test/override)
            return ESP_OK;
        }
    }
    if (s_registry_len >= IE_CMD_REGISTRY_MAX) {
        ESP_LOGE(TAG, "registry đầy (%d) — không nhận action=%s",
                 IE_CMD_REGISTRY_MAX, action);
        return ESP_ERR_NO_MEM;
    }
    s_registry[s_registry_len].action = action; // chuỗi phải sống lâu (literal)
    s_registry[s_registry_len].handler = handler;
    s_registry_len++;
    ESP_LOGI(TAG, "đăng ký action=%s (%u/%d)", action,
             (unsigned)s_registry_len, IE_CMD_REGISTRY_MAX);
    return ESP_OK;
}

static const ie_command_entry_t *lookup(const char *action)
{
    for (size_t i = 0; i < s_registry_len; i++) {
        if (strcmp(s_registry[i].action, action) == 0) {
            return &s_registry[i];
        }
    }
    return NULL;
}

// Lệnh đã gặp: chỉ một trường hợp được ack "ok" — đã chạy XONG và thành công.
// Mọi trường hợp khác là "error" để cloud/đối tác đối soát, KHÔNG gửi lại.
static void ack_previous(int64_t command_id, ie_command_state_t previous)
{
    switch (previous) {
    case IE_COMMAND_OK:
        ie_ws_client_send_command_ack(command_id, "ok", "duplicate", NULL);
        break;
    case IE_COMMAND_FAILED:
        ie_ws_client_send_command_ack(command_id, "error",
                                      "previous execution failed; reconcile manually", NULL);
        break;
    default: // RUNNING (bị ngắt) hoặc TOO_OLD (rơi khỏi cửa sổ)
        ie_ws_client_send_command_ack(command_id, "error",
                                      "execution uncertain; reconcile manually, do not replay",
                                      NULL);
        break;
    }
}

void ie_command_bus_dispatch(int64_t command_id, const char *action,
                             const char *params_json)
{
    if (command_id <= 0) {
        // Không có id thì không chống trùng được → không chạy lệnh có side-effect.
        ie_ws_client_send_command_ack(command_id, "error", "invalid commandId", NULL);
        return;
    }
    if (!action || action[0] == '\0') {
        ESP_LOGW(TAG, "lệnh động thiếu action (commandId=%lld)", (long long)command_id);
        ie_ws_client_send_command_ack(command_id, "error", "missing action", NULL);
        return;
    }
    if (!s_lock || !s_journal_ready) {
        ie_ws_client_send_command_ack(command_id, "error", "command bus not initialised", NULL);
        return;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);

    ie_command_state_t previous = ie_command_journal_lookup(&s_journal, command_id);
    if (previous != IE_COMMAND_UNSEEN) {
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "commandId=%lld action=%s đã gặp (state=%d) — không chạy lại",
                 (long long)command_id, action, (int)previous);
        ack_previous(command_id, previous);
        return;
    }

    const ie_command_entry_t *entry = lookup(action);
    if (!entry) {
        xSemaphoreGive(s_lock);
        ESP_LOGW(TAG, "commandId=%lld action=%s không có trong registry",
                 (long long)command_id, action);
        ie_ws_client_send_command_ack(command_id, "error", "unknown action", NULL);
        return;
    }

    size_t slot = 0;
    esp_err_t persist_err = journal_begin(command_id, &slot);
    if (persist_err != ESP_OK) {
        // Không ghi được RUNNING = không chạy. Thà từ chối còn hơn chạy mà không
        // nhớ là đã chạy (lần gửi lại sẽ nhả tiền lần hai).
        s_journal.entries[slot] = (ie_command_record_t){0};
        // Blob có thể đã ghi RUNNING (lỗi ở bước watermark): ghi lại bản đã gỡ để
        // sau reboot không báo nhầm "bị ngắt" cho lệnh chưa từng chạy.
        ie_config_store_save_blob(JOURNAL_KEY, &s_journal, sizeof(s_journal));
        xSemaphoreGive(s_lock);
        ESP_LOGE(TAG, "ghi journal lỗi: %s", esp_err_to_name(persist_err));
        ie_fault_set("command_journal_write_failed", "critical",
                     "Khong luu duoc nhat ky lenh - da chan thuc thi");
        ie_ws_client_send_command_ack(command_id, "error", "journal write failed; not executed", NULL);
        return;
    }

    cJSON *params = NULL;
    if (params_json && params_json[0] != '\0') {
        params = cJSON_Parse(params_json);
    }

    char result[160] = {0};
    char msg[96] = {0};
    ESP_LOGI(TAG, "thực thi commandId=%lld action=%s", (long long)command_id, action);
    esp_err_t err = entry->handler(params, result, sizeof(result), msg, sizeof(msg));
    cJSON_Delete(params);

    s_journal.entries[slot].state = (err == ESP_OK) ? IE_COMMAND_OK : IE_COMMAND_FAILED;
    persist_err = ie_config_store_save_blob(JOURNAL_KEY, &s_journal, sizeof(s_journal));
    if (persist_err != ESP_OK) {
        // Đã chạy nhưng không ghi được kết quả: sau reboot nó còn RUNNING = "không
        // chắc" — đúng sự thật. Báo lỗi thay vì ack ok.
        s_journal.entries[slot].state = IE_COMMAND_RUNNING;
        err = persist_err;
        snprintf(msg, sizeof(msg), "result not durable; reconcile manually");
        ie_fault_set("command_result_uncertain", "critical",
                     "Khong luu duoc ket qua lenh - can doi soat");
    }
    xSemaphoreGive(s_lock);

    if (err == ESP_OK) {
        ie_ws_client_send_command_ack(command_id, "ok", msg[0] ? msg : NULL,
                                      result[0] ? result : NULL);
    } else {
        if (msg[0] == '\0') {
            snprintf(msg, sizeof(msg), "%s", esp_err_to_name(err));
        }
        ie_ws_client_send_command_ack(command_id, "error", msg,
                                      result[0] ? result : NULL);
    }
}
