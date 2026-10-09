// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "innoedge_pulse_input.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ie_fault.h"
#include "ie_ws_client.h"
#include "sdkconfig.h"

#ifndef CONFIG_INNOEDGE_COIN_PULSE_GPIO
#ifdef CONFIG_INNOEDGE_COIN_PULSE_GPIO
#define CONFIG_INNOEDGE_COIN_PULSE_GPIO CONFIG_INNOEDGE_COIN_PULSE_GPIO
#else
#define CONFIG_INNOEDGE_COIN_PULSE_GPIO -1
#endif
#endif

#ifndef CONFIG_INNOEDGE_BILL_PULSE_GPIO
#ifdef CONFIG_INNOEDGE_BILL_PULSE_GPIO
#define CONFIG_INNOEDGE_BILL_PULSE_GPIO CONFIG_INNOEDGE_BILL_PULSE_GPIO
#else
#define CONFIG_INNOEDGE_BILL_PULSE_GPIO -1
#endif
#endif

#ifndef CONFIG_INNOEDGE_COIN_PULSE_PCA9554_P0
#ifdef CONFIG_INNOEDGE_COIN_PULSE_PCA9554_P0
#define CONFIG_INNOEDGE_COIN_PULSE_PCA9554_P0 CONFIG_INNOEDGE_COIN_PULSE_PCA9554_P0
#else
#define CONFIG_INNOEDGE_COIN_PULSE_PCA9554_P0 0
#endif
#endif

#ifndef CONFIG_INNOEDGE_PULSE_MIN_MS
#ifdef CONFIG_INNOEDGE_PULSE_MIN_MS
#define CONFIG_INNOEDGE_PULSE_MIN_MS CONFIG_INNOEDGE_PULSE_MIN_MS
#else
#define CONFIG_INNOEDGE_PULSE_MIN_MS 35
#endif
#endif

#ifndef CONFIG_INNOEDGE_PULSE_GAP_MS
#ifdef CONFIG_INNOEDGE_PULSE_GAP_MS
#define CONFIG_INNOEDGE_PULSE_GAP_MS CONFIG_INNOEDGE_PULSE_GAP_MS
#else
#define CONFIG_INNOEDGE_PULSE_GAP_MS 300
#endif
#endif

#ifndef CONFIG_INNOEDGE_BILL_VND_PER_PULSE
#ifdef CONFIG_INNOEDGE_BILL_VND_PER_PULSE
#define CONFIG_INNOEDGE_BILL_VND_PER_PULSE CONFIG_INNOEDGE_BILL_VND_PER_PULSE
#else
#define CONFIG_INNOEDGE_BILL_VND_PER_PULSE 10000
#endif
#endif

static const char *TAG = "innoedge.pulse";

typedef struct {
    volatile uint32_t pulses;
    volatile TickType_t last_tick;
} pulse_counter_t;

static pulse_counter_t s_coin;
static pulse_counter_t s_bill;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static innoedge_pulse_input_cb_t s_cb;
static void *s_cb_ctx;
static innoedge_pulse_expander_read_fn s_expander_read;

void innoedge_pulse_input_set_expander_reader(innoedge_pulse_expander_read_fn fn)
{
    s_expander_read = fn;
}

static void record_pulse(innoedge_pulse_kind_t kind, TickType_t now, bool from_isr)
{
    TickType_t min_ticks = pdMS_TO_TICKS(CONFIG_INNOEDGE_PULSE_MIN_MS);

    if (from_isr) {
        portENTER_CRITICAL_ISR(&s_mux);
    } else {
        portENTER_CRITICAL(&s_mux);
    }
    pulse_counter_t *counter = kind == INNOEDGE_PULSE_COIN ? &s_coin : &s_bill;
    if (counter->last_tick == 0 || now - counter->last_tick >= min_ticks) {
        counter->pulses++;
        counter->last_tick = now;
    }
    if (from_isr) {
        portEXIT_CRITICAL_ISR(&s_mux);
    } else {
        portEXIT_CRITICAL(&s_mux);
    }
}

static void IRAM_ATTR pulse_isr(void *arg)
{
    innoedge_pulse_kind_t kind = (innoedge_pulse_kind_t)(intptr_t)arg;
    record_pulse(kind, xTaskGetTickCountFromISR(), true);
}

static void pulse_task(void *pv)
{
    (void)pv;
    const TickType_t gap_ticks = pdMS_TO_TICKS(CONFIG_INNOEDGE_PULSE_GAP_MS);
    bool prev_pca = false;
    TickType_t pca_stuck_start = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(10));
        TickType_t now = xTaskGetTickCount();

