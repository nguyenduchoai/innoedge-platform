// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
#include "innoedge_wash_control.h"

#include "innoedge_relay_control.h"
#include "gtek_ws_client.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "innoedge.wash";

#ifndef CONFIG_INNOEDGE_WASH_WATER_SENSOR_GPIO
#ifdef CONFIG_GTEK_WASH_WATER_SENSOR_GPIO
#define CONFIG_INNOEDGE_WASH_WATER_SENSOR_GPIO CONFIG_GTEK_WASH_WATER_SENSOR_GPIO
#else
#define CONFIG_INNOEDGE_WASH_WATER_SENSOR_GPIO (-1)
#endif
#endif

#ifndef CONFIG_INNOEDGE_WASH_MAX_ACTIVATION_SEC
#ifdef CONFIG_GTEK_WASH_MAX_ACTIVATION_SEC
#define CONFIG_INNOEDGE_WASH_MAX_ACTIVATION_SEC CONFIG_GTEK_WASH_MAX_ACTIVATION_SEC
#else
#define CONFIG_INNOEDGE_WASH_MAX_ACTIVATION_SEC 600
#endif
#endif

#ifndef CONFIG_INNOEDGE_WASH_MASTER_MARGIN_SEC
#ifdef CONFIG_GTEK_WASH_MASTER_MARGIN_SEC
#define CONFIG_INNOEDGE_WASH_MASTER_MARGIN_SEC CONFIG_GTEK_WASH_MASTER_MARGIN_SEC
#else
#define CONFIG_INNOEDGE_WASH_MASTER_MARGIN_SEC 120
#endif
#endif

// Thiết bị rửa (WATER..VACUUM) khớp 1-1 với 4 kênh relay đầu enum.
_Static_assert((int)INNOEDGE_WASH_WATER == (int)INNOEDGE_RELAY_WATER, "wash/relay enum lệch");
_Static_assert((int)INNOEDGE_WASH_VACUUM == (int)INNOEDGE_RELAY_VACUUM, "wash/relay enum lệch");

// ── Trạng thái phiên (bảo vệ bởi s_lock) ────────────────────────────────────
static SemaphoreHandle_t s_lock;
static bool s_active;                                  // phiên đang mở?
static int s_remaining[INNOEDGE_WASH_DEVICE_COUNT];    // ngân sách còn lại (giây)
static innoedge_wash_device_t s_active_device = INNOEDGE_WASH_NONE;
static int64_t s_master_deadline_us;                   // hạn chót cả phiên
static int64_t s_activation_started_us;                // mốc bật thiết bị đang chạy (watchdog)
static bool s_no_water_alert;                          // đang báo no_water?

static bool sensor_enabled(void)
{
    return CONFIG_INNOEDGE_WASH_WATER_SENSOR_GPIO >= 0 &&
           CONFIG_INNOEDGE_WASH_WATER_SENSOR_GPIO < GPIO_NUM_MAX;
}

static void lock(void)
{
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
}

static void unlock(void)
{
    if (s_lock) {
        xSemaphoreGive(s_lock);
    }
}

static bool raw_has_water(void)
{
    if (!sensor_enabled()) {
        return true;
    }
    return gpio_get_level((gpio_num_t)CONFIG_INNOEDGE_WASH_WATER_SENSOR_GPIO) == 0;
}

static void stop_active_locked(void)
{
    if (s_active_device != INNOEDGE_WASH_NONE) {
        innoedge_relay_set((innoedge_relay_channel_t)s_active_device, false);
        ESP_LOGI(TAG, "tắt thiết bị %s (tạm dừng hoặc hết ngân sách)",
                 innoedge_relay_channel_name((innoedge_relay_channel_t)s_active_device));
        s_active_device = INNOEDGE_WASH_NONE;
        s_activation_started_us = 0;
    }
}

static void end_session_locked(const char *reason)
{
    if (!s_active) {
        return;
    }
    ESP_LOGI(TAG, "phiên kết thúc (%s)", reason ? reason : "unknown");
    stop_active_locked();
    innoedge_relay_all_wash_off();
    s_active = false;
    memset(s_remaining, 0, sizeof(s_remaining));
    s_master_deadline_us = 0;
    s_activation_started_us = 0;
    if (s_no_water_alert) {
        s_no_water_alert = false;
        gtek_ws_send_alert("no_water", "warning", "het phien rua", false);
    }
}

