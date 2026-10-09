// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// 13 — Robot STEM Tự Hành: Xe Giao Hàng & Dịch Vụ Thông Minh
//
// Kết hợp lập trình Robot STEM với hạ tầng giao dịch InnoEdge:
// - Động cơ 2 bánh: điều khiển tiến, lùi, quay trái, quay phải, phanh.
// - Cảm biến siêu âm HC-SR04: liên tục quét khoảng cách, tự động phanh khi gặp vật cản < 15cm.
// - Servo SG90: cơ cấu khoá/mở nắp thùng giao hàng.
// - InnoEdge Command Bus: nhận lệnh thời gian thực từ Cloud, App hoặc Scratch/Blockly.
// - Mô hình kinh tế Robot: Khách quét VietQR (10.000 đ) -> on_paid() kích hoạt xe chạy
//   tới bàn và mở nắp thùng đồ!

#include "innoedge.h"

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "stem-robot";

// Định nghĩa chân phần cứng
#define BOOT_BUTTON_GPIO    0
#define STATUS_LED_GPIO     2

#define MOTOR_L_IN1_GPIO    4
#define MOTOR_L_IN2_GPIO    5
#define MOTOR_R_IN1_GPIO    6
#define MOTOR_R_IN2_GPIO    7

#define ULTRASONIC_TRIG     15
#define ULTRASONIC_ECHO     16
#define SERVO_PWM_GPIO      18

// Trạng thái chuyển động của Robot
typedef enum {
    BOT_STOP = 0,
    BOT_FORWARD,
    BOT_BACKWARD,
    BOT_LEFT,
    BOT_RIGHT
} bot_motion_t;

static volatile bot_motion_t s_motion = BOT_STOP;
static volatile float s_distance_cm = 100.0f;
static volatile int s_servo_angle = 0; // 0 = đóng nắp, 90 = mở nắp thùng đồ
static volatile int s_move_remaining_ms = 0;

// Cấu hình LEDC PWM cho Servo SG90 (50Hz)
static void servo_init(void)
{
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_14_BIT, // 0 - 16383
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 50,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_conf));

    ledc_channel_config_t ch_conf = {
        .gpio_num = SERVO_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
        .hpoint = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ch_conf));
}

static void servo_set_angle(int angle)
{
    if (angle < 0) angle = 0;
    if (angle > 180) angle = 180;
    s_servo_angle = angle;
    // 50Hz chu kỳ 20ms: 0.5ms (0°) -> 2.5ms (180°)
    // 14-bit: 16383 = 20ms. 0.5ms = 410, 2.5ms = 2048
    uint32_t duty = 410 + (uint32_t)((float)angle / 180.0f * (2048 - 410));
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
    ESP_LOGI(TAG, "🦾 Servo quay góc %d° (%s)", angle, angle >= 45 ? "MỞ THÙNG ĐỒ" : "ĐÓNG KHOÁ");
}

static void set_motors(bot_motion_t m)
{
    s_motion = m;
    switch (m) {
    case BOT_FORWARD:
        gpio_set_level(MOTOR_L_IN1_GPIO, 1); gpio_set_level(MOTOR_L_IN2_GPIO, 0);
        gpio_set_level(MOTOR_R_IN1_GPIO, 1); gpio_set_level(MOTOR_R_IN2_GPIO, 0);
        break;
    case BOT_BACKWARD:
        gpio_set_level(MOTOR_L_IN1_GPIO, 0); gpio_set_level(MOTOR_L_IN2_GPIO, 1);
        gpio_set_level(MOTOR_R_IN1_GPIO, 0); gpio_set_level(MOTOR_R_IN2_GPIO, 1);
        break;
    case BOT_LEFT:
        gpio_set_level(MOTOR_L_IN1_GPIO, 0); gpio_set_level(MOTOR_L_IN2_GPIO, 1);
        gpio_set_level(MOTOR_R_IN1_GPIO, 1); gpio_set_level(MOTOR_R_IN2_GPIO, 0);
        break;
    case BOT_RIGHT:
        gpio_set_level(MOTOR_L_IN1_GPIO, 1); gpio_set_level(MOTOR_L_IN2_GPIO, 0);
        gpio_set_level(MOTOR_R_IN1_GPIO, 0); gpio_set_level(MOTOR_R_IN2_GPIO, 1);
        break;
    case BOT_STOP:
    default:
        gpio_set_level(MOTOR_L_IN1_GPIO, 0); gpio_set_level(MOTOR_L_IN2_GPIO, 0);
        gpio_set_level(MOTOR_R_IN1_GPIO, 0); gpio_set_level(MOTOR_R_IN2_GPIO, 0);
        break;
    }
}

