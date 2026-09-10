// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// 10 — Edu Tutor: thiết bị học tiếng Anh cho bé, AI làm gia sư.
//
// Mô phỏng hợp đồng thiết bị của VIMATE Edu (sản phẩm thật: ESP32-S3 + LCD +
// mic/loa) trên một devkit trắng: "màn hình" là serial log, "giọng nói" là
// dòng chữ, "nút chạm" là nút BOOT. Kiến trúc và khung tin thì y hệt sản phẩm.
//
// Hai chiều:
//   AI → máy  (lệnh)     : say · show_card · quiz · show_reward
//   máy → AI  (sự kiện)  : quiz_answer{index} · wake
//
// Bé trả lời quiz bằng nút BOOT: bấm 1 lần = A, 2 lần = B, 3 lần = C (trong 1,5s).
// Giữ BOOT 2 giây = "Hi Lily" (gọi gia sư).

#include "innoedge.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "edu";

#define BOOT_BUTTON_GPIO 0
#define MULTI_PRESS_WINDOW_MS 1500
#define WAKE_HOLD_MS 2000

// Quiz đang mở: số lựa chọn (0 = không có quiz). Nút chỉ có nghĩa khi có quiz.
static volatile int s_quiz_options;

// ── Lệnh từ AI ──────────────────────────────────────────────────────────────

static esp_err_t cmd_say(cJSON *params, char *result, size_t result_len,
                         char *msg, size_t msg_len)
{
    (void)result; (void)result_len;
    cJSON *text = params ? cJSON_GetObjectItem(params, "text") : NULL;
    if (!cJSON_IsString(text)) {
        snprintf(msg, msg_len, "thieu text");
        return ESP_ERR_INVALID_ARG;
    }
    // Sản phẩm thật: TTS → loa. Devkit: in ra.
    ESP_LOGI(TAG, "🔊 Lily: %s", text->valuestring);
    snprintf(msg, msg_len, "da noi");
    return ESP_OK;
}

static esp_err_t cmd_show_card(cJSON *params, char *result, size_t result_len,
                               char *msg, size_t msg_len)
{
    (void)result; (void)result_len;
    cJSON *word = params ? cJSON_GetObjectItem(params, "word") : NULL;
    cJSON *hint = params ? cJSON_GetObjectItem(params, "hint") : NULL;
    if (!cJSON_IsString(word)) {
        snprintf(msg, msg_len, "thieu word");
        return ESP_ERR_INVALID_ARG;
    }
    s_quiz_options = 0; // thẻ mới = đóng quiz cũ
    ESP_LOGI(TAG, "┌──────────── THẺ HỌC ────────────┐");
    ESP_LOGI(TAG, "│  %-30s │", word->valuestring);
    ESP_LOGI(TAG, "│  %-30s │", cJSON_IsString(hint) ? hint->valuestring : "");
    ESP_LOGI(TAG, "└─────────────────────────────────┘");
    snprintf(msg, msg_len, "da hien the");
    return ESP_OK;
}

static esp_err_t cmd_quiz(cJSON *params, char *result, size_t result_len,
                          char *msg, size_t msg_len)
{
    cJSON *q = params ? cJSON_GetObjectItem(params, "question") : NULL;
    cJSON *opts = params ? cJSON_GetObjectItem(params, "options") : NULL;
    int n = cJSON_IsArray(opts) ? cJSON_GetArraySize(opts) : 0;
    if (!cJSON_IsString(q) || n < 2 || n > 4) {
        snprintf(msg, msg_len, "can question + 2..4 options");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "❓ %s", q->valuestring);
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_GetArrayItem(opts, i);
        ESP_LOGI(TAG, "   [%c] %s   (bấm %d lần)", 'A' + i,
                 cJSON_IsString(o) ? o->valuestring : "?", i + 1);
    }
    s_quiz_options = n; // mở cửa cho nút
    snprintf(result, result_len, "{\"shown\":true,\"options\":%d}", n);
    snprintf(msg, msg_len, "cho be tra loi");
    return ESP_OK;
}