static void wash_tick_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    const TickType_t period = pdMS_TO_TICKS(1000);

    for (;;) {
        vTaskDelayUntil(&last_wake, period);
        int64_t now_us = esp_timer_get_time();

        lock();
        if (!s_active) {
            unlock();
            continue;
        }

        if (now_us >= s_master_deadline_us) {
            end_session_locked("het gio master deadline");
            unlock();
            continue;
        }

        if (s_active_device != INNOEDGE_WASH_NONE) {
            int d = s_active_device;

            if (s_remaining[d] > 0) {
                s_remaining[d]--;
            }

            int64_t elapsed_sec = (now_us - s_activation_started_us) / 1000000LL;
            if (elapsed_sec >= CONFIG_INNOEDGE_WASH_MAX_ACTIVATION_SEC) {
                ESP_LOGW(TAG, "watchdog: %s chạy liên tục %llds (max %ds) → tắt",
                         innoedge_relay_channel_name((innoedge_relay_channel_t)d),
                         (long long)elapsed_sec, CONFIG_INNOEDGE_WASH_MAX_ACTIVATION_SEC);
                stop_active_locked();
                gtek_ws_send_alert("fault", "error",
                                   "watchdog phien: relay chay qua lau lien tuc, tu dong ngat", true);
                unlock();
                continue;
            }

            if (d == INNOEDGE_WASH_WATER && sensor_enabled()) {
                bool water = raw_has_water();
                if (!water && !s_no_water_alert) {
                    s_no_water_alert = true;
                    ESP_LOGE(TAG, "MẤT NƯỚC! Tắt bơm bảo vệ máy");
                    innoedge_relay_set((innoedge_relay_channel_t)INNOEDGE_WASH_WATER, false);
                    gtek_ws_send_alert("no_water", "warning",
                                       "Cam bien bao het nuoc - tam ngat bom bao ve", true);
                } else if (water && s_no_water_alert) {
                    s_no_water_alert = false;
                    ESP_LOGI(TAG, "Có nước lại");
                    gtek_ws_send_alert("no_water", "warning", "da co nuoc lai", false);
                    if (s_active_device == INNOEDGE_WASH_WATER && s_remaining[INNOEDGE_WASH_WATER] > 0) {
                        innoedge_relay_set((innoedge_relay_channel_t)INNOEDGE_WASH_WATER, true);
                    }
                }
            }

            if (s_remaining[d] <= 0) {
                ESP_LOGI(TAG, "thiết bị %s HẾT NGÂN SÁCH",
                         innoedge_relay_channel_name((innoedge_relay_channel_t)d));
                stop_active_locked();

                bool any_left = false;
                for (int i = 0; i < INNOEDGE_WASH_DEVICE_COUNT; i++) {
                    if (s_remaining[i] > 0) {
                        any_left = true;
                        break;
                    }
                }
                if (!any_left) {
                    end_session_locked("het tat ca ngan sach thiet bi");
                    unlock();
                    continue;
                }
            }
        }

        unlock();
    }
}

