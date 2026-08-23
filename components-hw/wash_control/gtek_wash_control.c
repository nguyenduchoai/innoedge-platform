#include "gtek_wash_control.h"

#include "gtek_relay_control.h"
#include "gtek_ws_client.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "gtek.wash";

#ifndef CONFIG_GTEK_WASH_WATER_SENSOR_GPIO
#define CONFIG_GTEK_WASH_WATER_SENSOR_GPIO (-1)
#endif
#ifndef CONFIG_GTEK_WASH_MAX_ACTIVATION_SEC
#define CONFIG_GTEK_WASH_MAX_ACTIVATION_SEC 600
#endif
#ifndef CONFIG_GTEK_WASH_MASTER_MARGIN_SEC
#define CONFIG_GTEK_WASH_MASTER_MARGIN_SEC 120
#endif

// Thiết bị rửa (WATER..VACUUM) khớp 1-1 với 4 kênh relay đầu enum.
_Static_assert((int)GTEK_WASH_WATER == (int)GTEK_RELAY_WATER, "wash/relay enum lệch");
_Static_assert((int)GTEK_WASH_VACUUM == (int)GTEK_RELAY_VACUUM, "wash/relay enum lệch");

// ── Trạng thái phiên (bảo vệ bởi s_lock) ────────────────────────────────────
static SemaphoreHandle_t s_lock;
static bool s_active;                                  // phiên đang mở?
static int s_remaining[GTEK_WASH_DEVICE_COUNT];        // ngân sách còn lại (giây)
static gtek_wash_device_t s_active_device = GTEK_WASH_NONE;
static int64_t s_master_deadline_us;                   // hạn chót cả phiên
static int64_t s_activation_started_us;                // mốc bật thiết bị đang chạy (watchdog)
static bool s_no_water_alert;                          // đang báo no_water?

