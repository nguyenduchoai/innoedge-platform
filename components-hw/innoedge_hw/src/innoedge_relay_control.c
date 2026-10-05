// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "innoedge_relay_control.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gtek_fault.h"
#include "sdkconfig.h"

static const char *TAG = "innoedge.relay";

// GPIO mặc định cho các kênh chưa khai báo trong Kconfig (an toàn = -1).
#ifndef CONFIG_INNOEDGE_WASH_GPIO_WATER
#ifdef CONFIG_GTEK_WASH_GPIO_WATER
#define CONFIG_INNOEDGE_WASH_GPIO_WATER CONFIG_GTEK_WASH_GPIO_WATER
#else
#define CONFIG_INNOEDGE_WASH_GPIO_WATER (-1)
#endif
#endif

#ifndef CONFIG_INNOEDGE_WASH_GPIO_FOAM
#ifdef CONFIG_GTEK_WASH_GPIO_FOAM
#define CONFIG_INNOEDGE_WASH_GPIO_FOAM CONFIG_GTEK_WASH_GPIO_FOAM
#else
#define CONFIG_INNOEDGE_WASH_GPIO_FOAM (-1)
#endif
#endif

#ifndef CONFIG_INNOEDGE_WASH_GPIO_AIR
#ifdef CONFIG_GTEK_WASH_GPIO_AIR
#define CONFIG_INNOEDGE_WASH_GPIO_AIR CONFIG_GTEK_WASH_GPIO_AIR
#else
#define CONFIG_INNOEDGE_WASH_GPIO_AIR (-1)
#endif
#endif

#ifndef CONFIG_INNOEDGE_WASH_GPIO_VACUUM
#ifdef CONFIG_GTEK_WASH_GPIO_VACUUM
#define CONFIG_INNOEDGE_WASH_GPIO_VACUUM CONFIG_GTEK_WASH_GPIO_VACUUM
#else
#define CONFIG_INNOEDGE_WASH_GPIO_VACUUM (-1)
#endif
#endif

#ifndef CONFIG_INNOEDGE_RELAY_GPIO
#ifdef CONFIG_GTEK_RELAY_GPIO
#define CONFIG_INNOEDGE_RELAY_GPIO CONFIG_GTEK_RELAY_GPIO
#else
#define CONFIG_INNOEDGE_RELAY_GPIO (-1)
#endif
#endif

#ifndef CONFIG_INNOEDGE_RELAY_PULSE_MS
#ifdef CONFIG_GTEK_RELAY_PULSE_MS
#define CONFIG_INNOEDGE_RELAY_PULSE_MS CONFIG_GTEK_RELAY_PULSE_MS
#else
#define CONFIG_INNOEDGE_RELAY_PULSE_MS 80
#endif
#endif

// Bảng kênh: GPIO + tên. Thứ tự khớp innoedge_relay_channel_t.
static const struct {
    int gpio;
    const char *name;
} s_channels[INNOEDGE_RELAY_COUNT] = {
    [INNOEDGE_RELAY_WATER] = {CONFIG_INNOEDGE_WASH_GPIO_WATER, "water"},
    [INNOEDGE_RELAY_FOAM] = {CONFIG_INNOEDGE_WASH_GPIO_FOAM, "foam"},
    [INNOEDGE_RELAY_AIR] = {CONFIG_INNOEDGE_WASH_GPIO_AIR, "air"},
    [INNOEDGE_RELAY_VACUUM] = {CONFIG_INNOEDGE_WASH_GPIO_VACUUM, "vacuum"},
    [INNOEDGE_RELAY_DISPENSE] = {CONFIG_INNOEDGE_RELAY_GPIO, "dispense"},
};

static bool gpio_ok(int gpio)
{
    return gpio >= 0 && gpio < GPIO_NUM_MAX;
}

const char *innoedge_relay_channel_name(innoedge_relay_channel_t channel)
{
    if (channel < 0 || channel >= INNOEDGE_RELAY_COUNT) {
        return "?";
    }
    return s_channels[channel].name;
}

