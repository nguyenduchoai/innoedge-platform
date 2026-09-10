// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// 09 — AI Agent: AI quyết định hành động, thiết bị thực thi.
//
// Phía thiết bị KHÔNG biết gì về AI. Nó chỉ đăng ký vài lệnh (đèn, motor, màn
// hình, cảm biến) như mọi example khác. Chuyện "ai gửi lệnh" — người gõ, hay
// LLM tự quyết qua tool calling — là việc của phía server (mock-cloud -ai).
//
// Đây là điểm dạy quan trọng nhất: kiến trúc đúng thì thiết bị không cần đổi
// gì khi bên trên thay người bằng AI.
//
//   người nói → LLM chọn tool → {"action":"led","params":{"on":true}} → đây
//   người nghe ← LLM đọc kết quả ← {"status":"ok","result":{"on":true}}  ← đây

#include "innoedge.h"

#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "ai-agent";

#define LED_GPIO   2 // đổi theo bo
#define MOTOR_GPIO 4 // relay / MOSFET điều khiển motor
#define MOTOR_MAX_SEC 60

static temperature_sensor_handle_t s_tsens;
static volatile int s_motor_remaining; // giây còn chạy; 0 = dừng

// ── led {on:bool} ───────────────────────────────────────────────────────────
static esp_err_t cmd_led(cJSON *params, char *result, size_t result_len,
                         char *msg, size_t msg_len)
{
    cJSON *on = params ? cJSON_GetObjectItem(params, "on") : NULL;
    if (!cJSON_IsBool(on)) {
        snprintf(msg, msg_len, "thieu tham so on");
        return ESP_ERR_INVALID_ARG;
    }
    bool level = cJSON_IsTrue(on);
    gpio_set_level(LED_GPIO, level);
    snprintf(result, result_len, "{\"on\":%s}", level ? "true" : "false");
    snprintf(msg, msg_len, "LED da %s", level ? "bat" : "tat");
    return ESP_OK;
}

// ── motor {seconds:int} — chạy nền, ack NGAY ────────────────────────────────
// Handler chạy trên task WebSocket: KHÔNG được ngồi chờ 30 giây trong đó.
// Đặt bộ đếm rồi trả về; task motor tự tắt khi hết giờ.
static void motor_task(void *arg)
{
    (void)arg;
    while (true) {
        int left = s_motor_remaining;
        gpio_set_level(MOTOR_GPIO, left > 0);
        if (left > 0) {
            s_motor_remaining = left - 1;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static esp_err_t cmd_motor(cJSON *params, char *result, size_t result_len,
                           char *msg, size_t msg_len)
{
    cJSON *sec = params ? cJSON_GetObjectItem(params, "seconds") : NULL;
    int n = cJSON_IsNumber(sec) ? sec->valueint : 0;
    if (n <= 0) {
        snprintf(msg, msg_len, "seconds phai > 0");
        return ESP_ERR_INVALID_ARG;
    }
    // Trần an toàn: LLM có thể hiểu nhầm "5 phút" thành 300 — không được để
    // một tham số sai biến thành motor chạy quá lâu. Cùng nguyên tắc với trần
    // nhả tiền ở example 06.
    if (n > MOTOR_MAX_SEC) {
        n = MOTOR_MAX_SEC;
    }
    s_motor_remaining = n;
    snprintf(result, result_len, "{\"running\":true,\"seconds\":%d}", n);
    snprintf(msg, msg_len, "motor chay %ds", n);
    return ESP_OK;
}

// ── show {text} — "màn hình" là log serial (bo không có LCD) ───────────────
static esp_err_t cmd_show(cJSON *params, char *result, size_t result_len,
                          char *msg, size_t msg_len)
{
    (void)result; (void)result_len;
    cJSON *text = params ? cJSON_GetObjectItem(params, "text") : NULL;
    if (!cJSON_IsString(text)) {
        snprintf(msg, msg_len, "thieu text");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "╔══ MÀN HÌNH ══╗  %s", text->valuestring);
    snprintf(msg, msg_len, "da hien");
    return ESP_OK;
}

// ── read_temperature — cảm biến nhiệt trong chip S3, không cần đấu dây ──────
// Cho AI "cảm nhận" được thế giới: tool trả dữ liệu thật, LLM nói lại đúng số.
static esp_err_t cmd_read_temperature(cJSON *params, char *result, size_t result_len,
                                      char *msg, size_t msg_len)
{
    (void)params;
    float c = 0;
    esp_err_t err = temperature_sensor_get_celsius(s_tsens, &c);
    if (err != ESP_OK) {
        snprintf(msg, msg_len, "cam bien loi: %s", esp_err_to_name(err));
        return err;
    }
    snprintf(result, result_len, "{\"celsius\":%.1f}", c);
    snprintf(msg, msg_len, "%.1f C", c);
    return ESP_OK;
}

static esp_err_t cmd_ping(cJSON *params, char *result, size_t result_len,
                          char *msg, size_t msg_len)
{
    (void)params; (void)msg; (void)msg_len;
    snprintf(result, result_len, "{\"pong\":true,\"uptime_s\":%lu}",
             (unsigned long)(xTaskGetTickCount() / configTICK_RATE_HZ));
    return ESP_OK;
}

void app_main(void)
{
    innoedge_config_t cfg = { .fw_version = "0.1.0" };
    ESP_ERROR_CHECK(innoedge_init(&cfg));

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << LED_GPIO) | (1ULL << MOTOR_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    temperature_sensor_config_t tcfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
    ESP_ERROR_CHECK(temperature_sensor_install(&tcfg, &s_tsens));
    ESP_ERROR_CHECK(temperature_sensor_enable(s_tsens));
    xTaskCreate(motor_task, "motor", 2048, NULL, 3, NULL);

    // Đây là toàn bộ "năng lực" thiết bị mà AI nhìn thấy. Mỗi dòng = một tool.
    ESP_ERROR_CHECK(innoedge_register_command("led", cmd_led));
    ESP_ERROR_CHECK(innoedge_register_command("motor", cmd_motor));
    ESP_ERROR_CHECK(innoedge_register_command("show", cmd_show));
    ESP_ERROR_CHECK(innoedge_register_command("read_temperature", cmd_read_temperature));
    ESP_ERROR_CHECK(innoedge_register_command("ping", cmd_ping));

    ESP_ERROR_CHECK(innoedge_start());
    ESP_LOGI(TAG, "sẵn sàng — chạy `mock-cloud -ai` rồi gõ: bật đèn / chạy motor 5 giây / nhiệt độ?");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
