// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// 06 — Coin & Relay: máy coin-op thật.
//
// Đầu vào : đầu đọc xu/bill nhả xung → đếm → báo tiền lên cloud.
// Đầu ra  : cloud gửi lệnh "dispense" → nhả relay đúng số xung.
//
// Đây là ví dụ ĐẦY ĐỦ của một máy vận hành bằng xu — hai chiều tiền.

#include "innoedge.h"

#include "innoedge_pulse_input.h"
#include "innoedge_relay_control.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>

static const char *TAG = "coinop";

#ifndef CONFIG_INNOEDGE_BILL_VND_PER_PULSE
#ifdef CONFIG_INNOEDGE_BILL_VND_PER_PULSE
#define CONFIG_INNOEDGE_BILL_VND_PER_PULSE CONFIG_INNOEDGE_BILL_VND_PER_PULSE
#else
#define CONFIG_INNOEDGE_BILL_VND_PER_PULSE 10000
#endif
#endif

#ifndef CONFIG_INNOEDGE_DISPENSE_VND_PER_PULSE
#ifdef CONFIG_INNOEDGE_DISPENSE_VND_PER_PULSE
#define CONFIG_INNOEDGE_DISPENSE_VND_PER_PULSE CONFIG_INNOEDGE_DISPENSE_VND_PER_PULSE
#else
#define CONFIG_INNOEDGE_DISPENSE_VND_PER_PULSE 0
#endif
#endif

#ifndef CONFIG_INNOEDGE_DISPENSE_MAX_PULSES
#ifdef CONFIG_INNOEDGE_DISPENSE_MAX_PULSES
#define CONFIG_INNOEDGE_DISPENSE_MAX_PULSES CONFIG_INNOEDGE_DISPENSE_MAX_PULSES
#else
#define CONFIG_INNOEDGE_DISPENSE_MAX_PULSES 200
#endif
#endif

#ifndef CONFIG_INNOEDGE_RELAY_PULSE_GAP_MS
#ifdef CONFIG_INNOEDGE_RELAY_PULSE_GAP_MS
#define CONFIG_INNOEDGE_RELAY_PULSE_GAP_MS CONFIG_INNOEDGE_RELAY_PULSE_GAP_MS
#else
#define CONFIG_INNOEDGE_RELAY_PULSE_GAP_MS 120
#endif
#endif

#ifndef CONFIG_INNOEDGE_COIN_PULSE_GPIO
#ifdef CONFIG_INNOEDGE_COIN_PULSE_GPIO
#define CONFIG_INNOEDGE_COIN_PULSE_GPIO CONFIG_INNOEDGE_COIN_PULSE_GPIO
#else
#define CONFIG_INNOEDGE_COIN_PULSE_GPIO -1
#endif
#endif

#ifndef CONFIG_INNOEDGE_RELAY_GPIO
#ifdef CONFIG_INNOEDGE_RELAY_GPIO
#define CONFIG_INNOEDGE_RELAY_GPIO CONFIG_INNOEDGE_RELAY_GPIO
#else
#define CONFIG_INNOEDGE_RELAY_GPIO -1
#endif
#endif

static void on_pulse(innoedge_pulse_kind_t kind, uint32_t pulses, int64_t amount_vnd,
                     void *ctx)
{
    (void)ctx;
    if (kind == INNOEDGE_PULSE_COIN && amount_vnd <= 0) {
        ESP_LOGI(TAG, "khách bỏ %u xu", (unsigned)pulses);
        innoedge_publish_payment(INNOEDGE_PAY_COIN, (int)pulses, 0);
    } else {
        if (amount_vnd <= 0) {
            amount_vnd = (int64_t)pulses * CONFIG_INNOEDGE_BILL_VND_PER_PULSE;
        }
        ESP_LOGI(TAG, "khách bỏ %lld đ tiền mặt", (long long)amount_vnd);
        innoedge_publish_payment(INNOEDGE_PAY_CASH, 0, amount_vnd);
    }
}

static esp_err_t cmd_dispense(cJSON *params, char *result, size_t result_len,
                              char *msg, size_t msg_len)
{
    cJSON *amount = params ? cJSON_GetObjectItem(params, "amountVnd") : NULL;
    if (!cJSON_IsNumber(amount) || amount->valuedouble <= 0) {
        snprintf(msg, msg_len, "thieu amountVnd");
        return ESP_ERR_INVALID_ARG;
    }

    int per_pulse = CONFIG_INNOEDGE_DISPENSE_VND_PER_PULSE > 0
                        ? CONFIG_INNOEDGE_DISPENSE_VND_PER_PULSE
                        : CONFIG_INNOEDGE_BILL_VND_PER_PULSE;
    int pulses = (int)(amount->valuedouble / per_pulse);
    if (pulses <= 0) {
        snprintf(msg, msg_len, "so tien nho hon 1 xung");
        return ESP_ERR_INVALID_ARG;
    }
    if (pulses > CONFIG_INNOEDGE_DISPENSE_MAX_PULSES) {
        pulses = CONFIG_INNOEDGE_DISPENSE_MAX_PULSES;
        ESP_LOGW(TAG, "vượt trần — cắt còn %d xung", pulses);
    }

    for (int i = 0; i < pulses; i++) {
        innoedge_relay_control_pulse(0);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_INNOEDGE_RELAY_PULSE_GAP_MS));
    }

    snprintf(result, result_len, "{\"pulses\":%d}", pulses);
    snprintf(msg, msg_len, "da nha %d xung", pulses);
    return ESP_OK;
}

void app_main(void)
{
    innoedge_config_t cfg = { .fw_version = "0.1.3" };
    ESP_ERROR_CHECK(innoedge_init(&cfg));

    ESP_ERROR_CHECK(innoedge_relay_control_init());
    ESP_ERROR_CHECK(innoedge_pulse_input_init(on_pulse, NULL));
    ESP_ERROR_CHECK(innoedge_register_command("dispense", cmd_dispense));

    ESP_ERROR_CHECK(innoedge_start());
    ESP_LOGI(TAG, "máy coin-op sẵn sàng (xu vào GPIO%d, relay ra GPIO%d)",
             CONFIG_INNOEDGE_COIN_PULSE_GPIO, CONFIG_INNOEDGE_RELAY_GPIO);

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
}