// Đo khoảng cách cảm biến siêu âm HC-SR04
static float measure_distance(void)
{
    gpio_set_level(ULTRASONIC_TRIG, 0);
    ets_delay_us(2);
    gpio_set_level(ULTRASONIC_TRIG, 1);
    ets_delay_us(10);
    gpio_set_level(ULTRASONIC_TRIG, 0);

    int64_t start_time = esp_timer_get_time();
    while (gpio_get_level(ULTRASONIC_ECHO) == 0) {
        if (esp_timer_get_time() - start_time > 10000) return 999.0f; // Timeout 10ms
    }

    int64_t pulse_start = esp_timer_get_time();
    while (gpio_get_level(ULTRASONIC_ECHO) == 1) {
        if (esp_timer_get_time() - pulse_start > 30000) return 999.0f; // Timeout 30ms (~5m)
    }
    int64_t duration_us = esp_timer_get_time() - pulse_start;

    float dist = (float)duration_us * 0.0343f / 2.0f;
    return dist;
}

// Task nền giám sát an toàn & né vật cản
static void robot_safety_task(void *arg)
{
    (void)arg;
    while (true) {
        float dist = measure_distance();
        s_distance_cm = dist;

        // An toàn khẩn cấp: nếu đang tiến lên mà gặp vật cản < 15cm -> Phanh gấp!
        if (s_motion == BOT_FORWARD && dist < 15.0f && dist > 0.5f) {
            ESP_LOGW(TAG, "⚠️ CẢNH BÁO: Phát hiện vật cản ở %.1f cm -> PHANH GẤP!", dist);
            set_motors(BOT_STOP);
            s_move_remaining_ms = 0;
        }

        // Quản lý thời gian chạy lệnh
        if (s_move_remaining_ms > 0) {
            s_move_remaining_ms -= 50;
            if (s_move_remaining_ms <= 0) {
                set_motors(BOT_STOP);
                ESP_LOGI(TAG, "Đã hoàn thành thời gian di chuyển -> Dừng.");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ── InnoEdge Event Handlers ──────────────────────────────────────────────────

static void on_qr(const char *payload, int64_t amount_vnd, const char *ref_code,
                  int expires_sec, int64_t intent_id)
{
    ESP_LOGI(TAG, "Mã VietQR gọi Robot giao hàng: %lld đ | Ref: %s (Hạn: %ds, intent=%lld)",
             (long long)amount_vnd, ref_code, expires_sec, (long long)intent_id);
    ESP_LOGI(TAG, "Quét mã để kích hoạt Robot giao đồ: %s", payload);
}

static void on_paid(int64_t intent_id, int64_t amount_vnd)
{
    ESP_LOGI(TAG, "✓ XÁC NHẬN TIỀN VỀ: %lld đ (intent=%lld) -> BẮT ĐẦU NHIỆM VỤ GIAO HÀNG!",
             (long long)amount_vnd, (long long)intent_id);

    // Kịch bản giao hàng tự động:
    // 1. Tiến lên 2 giây về phía bàn khách
    set_motors(BOT_FORWARD);
    s_move_remaining_ms = 2000;
    vTaskDelay(pdMS_TO_TICKS(2200));

    // 2. Mở nắp thùng hàng bằng Servo
    servo_set_angle(90);
    vTaskDelay(pdMS_TO_TICKS(4000)); // Chờ 4s cho khách lấy đồ

    // 3. Đóng nắp thùng hàng và thông báo hoàn thành
    servo_set_angle(0);
    ESP_LOGI(TAG, "Nhiệm vụ hoàn tất! Robot sẵn sàng cho lượt tiếp theo.");

    char report[128];
    snprintf(report, sizeof(report), "{\"intent_id\":%lld,\"status\":\"delivered\",\"amount_vnd\":%lld}",
             (long long)intent_id, (long long)amount_vnd);
    innoedge_publish_event("delivery_success", report);
}

// ── InnoEdge Remote Command Handlers ─────────────────────────────────────────

// {"action":"bot_move", "params":{"direction":"forward", "ms":1500}}
static esp_err_t cmd_bot_move(cJSON *params, char *result, size_t result_len,
                              char *msg, size_t msg_len)
{
    cJSON *dir = params ? cJSON_GetObjectItem(params, "direction") : NULL;
    cJSON *ms_item = params ? cJSON_GetObjectItem(params, "ms") : NULL;

    const char *dir_str = cJSON_IsString(dir) ? dir->valuestring : "forward";
    int ms = cJSON_IsNumber(ms_item) ? ms_item->valueint : 1000;
    if (ms > 10000) ms = 10000; // Trần an toàn tối đa 10s

    if (strcmp(dir_str, "forward") == 0) {
        if (s_distance_cm < 15.0f) {
            snprintf(msg, msg_len, "khong the tien: co vat can o %.1f cm", s_distance_cm);
            return ESP_ERR_INVALID_STATE;
        }
        set_motors(BOT_FORWARD);
    } else if (strcmp(dir_str, "backward") == 0) {
        set_motors(BOT_BACKWARD);
    } else if (strcmp(dir_str, "left") == 0) {
        set_motors(BOT_LEFT);
    } else if (strcmp(dir_str, "right") == 0) {
        set_motors(BOT_RIGHT);
    } else {
        set_motors(BOT_STOP);
    }

    s_move_remaining_ms = ms;
    snprintf(result, result_len, "{\"direction\":\"%s\",\"duration_ms\":%d,\"moving\":true}", dir_str, ms);
    snprintf(msg, msg_len, "robot dang chay: %s", dir_str);
    return ESP_OK;
}

// {"action":"bot_servo", "params":{"angle":90}}
static esp_err_t cmd_bot_servo(cJSON *params, char *result, size_t result_len,
                               char *msg, size_t msg_len)
{
    cJSON *ang = params ? cJSON_GetObjectItem(params, "angle") : NULL;
    int a = cJSON_IsNumber(ang) ? ang->valueint : 0;
    servo_set_angle(a);

    snprintf(result, result_len, "{\"angle\":%d,\"open\":%s}", a, a >= 45 ? "true" : "false");
    snprintf(msg, msg_len, "da quay servo goc %d", a);
    return ESP_OK;
}

// {"action":"bot_status"}
static esp_err_t cmd_bot_status(cJSON *params, char *result, size_t result_len,
                                char *msg, size_t msg_len)
{
    (void)params;
    snprintf(result, result_len,
             "{\"device\":\"%s\",\"distance_cm\":%.1f,\"motion\":%d,\"servo_angle\":%d}",
             innoedge_device_id(), s_distance_cm, (int)s_motion, s_servo_angle);
    snprintf(msg, msg_len, "ok");
    return ESP_OK;
}

void app_main(void)
{
    static const innoedge_events_t events = {
        .on_qr = on_qr,
        .on_paid = on_paid,
    };
    innoedge_config_t cfg = {
        .events = &events,
    };

    ESP_ERROR_CHECK(innoedge_init(&cfg));

    // Cấu hình chân điều khiển động cơ
    gpio_config_t motor_io = {
        .pin_bit_mask = (1ULL << MOTOR_L_IN1_GPIO) | (1ULL << MOTOR_L_IN2_GPIO) |
                        (1ULL << MOTOR_R_IN1_GPIO) | (1ULL << MOTOR_R_IN2_GPIO) |
                        (1ULL << STATUS_LED_GPIO)  | (1ULL << ULTRASONIC_TRIG),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&motor_io));
    set_motors(BOT_STOP);
    gpio_set_level(STATUS_LED_GPIO, 1);

    // Cấu hình chân Echo và Nút Boot
    gpio_config_t in_io = {
        .pin_bit_mask = (1ULL << ULTRASONIC_ECHO) | (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&in_io));

    // Khởi tạo Servo PWM
    servo_init();
    servo_set_angle(0); // Đóng nắp thùng hàng

    // Khởi chạy task an toàn & đo khoảng cách
    xTaskCreate(robot_safety_task, "robot_safe", 2048, NULL, 5, NULL);

    // Đăng ký lệnh điều khiển với InnoEdge Command Bus
    ESP_ERROR_CHECK(innoedge_register_command("bot_move", cmd_bot_move));
    ESP_ERROR_CHECK(innoedge_register_command("bot_servo", cmd_bot_servo));
    ESP_ERROR_CHECK(innoedge_register_command("bot_status", cmd_bot_status));

    ESP_ERROR_CHECK(innoedge_start());
    ESP_LOGI(TAG, "InnoEdge STEM Delivery Robot đã sẵn sàng!");

    // Nút bấm BOOT mô phỏng khách gọi xe giao hàng (10.000 đ)
    bool was_down = false;
    while (true) {
        bool down = (gpio_get_level(BOOT_BUTTON_GPIO) == 0);
        if (down && !was_down) {
            ESP_LOGI(TAG, "Khách ấn nút gọi xe giao hàng (10.000 đ) -> Tạo VietQR...");
            esp_err_t err = innoedge_request_qr(10000);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Chưa tạo được QR: %s (cần online)", esp_err_to_name(err));
            }
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
