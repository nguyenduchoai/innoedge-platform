// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// 08 — Car Wash: một phiên nhiều thiết bị, chạy bằng NGÂN SÁCH THỜI GIAN.
//
// Combo = ngân sách giây cho từng thiết bị (nước / bọt / khí / hút bụi).
// Khách bấm chức năng nào thì relay đó chạy và trừ ngân sách của riêng nó.
// Cùng lúc chỉ MỘT relay bật (bảo vệ bơm). Hết ngân sách → khoá chức năng đó.
//
// Đây là example "sản phẩm hoàn chỉnh": lệnh từ cloud + cấu hình động +
// điều khiển nhiều relay + cảnh báo phần cứng.

#include "innoedge.h"

#include "gtek_config_client.h"
#include "innoedge_relay_control.h"
#include "innoedge_wash_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "carwash";

// "start_wash": mở phiên rửa. Chấp nhận hai dạng params:
//   {"combo":"A"}                                  → tra ngân sách từ cache config
//   {"steps":[{"device":"water","seconds":120}]}   → ngân sách gửi thẳng
// Tuỳ chọn {"maxSec":600} đặt trần cả phiên.
static esp_err_t cmd_start_wash(cJSON *params, char *result, size_t result_len,
                                char *msg, size_t msg_len)
{
    int budgets[INNOEDGE_WASH_DEVICE_COUNT] = {0};
    const char *combo_id = "";

    cJSON *combo = params ? cJSON_GetObjectItem(params, "combo") : NULL;
    if (!combo) {
        combo = params ? cJSON_GetObjectItem(params, "comboId") : NULL;
    }
    cJSON *steps = params ? cJSON_GetObjectItem(params, "steps") : NULL;

    if (cJSON_IsString(combo)) {
        combo_id = combo->valuestring;
        // Đọc từ CACHE NVS → mất mạng vẫn mở được phiên đúng combo.
        if (gtek_config_lookup_combo(combo_id, budgets) != ESP_OK) {
            snprintf(msg, msg_len, "khong co combo %s trong cau hinh", combo_id);
            return ESP_ERR_NOT_FOUND;
        }
    } else if (cJSON_IsArray(steps)) {
        cJSON *step = NULL;
        cJSON_ArrayForEach(step, steps) {
            cJSON *dev = cJSON_GetObjectItem(step, "device");
            cJSON *sec = cJSON_GetObjectItem(step, "seconds");
            int idx = cJSON_IsString(dev) ? gtek_config_device_index(dev->valuestring) : -1;
            if (idx >= 0 && cJSON_IsNumber(sec) && sec->valueint > 0) {
                budgets[idx] += sec->valueint;
            }
        }
    } else {
        snprintf(msg, msg_len, "thieu combo hoac steps");
        return ESP_ERR_INVALID_ARG;
    }

    int total = 0;
    for (int i = 0; i < INNOEDGE_WASH_DEVICE_COUNT; i++) {
        total += budgets[i];
    }
    if (total <= 0) {
        snprintf(msg, msg_len, "combo rong (tong ngan sach = 0)");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *max_sec = params ? cJSON_GetObjectItem(params, "maxSec") : NULL;
    esp_err_t err = innoedge_wash_start(budgets, cJSON_IsNumber(max_sec) ? max_sec->valueint : 0);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "mở phiên: nước=%ds bọt=%ds khí=%ds hút=%ds",
             budgets[INNOEDGE_WASH_WATER], budgets[INNOEDGE_WASH_FOAM],
             budgets[INNOEDGE_WASH_AIR], budgets[INNOEDGE_WASH_VACUUM]);
    snprintf(result, result_len, "{\"started\":true,\"combo\":\"%s\",\"totalSec\":%d}",
             combo_id, total);
    snprintf(msg, msg_len, "da mo phien rua");
    return ESP_OK;
}

// "stop_wash": dừng khẩn cấp — tắt hết relay ngay.
static esp_err_t cmd_stop_wash(cJSON *params, char *result, size_t result_len,
                               char *msg, size_t msg_len)
{
    (void)params; (void)result; (void)result_len;
    innoedge_wash_end();
    snprintf(msg, msg_len, "da dung phien");
    return ESP_OK;
}

// Khách bấm nút chức năng trên máy → gọi hàm này. Máy thật thì nối vào 4 nút
// vật lý hoặc màn cảm ứng.
static esp_err_t customer_pressed(innoedge_wash_device_t device)
{
    return innoedge_wash_activate(device);
}

// Theo dõi phiên: log thời gian còn lại, và báo cảnh báo khi phiên kết thúc.
static void session_monitor_task(void *arg)
{
    (void)arg;
    bool was_active = false;
    while (true) {
        innoedge_wash_status_t st = {0};
        bool active = innoedge_wash_status(&st);
        if (active) {
            ESP_LOGI(TAG, "còn: nước=%ds bọt=%ds khí=%ds hút=%ds · phiên=%ds",
                     st.remaining_sec[INNOEDGE_WASH_WATER], st.remaining_sec[INNOEDGE_WASH_FOAM],
                     st.remaining_sec[INNOEDGE_WASH_AIR], st.remaining_sec[INNOEDGE_WASH_VACUUM],
                     st.session_remaining_sec);
        } else if (was_active) {
            ESP_LOGI(TAG, "phiên kết thúc — mọi relay đã tắt");
        }
        was_active = active;
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void app_main(void)
{
    innoedge_config_t cfg = { .fw_version = "0.1.3" };
    ESP_ERROR_CHECK(innoedge_init(&cfg));

    ESP_ERROR_CHECK(innoedge_relay_control_init());
    ESP_ERROR_CHECK(innoedge_wash_control_init());
    ESP_ERROR_CHECK(innoedge_register_command("start_wash", cmd_start_wash));
    ESP_ERROR_CHECK(innoedge_register_command("stop_wash", cmd_stop_wash));

    ESP_ERROR_CHECK(innoedge_start());
    xTaskCreate(session_monitor_task, "wash_mon", 3072, NULL, 3, NULL);

    vTaskDelay(pdMS_TO_TICKS(20000));
    if (innoedge_wash_status(NULL)) {
        ESP_LOGI(TAG, "demo: khách bấm XỊT NƯỚC → %s",
                 esp_err_to_name(customer_pressed(INNOEDGE_WASH_WATER)));
    } else {
        ESP_LOGW(TAG, "chưa có phiên — gửi lệnh start_wash từ cloud trước");
    }

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