static bool sensor_enabled(void)
{
    return CONFIG_GTEK_WASH_WATER_SENSOR_GPIO >= 0 &&
           CONFIG_GTEK_WASH_WATER_SENSOR_GPIO < GPIO_NUM_MAX;
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

static bool any_budget_left_locked(void)
{
    for (int i = 0; i < GTEK_WASH_DEVICE_COUNT; i++) {
        if (s_remaining[i] > 0) {
            return true;
        }
    }
    return false;
}

// Tắt thiết bị đang chạy (relay off) + xoá mốc. Gọi trong vùng đã lock.
// Các lời gọi gtek_relay_* trong file này bỏ qua return CÓ CHỦ ĐÍCH: tầng
// relay_control đã fault_set critical + alert ngay tại chỗ lỗi (relay_set_failed
// / relay_stuck_on) — không có lỗi nào câm lặng. Đừng "sửa" thành check tại đây.
static void stop_active_locked(void)
{
    if (s_active_device != GTEK_WASH_NONE) {
        gtek_relay_set((gtek_relay_channel_t)s_active_device, false);
    }
    s_active_device = GTEK_WASH_NONE;
    s_activation_started_us = 0;
}

// Kết thúc phiên: tắt hết relay rửa, xoá trạng thái. Gọi trong vùng đã lock.
static void end_session_locked(const char *reason)
{
    if (s_active) {
        ESP_LOGI(TAG, "kết thúc phiên (%s)", reason ? reason : "-");
    }
    stop_active_locked();
    gtek_relay_all_wash_off();
    s_active = false;
    s_master_deadline_us = 0;
    memset(s_remaining, 0, sizeof(s_remaining));
    // Xoá cờ cảnh báo nước nếu còn treo.
    if (s_no_water_alert) {
        s_no_water_alert = false;
        gtek_ws_send_alert("no_water", "warning", "het phien rua", false);
    }
}

// ── Task tick 1Hz ───────────────────────────────────────────────────────────
static void wash_tick_task(void *arg)
{
    (void)arg;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        lock();
        if (!s_active) {
            unlock();
            continue;
        }
        int64_t now = esp_timer_get_time();

        // Hạn chót cả phiên.
        if (s_master_deadline_us > 0 && now > s_master_deadline_us) {
            end_session_locked("master deadline");
            unlock();
            continue;
        }

        if (s_active_device != GTEK_WASH_NONE) {
            int d = (int)s_active_device;
            // Watchdog: 1 lần bật chạy quá lâu → tắt + báo fault (chống kẹt relay).
            if (s_activation_started_us > 0 &&
                (now - s_activation_started_us) >
                    (int64_t)CONFIG_GTEK_WASH_MAX_ACTIVATION_SEC * 1000000LL) {
                ESP_LOGW(TAG, "watchdog: %s bật quá %ds → tắt",
                         gtek_relay_channel_name((gtek_relay_channel_t)d),
                         CONFIG_GTEK_WASH_MAX_ACTIVATION_SEC);
                stop_active_locked();
                unlock();
                gtek_ws_send_alert("fault", "error",
                                   "wash activation watchdog timeout", true);
                continue;
            }

            // Cảm biến nước: nếu đang xịt nước mà cảm biến báo thiếu (mức thấp) →
            // cảnh báo no_water active; khi đủ nước lại → clear.
            if (sensor_enabled() && d == GTEK_WASH_WATER) {
                int level = gpio_get_level(CONFIG_GTEK_WASH_WATER_SENSOR_GPIO);
                if (level == 0 && !s_no_water_alert) {
                    s_no_water_alert = true;
                    unlock();
                    gtek_ws_send_alert("no_water", "warning",
                                       "khong co nuoc khi dang xit", true);
                    lock();
                } else if (level != 0 && s_no_water_alert) {
                    s_no_water_alert = false;
                    unlock();
                    gtek_ws_send_alert("no_water", "warning", "da co nuoc lai", false);
                    lock();
                }
            }

            // Trừ 1 giây cho thiết bị đang chạy + giữ relay bật.
            if (s_remaining[d] > 0) {
                s_remaining[d]--;
            }
            if (s_remaining[d] <= 0) {
                ESP_LOGI(TAG, "%s hết ngân sách → khoá",
                         gtek_relay_channel_name((gtek_relay_channel_t)d));
                stop_active_locked();
            } else {
                // Giữ relay ON (idempotent, phòng lỡ bị tắt ngoài ý muốn).
                gtek_relay_set((gtek_relay_channel_t)d, true);
            }
        }

        // Hết sạch ngân sách mọi thiết bị → kết thúc phiên.
        if (!any_budget_left_locked()) {
            end_session_locked("het ngan sach");
        }
        unlock();
    }
}

esp_err_t gtek_wash_control_init(void)
{
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
        if (!s_lock) {
            return ESP_ERR_NO_MEM;
        }
    }
    // An toàn boot: tắt hết relay rửa, không có phiên.
    s_active = false;
    s_active_device = GTEK_WASH_NONE;
    s_master_deadline_us = 0;
    s_activation_started_us = 0;
    s_no_water_alert = false;
    memset(s_remaining, 0, sizeof(s_remaining));
    gtek_relay_all_wash_off();

    if (sensor_enabled()) {
        gpio_config_t io = {
            .pin_bit_mask = 1ULL << CONFIG_GTEK_WASH_WATER_SENSOR_GPIO,
            .mode = GPIO_MODE_INPUT,
            .pull_up_en = GPIO_PULLUP_ENABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t sensor_err = gpio_config(&io);
        if (sensor_err != ESP_OK) {
            // Cảm biến hỏng → mức đọc rác → cảnh báo no_water sai/miss. Báo ngay,
            // máy vẫn chạy (degrade, không sập phiên rửa vì thiếu cảm biến).
            ESP_LOGW(TAG, "cấu hình cảm biến nước lỗi: %s", esp_err_to_name(sensor_err));
            gtek_ws_send_alert("fault", "warning",
                               "cau hinh cam bien nuoc loi - canh bao thieu nuoc co the sai", true);
        } else {
            ESP_LOGI(TAG, "cảm biến nước GPIO%d bật", CONFIG_GTEK_WASH_WATER_SENSOR_GPIO);
        }
    }

    BaseType_t ok = xTaskCreate(wash_tick_task, "gtek_wash", 4096, NULL, 5, NULL);
    if (ok != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "init xong (tick 1Hz, watchdog %ds)", CONFIG_GTEK_WASH_MAX_ACTIVATION_SEC);
    return ESP_OK;
}