#if CONFIG_INNOEDGE_COIN_PULSE_PCA9554_P0
        if (s_expander_read) {
            bool active = false;
            esp_err_t err = s_expander_read(&active);
            if (err != ESP_OK) {
                ie_fault_set("coin_reader_fault", "critical",
                               "Khong doc duoc trang thai dau doc xu (expander loi)");
            } else {
                ie_fault_clear("coin_reader_fault");
                if (active && !prev_pca) {
                    record_pulse(INNOEDGE_PULSE_COIN, now, false);
                }
                if (active) {
                    if (pca_stuck_start == 0) pca_stuck_start = now;
                    else if (now - pca_stuck_start > pdMS_TO_TICKS(2000)) {
                        ie_fault_set("coin_acceptor_stuck", "critical",
                                       "Dau doc xu keo dai bat thuong (co the ket xu)");
                    }
                } else {
                    pca_stuck_start = 0;
                    ie_fault_clear("coin_acceptor_stuck");
                }
                prev_pca = active;
            }
        }
#endif

        uint32_t coin_pulses = 0;
        uint32_t bill_pulses = 0;

        portENTER_CRITICAL(&s_mux);
        if (s_coin.pulses > 0 && (now - s_coin.last_tick >= gap_ticks)) {
            coin_pulses = s_coin.pulses;
            s_coin.pulses = 0;
            s_coin.last_tick = 0;
        }
        if (s_bill.pulses > 0 && (now - s_bill.last_tick >= gap_ticks)) {
            bill_pulses = s_bill.pulses;
            s_bill.pulses = 0;
            s_bill.last_tick = 0;
        }
        portEXIT_CRITICAL(&s_mux);

        if (coin_pulses > 0 && s_cb) {
            ESP_LOGI(TAG, "coin burst: %lu pulses", (unsigned long)coin_pulses);
            s_cb(INNOEDGE_PULSE_COIN, coin_pulses, 0, s_cb_ctx);
        }

        if (bill_pulses > 0 && s_cb) {
            int64_t amount = (int64_t)bill_pulses * CONFIG_INNOEDGE_BILL_VND_PER_PULSE;
            ESP_LOGI(TAG, "bill burst: %lu pulses (%lld VND)",
                     (unsigned long)bill_pulses, (long long)amount);
            s_cb(INNOEDGE_PULSE_BILL, bill_pulses, amount, s_cb_ctx);
        }
    }
}

static esp_err_t configure_input(gpio_num_t gpio, innoedge_pulse_kind_t kind)
{
    if ((int)gpio < 0) {
        return ESP_OK;
    }
    if (gpio >= GPIO_NUM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    gpio_config_t io_conf = {
        .pin_bit_mask = 1ULL << gpio,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_isr_handler_add(gpio, pulse_isr, (void *)(intptr_t)kind);
}

esp_err_t innoedge_pulse_input_init(innoedge_pulse_input_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
    bool coin_gpio_enabled = CONFIG_INNOEDGE_COIN_PULSE_GPIO >= 0;
    bool bill_enabled = CONFIG_INNOEDGE_BILL_PULSE_GPIO >= 0;
    bool coin_enabled = coin_gpio_enabled || CONFIG_INNOEDGE_COIN_PULSE_PCA9554_P0;
    if (!coin_enabled && !bill_enabled) {
        ESP_LOGW(TAG, "pulse inputs disabled");
        return ESP_OK;
    }

#if CONFIG_INNOEDGE_COIN_PULSE_PCA9554_P0
    if (!s_expander_read) {
        ie_fault_set("coin_reader_init_failed", "critical",
                       "Chua dang ky nguon doc xu expander - may khong nhan duoc xu");
    }
#endif

    esp_err_t err = ESP_OK;
    if (coin_gpio_enabled || bill_enabled) {
        err = gpio_install_isr_service(0);
        if (err == ESP_ERR_INVALID_STATE) {
            err = ESP_OK;
        }
        if (err != ESP_OK) {
            return err;
        }
    }
    if (coin_gpio_enabled) {
        err = configure_input((gpio_num_t)CONFIG_INNOEDGE_COIN_PULSE_GPIO, INNOEDGE_PULSE_COIN);
        if (err != ESP_OK) {
            ie_fault_set("coin_reader_init_failed", "critical",
                           "Khoi tao dau doc xu that bai - may khong nhan duoc xu");
            return err;
        }
    }
    if (bill_enabled) {
        err = configure_input((gpio_num_t)CONFIG_INNOEDGE_BILL_PULSE_GPIO, INNOEDGE_PULSE_BILL);
        if (err != ESP_OK) {
            ie_fault_set("bill_reader_init_failed", "critical",
                           "Khoi tao dau doc tien that bai");
            return err;
        }
    }
    BaseType_t ok = xTaskCreate(pulse_task, "ie_pulse", 4096, NULL, 6, NULL);
    if (ok != pdPASS) {
        ie_fault_set("coin_reader_init_failed", "critical",
                       "Khong tao duoc task doc xu");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "pulse inputs coin_gpio=%d coin_pca_p0=%d bill_gpio=%d",
             CONFIG_INNOEDGE_COIN_PULSE_GPIO, CONFIG_INNOEDGE_COIN_PULSE_PCA9554_P0 ? 1 : 0,
             CONFIG_INNOEDGE_BILL_PULSE_GPIO);
    return ESP_OK;
}