esp_err_t innoedge_wash_control_init(void)
{
    if (s_lock == NULL) {
        s_lock = xSemaphoreCreateMutex();
        if (s_lock == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }

    lock();
    s_active = false;
    memset(s_remaining, 0, sizeof(s_remaining));
    s_active_device = INNOEDGE_WASH_NONE;
    s_master_deadline_us = 0;
    s_activation_started_us = 0;
    s_no_water_alert = false;
    unlock();

    innoedge_relay_all_wash_off();

    if (sensor_enabled()) {
        gpio_config_t io_conf = {
            .pin_bit_mask = 1ULL << CONFIG_INNOEDGE_WASH_WATER_SENSOR_GPIO,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&io_conf);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "cấu hình water sensor GPIO%d thất bại: %s",
                     CONFIG_INNOEDGE_WASH_WATER_SENSOR_GPIO, esp_err_to_name(err));
            gtek_ws_send_alert("fault", "warning",
                               "Khong doc duoc cam bien nuoc", true);
        } else {
            ESP_LOGI(TAG, "water sensor bật trên GPIO%d (active-low)",
                     CONFIG_INNOEDGE_WASH_WATER_SENSOR_GPIO);
        }
    }

    BaseType_t ok = xTaskCreate(wash_tick_task, "ie_wash", 4096, NULL, 5, NULL);
    if (ok != pdPASS) {
        ESP_LOGE(TAG, "không tạo được wash tick task");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t innoedge_wash_start(const int budgets_sec[INNOEDGE_WASH_DEVICE_COUNT], int master_max_sec)
{
    if (budgets_sec == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    int sum = 0;
    for (int i = 0; i < INNOEDGE_WASH_DEVICE_COUNT; i++) {
        if (budgets_sec[i] > 0) {
            sum += budgets_sec[i];
        }
    }
    if (sum <= 0) {
        ESP_LOGW(TAG, "wash_start: tổng ngân sách = 0, từ chối");
        return ESP_ERR_INVALID_ARG;
    }
    if (master_max_sec <= 0) {
        master_max_sec = sum + CONFIG_INNOEDGE_WASH_MASTER_MARGIN_SEC;
    }

    lock();
    stop_active_locked();
    innoedge_relay_all_wash_off();

    s_active = true;
    for (int i = 0; i < INNOEDGE_WASH_DEVICE_COUNT; i++) {
        s_remaining[i] = budgets_sec[i] > 0 ? budgets_sec[i] : 0;
    }
    s_active_device = INNOEDGE_WASH_NONE;
    int64_t now_us = esp_timer_get_time();
    s_master_deadline_us = now_us + (int64_t)master_max_sec * 1000000LL;
    s_activation_started_us = 0;
    s_no_water_alert = false;

    ESP_LOGI(TAG, "PHIÊN MỚI: water=%ds foam=%ds air=%ds vacuum=%ds | master=%ds",
             s_remaining[0], s_remaining[1], s_remaining[2], s_remaining[3], master_max_sec);
    unlock();
    return ESP_OK;
}

esp_err_t innoedge_wash_activate(innoedge_wash_device_t device)
{
    if (device < 0 || device >= INNOEDGE_WASH_DEVICE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }

    lock();
    if (!s_active) {
        ESP_LOGW(TAG, "activate %s thất bại: không có phiên nào mở",
                 innoedge_relay_channel_name((innoedge_relay_channel_t)device));
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_remaining[device] <= 0) {
        ESP_LOGW(TAG, "activate %s thất bại: ngân sách = 0",
                 innoedge_relay_channel_name((innoedge_relay_channel_t)device));
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    if (s_active_device == device) {
        unlock();
        return ESP_OK;
    }

    s_active_device = device;
    s_activation_started_us = esp_timer_get_time();
    innoedge_relay_set_exclusive((innoedge_relay_channel_t)device);

    ESP_LOGI(TAG, "BẬT %s (ngân sách còn %ds)",
             innoedge_relay_channel_name((innoedge_relay_channel_t)device), s_remaining[device]);
    unlock();
    return ESP_OK;
}

esp_err_t innoedge_wash_stop_active(void)
{
    lock();
    if (!s_active) {
        unlock();
        return ESP_ERR_INVALID_STATE;
    }
    stop_active_locked();
    unlock();
    return ESP_OK;
}

esp_err_t innoedge_wash_end(void)
{
    lock();
    end_session_locked("lệnh kết thúc chủ động");
    unlock();
    return ESP_OK;
}

bool innoedge_wash_status(innoedge_wash_status_t *status)
{
    lock();
    bool active = s_active;
    if (status != NULL) {
        status->active = active;
        if (active) {
            for (int i = 0; i < INNOEDGE_WASH_DEVICE_COUNT; i++) {
                status->remaining_sec[i] = s_remaining[i];
            }
            status->active_device = s_active_device;
            status->active_remaining_sec =
                (s_active_device != INNOEDGE_WASH_NONE) ? s_remaining[s_active_device] : 0;
            int64_t now_us = esp_timer_get_time();
            int64_t left_us = s_master_deadline_us - now_us;
            status->session_remaining_sec = left_us > 0 ? (int)(left_us / 1000000LL) : 0;
        } else {
            memset(status, 0, sizeof(*status));
            status->active_device = INNOEDGE_WASH_NONE;
        }
    }
    unlock();
    return active;
}