static esp_err_t cmd_show_reward(cJSON *params, char *result, size_t result_len,
                                 char *msg, size_t msg_len)
{
    (void)result; (void)result_len;
    cJSON *stars = params ? cJSON_GetObjectItem(params, "stars") : NULL;
    int n = cJSON_IsNumber(stars) ? stars->valueint : 1;
    if (n < 1) n = 1;
    if (n > 3) n = 3; // trần: LLM không phát được 100 sao
    char line[16] = {0};
    for (int i = 0; i < n; i++) strcat(line, "⭐");
    ESP_LOGI(TAG, "🎉 %s", line);
    snprintf(msg, msg_len, "%d sao", n);
    return ESP_OK;
}

// ── Nút bấm → sự kiện lên AI ────────────────────────────────────────────────
// Đếm số lần bấm trong cửa sổ 1,5s → index đáp án. Giữ 2s → wake.
// Chỉ gửi quiz_answer khi đang có quiz — bấm lung tung không làm AI rối.

static void button_task(void *arg)
{
    (void)arg;
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    int presses = 0;
    int64_t first_press_us = 0, down_since_us = 0;
    bool was_down = false, wake_sent = false;

    while (true) {
        bool down = gpio_get_level(BOOT_BUTTON_GPIO) == 0;
        int64_t now = esp_timer_get_time();

        if (down && !was_down) {
            down_since_us = now;
            wake_sent = false;
        }
        if (down && !wake_sent && now - down_since_us > WAKE_HOLD_MS * 1000) {
            wake_sent = true;
            presses = 0;
            ESP_LOGI(TAG, "👋 bé gọi Lily");
            innoedge_publish_event("wake", NULL);
        }
        if (!down && was_down && !wake_sent) {
            if (presses == 0) first_press_us = now;
            presses++;
        }
        if (presses > 0 && now - first_press_us > MULTI_PRESS_WINDOW_MS * 1000) {
            int index = presses - 1;
            presses = 0;
            if (s_quiz_options == 0) {
                ESP_LOGW(TAG, "bấm nút nhưng chưa có câu hỏi — bỏ qua");
            } else if (index >= s_quiz_options) {
                ESP_LOGW(TAG, "bấm %d lần nhưng chỉ có %d lựa chọn", index + 1, s_quiz_options);
            } else {
                char data[32];
                snprintf(data, sizeof(data), "{\"index\":%d}", index);
                ESP_LOGI(TAG, "✋ bé chọn [%c]", 'A' + index);
                s_quiz_options = 0; // một câu, một câu trả lời
                // Vào hàng đợi bền: rớt WiFi đúng lúc bé bấm cũng không mất câu trả lời.
                innoedge_publish_event("quiz_answer", data);
            }
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void app_main(void)
{
    innoedge_config_t cfg = { .fw_version = "0.1.0" };
    ESP_ERROR_CHECK(innoedge_init(&cfg));

    // Năng lực thiết bị = 4 lệnh. Gia sư là ai (LLM nào, bài nào) không phải
    // việc của firmware — cùng bài học với example 09.
    ESP_ERROR_CHECK(innoedge_register_command("say", cmd_say));
    ESP_ERROR_CHECK(innoedge_register_command("show_card", cmd_show_card));
    ESP_ERROR_CHECK(innoedge_register_command("quiz", cmd_quiz));
    ESP_ERROR_CHECK(innoedge_register_command("show_reward", cmd_show_reward));

    ESP_ERROR_CHECK(innoedge_start());
    xTaskCreate(button_task, "button", 3072, NULL, 4, NULL);
    ESP_LOGI(TAG, "sẵn sàng — chạy `mock-cloud -ai -persona edu`, gõ \"bắt đầu bài học\"");
    ESP_LOGI(TAG, "trả lời: bấm BOOT 1/2/3 lần = A/B/C · giữ 2s = gọi Lily");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