esp_err_t innoedge_relay_control_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < INNOEDGE_RELAY_COUNT; i++) {
        if (gpio_ok(s_channels[i].gpio)) {
            mask |= 1ULL << s_channels[i].gpio;
        } else {
            ESP_LOGW(TAG, "kênh %s vô hiệu hoá (GPIO=-1)", s_channels[i].name);
        }
    }
    if (mask == 0) {
        ESP_LOGW(TAG, "không có relay nào được cấu hình");
        gtek_fault_set("relay_none_configured", "warning",
                       "Khong relay nao duoc cau hinh - may khong dieu khien duoc co cau");
        return ESP_OK;
    }

    for (int i = 0; i < INNOEDGE_RELAY_COUNT; i++) {
        if (gpio_ok(s_channels[i].gpio)) {
            gpio_reset_pin(s_channels[i].gpio);
        }
    }
    gpio_config_t io_conf = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t cfg_err = gpio_config(&io_conf);
    if (cfg_err != ESP_OK) {
        ESP_LOGE(TAG, "gpio config failed: %s", esp_err_to_name(cfg_err));
        gtek_fault_set("relay_init_fail", "critical", "Khoi tao relay that bai");
        return cfg_err;
    }
    for (int i = 0; i < INNOEDGE_RELAY_COUNT; i++) {
        if (gpio_ok(s_channels[i].gpio)) {
            gpio_set_level(s_channels[i].gpio, 0);
        }
    }
    ESP_LOGI(TAG, "init xong, tất cả relay OFF");
    return ESP_OK;
}

esp_err_t innoedge_relay_set(innoedge_relay_channel_t channel, bool on)
{
    if (channel < 0 || channel >= INNOEDGE_RELAY_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    int gpio = s_channels[channel].gpio;
    if (!gpio_ok(gpio)) {
        ESP_LOGI(TAG, "[no-op] %s %s (GPIO=-1)", s_channels[channel].name, on ? "ON" : "OFF");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "%s %s (GPIO%d)", s_channels[channel].name, on ? "ON" : "OFF", gpio);
    esp_err_t err = gpio_set_level(gpio, on ? 1 : 0);
    if (err != ESP_OK) {
        gtek_fault_set("relay_set_failed", "critical", "Khong dieu khien duoc relay");
    }
    return err;
}

esp_err_t innoedge_relay_set_exclusive(innoedge_relay_channel_t channel)
{
    esp_err_t first_err = ESP_OK;
    for (int i = INNOEDGE_RELAY_WATER; i <= INNOEDGE_RELAY_VACUUM; i++) {
        if (i == (int)channel) {
            continue;
        }
        esp_err_t err = innoedge_relay_set((innoedge_relay_channel_t)i, false);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    if (channel >= INNOEDGE_RELAY_WATER && channel <= INNOEDGE_RELAY_VACUUM) {
        esp_err_t err = innoedge_relay_set(channel, true);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}

esp_err_t innoedge_relay_all_wash_off(void)
{
    esp_err_t first_err = ESP_OK;
    for (int i = INNOEDGE_RELAY_WATER; i <= INNOEDGE_RELAY_VACUUM; i++) {
        esp_err_t err = innoedge_relay_set((innoedge_relay_channel_t)i, false);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}

esp_err_t innoedge_relay_control_pulse(uint32_t pulse_ms)
{
    int gpio = s_channels[INNOEDGE_RELAY_DISPENSE].gpio;
    if (!gpio_ok(gpio)) {
        gtek_fault_set("dispense_relay_not_configured", "critical",
                       "Relay nha tien chua duoc cau hinh - khach tra tien khong ra credit");
        return ESP_ERR_INVALID_STATE;
    }
    if (pulse_ms == 0) {
        pulse_ms = CONFIG_INNOEDGE_RELAY_PULSE_MS;
    }
    esp_err_t on_err = gpio_set_level(gpio, 1);
    if (on_err != ESP_OK) {
        gtek_fault_set("dispense_relay_error", "critical", "Khong kich duoc relay nha tien");
        return on_err;
    }
    vTaskDelay(pdMS_TO_TICKS(pulse_ms));
    esp_err_t off_err = gpio_set_level(gpio, 0);
    if (off_err != ESP_OK) {
        gtek_fault_set("relay_stuck_on", "critical", "Relay nha tien khong tat duoc - nguy co ket bat");
    }
    return off_err;
}