esp_err_t gtek_wash_start(const int budgets_sec[GTEK_WASH_DEVICE_COUNT], int master_max_sec)
{
    if (!budgets_sec) {
        return ESP_ERR_INVALID_ARG;
    }
    lock();
    // Ghi đè phiên cũ: tắt hết trước.
    end_session_locked("phien moi");

    int total = 0;
    for (int i = 0; i < GTEK_WASH_DEVICE_COUNT; i++) {
        int b = budgets_sec[i] > 0 ? budgets_sec[i] : 0;
        s_remaining[i] = b;
        total += b;
    }
    if (total <= 0) {
        unlock();
        ESP_LOGW(TAG, "start bị từ chối: tổng ngân sách = 0");
        return ESP_ERR_INVALID_ARG;
    }

    int master = master_max_sec;
    if (master <= 0) {
        // Tự suy ra: tổng ngân sách + biên cho khách thao tác/nghỉ giữa chừng.
        master = total + CONFIG_GTEK_WASH_MASTER_MARGIN_SEC;
    }
    s_master_deadline_us = esp_timer_get_time() + (int64_t)master * 1000000LL;
    s_active = true;
    s_active_device = GTEK_WASH_NONE;
    s_activation_started_us = 0;
    ESP_LOGI(TAG, "phiên bắt đầu: water=%d foam=%d air=%d vacuum=%d, master=%ds",
             s_remaining[0], s_remaining[1], s_remaining[2], s_remaining[3], master);
    unlock();
    return ESP_OK;
}

esp_err_t gtek_wash_activate(gtek_wash_device_t device)
{
    if (device < 0 || device >= GTEK_WASH_DEVICE_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    lock();
    if (!s_active) {
        unlock();
        ESP_LOGW(TAG, "activate %s bị bỏ qua: không có phiên",
                 gtek_relay_channel_name((gtek_relay_channel_t)device));
        return ESP_ERR_INVALID_STATE;
    }
    if (s_remaining[device] <= 0) {
        unlock();
        ESP_LOGW(TAG, "activate %s bị bỏ qua: hết ngân sách",
                 gtek_relay_channel_name((gtek_relay_channel_t)device));
        return ESP_ERR_INVALID_STATE;
    }
    if (s_active_device == device) {
        unlock();  // đang chạy đúng thiết bị này rồi
        return ESP_OK;
    }
    // Đổi sang thiết bị mới: relay độc quyền (tắt các kênh rửa khác, bật kênh này).
    gtek_relay_set_exclusive((gtek_relay_channel_t)device);
    s_active_device = device;
    s_activation_started_us = esp_timer_get_time();
    ESP_LOGI(TAG, "activate %s (còn %ds)",
             gtek_relay_channel_name((gtek_relay_channel_t)device), s_remaining[device]);
    unlock();
    return ESP_OK;
}

esp_err_t gtek_wash_stop_active(void)
{
    lock();
    stop_active_locked();
    unlock();
    return ESP_OK;
}

esp_err_t gtek_wash_end(void)
{
    lock();
    end_session_locked("yêu cầu dừng");
    unlock();
    return ESP_OK;
}

bool gtek_wash_status(gtek_wash_status_t *status)
{
    lock();
    bool active = s_active;
    if (status) {
        memset(status, 0, sizeof(*status));
        status->active = s_active;
        status->active_device = s_active_device;
        for (int i = 0; i < GTEK_WASH_DEVICE_COUNT; i++) {
            status->remaining_sec[i] = s_remaining[i];
        }
        if (s_active_device != GTEK_WASH_NONE) {
            status->active_remaining_sec = s_remaining[s_active_device];
        }
        if (s_active && s_master_deadline_us > 0) {
            int64_t left = (s_master_deadline_us - esp_timer_get_time()) / 1000000LL;
            status->session_remaining_sec = left > 0 ? (int)left : 0;
        }
    }
    unlock();
    return active;
}
