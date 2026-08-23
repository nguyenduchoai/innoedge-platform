// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// 03 — Remote Command: cloud gửi lệnh xuống, máy làm và trả kết quả.
//
// Thêm một nghiệp vụ = viết 1 hàm + 1 dòng đăng ký. SDK lo phần khó:
//   nhận frame → chống trùng (bền qua reboot) → parse JSON → gọi hàm → ack.

#include "innoedge.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "command";

#define LED_GPIO 2 // đổi theo bo của bạn

// ── Lệnh "led": {"on": true} ────────────────────────────────────────────────
static esp_err_t cmd_led(cJSON *params, char *result, size_t result_len,
                         char *msg, size_t msg_len)
{
    cJSON *on = params ? cJSON_GetObjectItem(params, "on") : NULL;
    if (!cJSON_IsBool(on)) {
        snprintf(msg, msg_len, "thieu tham so 'on' (true/false)");
        return ESP_ERR_INVALID_ARG; // → cloud nhận ack status "error" kèm msg
    }
    bool level = cJSON_IsTrue(on);
    gpio_set_level(LED_GPIO, level);
    snprintf(result, result_len, "{\"on\":%s}", level ? "true" : "false");
    snprintf(msg, msg_len, "LED da %s", level ? "bat" : "tat");
    return ESP_OK;
}

// ── Lệnh "echo": trả lại tham số + thông tin máy ────────────────────────────
static esp_err_t cmd_echo(cJSON *params, char *result, size_t result_len,
                          char *msg, size_t msg_len)
{
    const cJSON *text = params ? cJSON_GetObjectItem(params, "text") : NULL;
    snprintf(result, result_len, "{\"echo\":\"%s\",\"device\":\"%s\"}",
             cJSON_IsString(text) ? text->valuestring : "", innoedge_device_id());
    snprintf(msg, msg_len, "ok");
    return ESP_OK;
}

// ── Lệnh "reboot" ───────────────────────────────────────────────────────────
// KHÔNG gọi esp_restart() thẳng trong handler: máy sẽ reboot TRƯỚC khi ack kịp
// đi, cloud tưởng lệnh thất bại và gửi lại. Trả ESP_OK rồi hẹn reboot.
static esp_err_t cmd_reboot(cJSON *params, char *result, size_t result_len,
                            char *msg, size_t msg_len)
{
    (void)params; (void)result; (void)result_len;
    snprintf(msg, msg_len, "se khoi dong lai");
    innoedge_reboot_after_ack(); // reboot sau ~800ms, đủ để ack bay đi
    return ESP_OK;
}

void app_main(void)
{
    innoedge_config_t cfg = { .fw_version = "0.1.0" };
    ESP_ERROR_CHECK(innoedge_init(&cfg));

    gpio_config_t io = { .pin_bit_mask = 1ULL << LED_GPIO, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&io));

    // Đăng ký nghiệp vụ. Chuỗi action phải là string literal (registry giữ con trỏ).
    ESP_ERROR_CHECK(innoedge_register_command("led", cmd_led));
    ESP_ERROR_CHECK(innoedge_register_command("echo", cmd_echo));
    ESP_ERROR_CHECK(innoedge_register_command("reboot", cmd_reboot));

    ESP_ERROR_CHECK(innoedge_start());
    ESP_LOGI(TAG, "sẵn sàng nhận lệnh: led / echo / reboot");

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
