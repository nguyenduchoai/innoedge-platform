// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// 04 — Device Config: máy chạy đúng cấu hình chủ máy đặt trên app, KỂ CẢ OFFLINE.
//
// Cloud giữ cấu hình vận hành (combo, giá, tham số). SDK tải về, cache vào NVS.
// Mất mạng → đọc cache, máy vẫn bán đúng giá.

#include "innoedge.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "config";

#define CFG_BUF 4352 // trần cấu hình (4096) + lề

// Đọc cache NVS và in ra combo/giá. Chạy được cả khi chưa từng có mạng lần nào
// (khi đó trả ESP_ERR_NOT_FOUND — máy nên dùng giá mặc định compile-time).
static void dump_config(void)
{
    char *json = malloc(CFG_BUF); // 4KB — đừng để trên stack task
    if (!json) {
        return;
    }
    int version = 0;
    esp_err_t err = innoedge_config_json(json, CFG_BUF, &version);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "chưa có cấu hình (%s) — dùng mặc định compile-time",
                 esp_err_to_name(err));
        free(json);
        return;
    }

    cJSON *root = cJSON_Parse(json);
    free(json);
    if (!root) {
        ESP_LOGE(TAG, "cấu hình cache hỏng JSON");
        return;
    }

    ESP_LOGI(TAG, "── cấu hình version=%d ──", version);
    cJSON *pricing = cJSON_GetObjectItem(root, "pricing");
    cJSON *rate = pricing ? cJSON_GetObjectItem(pricing, "coinPerBillVnd") : NULL;
    if (cJSON_IsNumber(rate)) {
        ESP_LOGI(TAG, "  đơn giá: %d đ / xu", rate->valueint);
    }
    cJSON *combos = cJSON_GetObjectItem(root, "combos");
    cJSON *combo = NULL;
    cJSON_ArrayForEach(combo, combos) {
        cJSON *name = cJSON_GetObjectItem(combo, "name");
        cJSON *price = cJSON_GetObjectItem(combo, "priceVnd");
        ESP_LOGI(TAG, "  combo: %s — %d đ",
                 cJSON_IsString(name) ? name->valuestring : "?",
                 cJSON_IsNumber(price) ? price->valueint : 0);
    }
    cJSON_Delete(root);
}

// SDK gọi khi tải xong lúc boot (kể cả khi lỗi mạng — lúc đó version là của cache).
static void on_config(int version)
{
    ESP_LOGI(TAG, "cấu hình sẵn sàng, version=%d", version);
    dump_config();
}

// Chủ máy đổi cấu hình trên app → cloud gửi lệnh này → máy tải lại NGAY,
// không cần reboot.
static esp_err_t cmd_config_updated(cJSON *params, char *result, size_t result_len,
                                    char *msg, size_t msg_len)
{
    (void)params; (void)msg; (void)msg_len;
    // Lỗi mạng vẫn ack "ok": cache cũ vẫn dùng được, không nên kẹt hàng đợi lệnh
    // của cloud vì một lần fetch hỏng.
    esp_err_t err = innoedge_config_reload();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "tải lại lỗi (%s) — giữ cache cũ", esp_err_to_name(err));
    }
    int version = 0;
    innoedge_config_json(NULL, 0, &version);
    snprintf(result, result_len, "{\"version\":%d}", version);
    return ESP_OK;
}

void app_main(void)
{
    static const innoedge_events_t events = { .on_config = on_config };
    innoedge_config_t cfg = { .events = &events };

    ESP_ERROR_CHECK(innoedge_init(&cfg));
    ESP_ERROR_CHECK(innoedge_register_command("config_updated", cmd_config_updated));

    // Đọc cache TRƯỚC khi lên mạng: máy phải phục vụ được ngay từ giây đầu,
    // không chờ WiFi.
    dump_config();
    ESP_ERROR_CHECK(innoedge_start());

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}
