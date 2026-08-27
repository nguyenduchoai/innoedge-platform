// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "gtek_command_bus.h"

#include "gtek_config_store.h"
#include "gtek_fault.h"
#include "gtek_ws_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "gtek.cmdbus";

// ── REGISTRY (runtime) ──────────────────────────────────────────────────────
// SDK không biết trước nghiệp vụ nào — application đăng ký handler lúc khởi
// động bằng gtek_command_bus_register(). Bảng tĩnh cũ (dispense/start_wash...)
// đã chuyển sang component gtek_app_commands (lớp SẢN PHẨM), giữ command_bus
// thuần hạ tầng: dispatch + dedupe + ack.
#ifndef GTEK_CMD_REGISTRY_MAX
#define GTEK_CMD_REGISTRY_MAX 24
#endif

static gtek_command_entry_t s_registry[GTEK_CMD_REGISTRY_MAX];
static size_t s_registry_len;

// ── DEDUPE ──────────────────────────────────────────────────────────────────
// Vòng tròn trong RAM cho các commandId vừa xử lý + commandId cuối lưu NVS để
// sống qua reboot. Gặp lại id đã xử lý → KHÔNG gọi handler, chỉ ack lại.
#define GTEK_CMD_DEDUPE_RING 16

static int64_t s_seen[GTEK_CMD_DEDUPE_RING];
static size_t s_seen_pos;
static int64_t s_last_persisted;
static SemaphoreHandle_t s_lock;

static bool dedupe_seen(int64_t command_id)
{
    if (command_id <= 0) {
        return false; // commandId không hợp lệ → không coi là trùng
    }
    // High-watermark: commandId là PK auto-increment (tăng đơn điệu) + server
    // redeliver theo id ASC → mọi id <= watermark = ĐÃ xử lý. Bền qua reboot chỉ
    // bằng 1 int NVS (s_last_persisted), KHÔNG mất cả cửa sổ như ring RAM trước
    // đây (nguyên nhân nhả tiền 2 lần sau reboot). Lệnh MỚI luôn id > watermark
    // nên không bao giờ bị bỏ nhầm.
    if (command_id <= s_last_persisted) {
        return true;
    }
    for (size_t i = 0; i < GTEK_CMD_DEDUPE_RING; i++) {
        if (s_seen[i] == command_id) {
            return true;
        }
    }
    return false;
}

static void dedupe_remember(int64_t command_id)
{
    if (command_id <= 0) {
        return;
    }
    s_seen[s_seen_pos] = command_id;
    s_seen_pos = (s_seen_pos + 1) % GTEK_CMD_DEDUPE_RING;
    if (command_id <= s_last_persisted) {
        return; // CHỈ nâng watermark, không hạ (giữ id cao nhất đã xử lý)
    }
    s_last_persisted = command_id;
    // Lưu NVS để dedupe sống qua reboot (quan trọng với dispense/nhả tiền).
    esp_err_t err = gtek_config_store_save_last_command_id(command_id);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "lưu last_command_id thất bại: %s", esp_err_to_name(err));
        // Dedupe không bền qua reboot → nguy cơ NHẢ TIỀN 2 LẦN nếu mất điện giữa
        // chừng rồi server gửi lại lệnh. Báo để kỹ thuật kiểm tra NVS/flash.
        gtek_fault_set("dedupe_persist_fail", "critical",
                       "Khong luu duoc chong-trung lenh - nguy co nha tien 2 lan");
    }
}

esp_err_t gtek_command_bus_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    // Registry về rỗng: init() là "bắt đầu lại từ đầu" — handler phải đăng ký
    // SAU init (xem gtek_command_bus.h).
    memset(s_registry, 0, sizeof(s_registry));
    s_registry_len = 0;
    s_last_persisted = gtek_config_store_last_command_id();
    s_seen_pos = 0;
    memset(s_seen, 0, sizeof(s_seen));
    ESP_LOGI(TAG, "init: last_command_id=%lld (registry %u action)",
             (long long)s_last_persisted, (unsigned)s_registry_len);
    return ESP_OK;
}

// Reboot trễ: handler "reboot" của application yêu cầu, dispatcher vẫn kịp gửi
// ack rồi máy mới khởi động lại.
static void deferred_reboot_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
}

void gtek_command_bus_request_reboot(void)
{
    xTaskCreate(deferred_reboot_task, "gtek_reboot", 2048, NULL, 5, NULL);
}

esp_err_t gtek_command_bus_register(const char *action, gtek_command_handler_fn handler)
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
    if (s_registry_len >= GTEK_CMD_REGISTRY_MAX) {
        ESP_LOGE(TAG, "registry đầy (%d) — không nhận action=%s",
                 GTEK_CMD_REGISTRY_MAX, action);
        return ESP_ERR_NO_MEM;
    }
    s_registry[s_registry_len].action = action; // chuỗi phải sống lâu (literal)
    s_registry[s_registry_len].handler = handler;
    s_registry_len++;
    ESP_LOGI(TAG, "đăng ký action=%s (%u/%d)", action,
             (unsigned)s_registry_len, GTEK_CMD_REGISTRY_MAX);
    return ESP_OK;
}

static const gtek_command_entry_t *lookup(const char *action)
{
    for (size_t i = 0; i < s_registry_len; i++) {
        if (strcmp(s_registry[i].action, action) == 0) {
            return &s_registry[i];
        }
    }
    return NULL;
}

void gtek_command_bus_dispatch(int64_t command_id, const char *action,
                               const char *params_json)
{
    if (!action || action[0] == '\0') {
        ESP_LOGW(TAG, "lệnh động thiếu action (commandId=%lld)", (long long)command_id);
        gtek_ws_client_send_command_ack(command_id, "error", "missing action", NULL);
        return;
    }

    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }

    if (dedupe_seen(command_id)) {
        if (s_lock) {
            xSemaphoreGive(s_lock);
        }
        ESP_LOGW(TAG, "commandId=%lld action=%s TRÙNG — chỉ ack lại",
                 (long long)command_id, action);
        gtek_ws_client_send_command_ack(command_id, "ok", "duplicate", NULL);
        return;
    }

    const gtek_command_entry_t *entry = lookup(action);
    if (!entry) {
        if (s_lock) {
            xSemaphoreGive(s_lock);
        }
        ESP_LOGW(TAG, "commandId=%lld action=%s không có trong registry",
                 (long long)command_id, action);
        gtek_ws_client_send_command_ack(command_id, "error", "unknown action", NULL);
        return;
    }

    // Đánh dấu đã xử lý TRƯỚC khi chạy handler: nếu handler có side-effect (nhả
    // tiền) rồi mất điện, lần gửi lại sau reboot sẽ thấy trùng và không nhả lại.
    dedupe_remember(command_id);
    if (s_lock) {
        xSemaphoreGive(s_lock);
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

    if (err == ESP_OK) {
        gtek_ws_client_send_command_ack(command_id, "ok", msg[0] ? msg : NULL,
                                        result[0] ? result : NULL);
    } else {
        if (msg[0] == '\0') {
            snprintf(msg, sizeof(msg), "%s", esp_err_to_name(err));
        }
        gtek_ws_client_send_command_ack(command_id, "error", msg,
                                        result[0] ? result : NULL);
    }
}
