// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// 12 — Meta Muse Gadget: Kiosk AI Avatar & Bán Hàng Tự Động
//
// Kết hợp Meta Muse Gadget SDK (facebookincubator/muse-gadget-sdk) với InnoEdge:
// - Meta Muse : xử lý nhận diện giọng nói, giao tiếp tự nhiên và Avatar tương tác.
// - InnoEdge  : hạ tầng thanh toán VietQR (SePAY/PayOS/Pay2S/Tingee), sổ cái bền NVS,
//               chống trùng lệnh điều khiển relay và bảo vệ phần cứng.
//
// Luồng hoạt động:
// 1. Khách nói với máy: "Cho tôi 1 ly cà phê sữa đá"
// 2. Muse Agent cloud phân tích ý định -> gọi lệnh "request_payment" (25.000 đ)
// 3. InnoEdge xin VietQR động từ cloud -> on_qr() vẽ mã QR lên màn hình cạnh Avatar
// 4. Khách quét QR chuyển khoản -> Webhook ngân hàng báo về cloud -> on_paid()
// 5. Firmware kích relay rót cà phê, sổ cái NVS ghi nhận, Avatar cảm ơn khách!

#include "innoedge.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "muse-gadget";

#define BOOT_BUTTON_GPIO    0
#define STATUS_LED_GPIO     2
#define RELAY_CH1_GPIO      4  // Kênh 1: Cà phê đen (20.000 đ)
#define RELAY_CH2_GPIO      5  // Kênh 2: Cà phê sữa (25.000 đ)
#define RELAY_MAX_SECONDS   10 // Trần an toàn phần cứng: tối đa 10s tự ngắt

// Trạng thái Avatar Meta Muse
typedef enum {
    MUSE_AVATAR_IDLE = 0,
    MUSE_AVATAR_LISTENING,
    MUSE_AVATAR_THINKING,
    MUSE_AVATAR_PAYMENT_PENDING,
    MUSE_AVATAR_DISPENSING,
    MUSE_AVATAR_HAPPY,
    MUSE_AVATAR_ERROR
} muse_avatar_state_t;

static volatile muse_avatar_state_t s_avatar_state = MUSE_AVATAR_IDLE;
static volatile int s_relay1_remaining_sec = 0;
static volatile int s_relay2_remaining_sec = 0;

static const char *avatar_state_str(muse_avatar_state_t st)
{
    switch (st) {
    case MUSE_AVATAR_IDLE:            return "IDLE (Chờ khách)";
    case MUSE_AVATAR_LISTENING:       return "LISTENING (Đang lắng nghe)";
    case MUSE_AVATAR_THINKING:        return "THINKING (Đang suy nghĩ)";
    case MUSE_AVATAR_PAYMENT_PENDING: return "PAYMENT_PENDING (Chờ quét VietQR)";
    case MUSE_AVATAR_DISPENSING:      return "DISPENSING (Đang nhả hàng / rót)";
    case MUSE_AVATAR_HAPPY:           return "HAPPY (Cảm ơn quý khách!)";
    case MUSE_AVATAR_ERROR:           return "ERROR (Sự cố)";
    default:                          return "UNKNOWN";
    }
}

static void update_avatar(muse_avatar_state_t st, const char *caption)
{
    s_avatar_state = st;
    ESP_LOGI(TAG, "🤖 [MUSE AVATAR] -> %s | \"%s\"", avatar_state_str(st), caption ? caption : "");
    // Với bo có màn hình ST7789/AMOLED: gọi hàm vẽ avatar tại đây.
}

// Task nền quản lý relay: không bao giờ block task WebSocket của SDK
static void relay_safety_task(void *arg)
{
    (void)arg;
    while (true) {
        // Kênh 1
        if (s_relay1_remaining_sec > 0) {
            gpio_set_level(RELAY_CH1_GPIO, 1);
            s_relay1_remaining_sec--;
            if (s_relay1_remaining_sec == 0) {
                gpio_set_level(RELAY_CH1_GPIO, 0);
                ESP_LOGI(TAG, "Relay Kênh 1 đã tự ngắt an toàn.");
                update_avatar(MUSE_AVATAR_HAPPY, "Đã chuẩn bị xong món Cà phê đen!");
            }
        } else {
            gpio_set_level(RELAY_CH1_GPIO, 0);
        }

        // Kênh 2
        if (s_relay2_remaining_sec > 0) {
            gpio_set_level(RELAY_CH2_GPIO, 1);
            s_relay2_remaining_sec--;
            if (s_relay2_remaining_sec == 0) {
                gpio_set_level(RELAY_CH2_GPIO, 0);
                ESP_LOGI(TAG, "Relay Kênh 2 đã tự ngắt an toàn.");
                update_avatar(MUSE_AVATAR_HAPPY, "Đã chuẩn bị xong món Cà phê sữa!");
            }
        } else {
            gpio_set_level(RELAY_CH2_GPIO, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ── InnoEdge Event Handlers ──────────────────────────────────────────────────

static void on_qr(const char *payload, int64_t amount_vnd, const char *ref_code,
                  int expires_sec, int64_t intent_id)
{
    ESP_LOGI(TAG, "Mã VietQR đã sẵn sàng: %lld đ | Ref: %s | Hết hạn: %ds | Intent: %lld",
             (long long)amount_vnd, ref_code, expires_sec, (long long)intent_id);
    ESP_LOGI(TAG, "QR Payload: %s", payload);

    char caption[96];
    snprintf(caption, sizeof(caption), "Mời bạn quét VietQR %lld đ để nhận món", (long long)amount_vnd);
    update_avatar(MUSE_AVATAR_PAYMENT_PENDING, caption);
}

static void on_qr_error(const char *message)
{
    ESP_LOGE(TAG, "Không thể tạo mã VietQR: %s", message ? message : "(không rõ lỗi)");
    update_avatar(MUSE_AVATAR_ERROR, "Cổng thanh toán đang bảo trì, vui lòng thử lại!");
}

// ĐÂY là điểm DUY NHẤT được phép nhả hàng: ngân hàng đã xác nhận tiền về qua Webhook.
static void on_paid(int64_t intent_id, int64_t amount_vnd)
{
    ESP_LOGI(TAG, "✓ XÁC NHẬN TIỀN VỀ: %lld đ (intent=%lld)", (long long)amount_vnd, (long long)intent_id);

    update_avatar(MUSE_AVATAR_DISPENSING, "Thanh toán thành công! Đang pha chế...");

    // Phân loại món theo số tiền thanh toán
    if (amount_vnd <= 20000) {
        ESP_LOGI(TAG, "Kích hoạt Kênh 1 (Cà phê đen) trong 5 giây");
        s_relay1_remaining_sec = 5;
    } else {
        ESP_LOGI(TAG, "Kích hoạt Kênh 2 (Cà phê sữa) trong 7 giây");
        s_relay2_remaining_sec = 7;
    }

    // Báo sự kiện lên InnoEdge Cloud & Muse Agent
    char data[128];
    snprintf(data, sizeof(data), "{\"intent_id\":%lld,\"amount_vnd\":%lld,\"status\":\"dispensing\"}",
             (long long)intent_id, (long long)amount_vnd);
    innoedge_publish_event("muse_sale_completed", data);
}

// ── InnoEdge Remote Command Handlers ─────────────────────────────────────────

// Lệnh điều khiển Avatar Meta Muse từ cloud AI:
// {"action":"muse_avatar", "params":{"mood":"thinking", "caption":"Đang tìm món cho bạn..."}}
static esp_err_t cmd_muse_avatar(cJSON *params, char *result, size_t result_len,
                                 char *msg, size_t msg_len)
{
    cJSON *mood = params ? cJSON_GetObjectItem(params, "mood") : NULL;
    cJSON *caption = params ? cJSON_GetObjectItem(params, "caption") : NULL;

    const char *mood_str = cJSON_IsString(mood) ? mood->valuestring : "idle";
    const char *cap_str = cJSON_IsString(caption) ? caption->valuestring : "";

    muse_avatar_state_t st = MUSE_AVATAR_IDLE;
    if (strcmp(mood_str, "listening") == 0) st = MUSE_AVATAR_LISTENING;
    else if (strcmp(mood_str, "thinking") == 0) st = MUSE_AVATAR_THINKING;
    else if (strcmp(mood_str, "happy") == 0) st = MUSE_AVATAR_HAPPY;
    else if (strcmp(mood_str, "dispense") == 0) st = MUSE_AVATAR_DISPENSING;

    update_avatar(st, cap_str);
    snprintf(result, result_len, "{\"mood\":\"%s\",\"updated\":true}", mood_str);
    snprintf(msg, msg_len, "ok");
    return ESP_OK;
}

// Lệnh nhả hàng thủ công hoặc từ AI Agent:
// {"action":"dispense", "params":{"channel":1, "seconds":5}}
static esp_err_t cmd_dispense(cJSON *params, char *result, size_t result_len,
                              char *msg, size_t msg_len)
{
    cJSON *ch_item = params ? cJSON_GetObjectItem(params, "channel") : NULL;
    cJSON *sec_item = params ? cJSON_GetObjectItem(params, "seconds") : NULL;

    int ch = cJSON_IsNumber(ch_item) ? ch_item->valueint : 1;
    int sec = cJSON_IsNumber(sec_item) ? sec_item->valueint : 3;

    if (sec <= 0) sec = 1;
    if (sec > RELAY_MAX_SECONDS) sec = RELAY_MAX_SECONDS; // Trần an toàn phần cứng

    if (ch == 1) {
        s_relay1_remaining_sec = sec;
    } else if (ch == 2) {
        s_relay2_remaining_sec = sec;
    } else {
        snprintf(msg, msg_len, "kenh khong hop le (1 hoac 2)");
        return ESP_ERR_INVALID_ARG;
    }

    update_avatar(MUSE_AVATAR_DISPENSING, "Đang nhả hàng theo lệnh...");
    snprintf(result, result_len, "{\"channel\":%d,\"seconds\":%d,\"running\":true}", ch, sec);
    snprintf(msg, msg_len, "da kich hoat relay kenh %d", ch);
    return ESP_OK;
}

// Lệnh xin mã VietQR qua Command Bus:
// {"action":"request_payment", "params":{"amount_vnd":25000, "item_name":"Cà phê sữa đá"}}
static esp_err_t cmd_request_payment(cJSON *params, char *result, size_t result_len,
                                     char *msg, size_t msg_len)
{
    cJSON *amt_item = params ? cJSON_GetObjectItem(params, "amount_vnd") : NULL;
    int64_t amt = cJSON_IsNumber(amt_item) ? (int64_t)amt_item->valuedouble : 20000;

    esp_err_t err = innoedge_request_qr(amt);
    if (err != ESP_OK) {
        snprintf(msg, msg_len, "loi request_qr: %s", esp_err_to_name(err));
        return err;
    }

    snprintf(result, result_len, "{\"amount_vnd\":%lld,\"requested\":true}", (long long)amt);
    snprintf(msg, msg_len, "da gui yeu cau tao VietQR");
    return ESP_OK;
}

// Lệnh kiểm tra tình trạng máy: {"action":"status"}
static esp_err_t cmd_status(cJSON *params, char *result, size_t result_len,
                            char *msg, size_t msg_len)
{
    (void)params;
    snprintf(result, result_len,
             "{\"device\":\"%s\",\"avatar_state\":\"%s\",\"relay1_busy\":%s,\"relay2_busy\":%s}",
             innoedge_device_id(), avatar_state_str(s_avatar_state),
             s_relay1_remaining_sec > 0 ? "true" : "false",
             s_relay2_remaining_sec > 0 ? "true" : "false");
    snprintf(msg, msg_len, "ok");
    return ESP_OK;
}

void app_main(void)
{
    static const innoedge_events_t events = {
        .on_qr = on_qr,
        .on_qr_error = on_qr_error,
        .on_paid = on_paid,
    };
    innoedge_config_t cfg = {
        .events = &events,
    };

    ESP_ERROR_CHECK(innoedge_init(&cfg));

    // Cấu hình chân GPIO phần cứng
    gpio_config_t out_io = {
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO) | (1ULL << RELAY_CH1_GPIO) | (1ULL << RELAY_CH2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out_io));
    gpio_set_level(STATUS_LED_GPIO, 1);
    gpio_set_level(RELAY_CH1_GPIO, 0);
    gpio_set_level(RELAY_CH2_GPIO, 0);

    gpio_config_t btn_io = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_io));

    // Khởi chạy task an toàn relay
    xTaskCreate(relay_safety_task, "relay_safe", 2048, NULL, 5, NULL);

    // Đăng ký các năng lực với InnoEdge Command Bus
    ESP_ERROR_CHECK(innoedge_register_command("muse_avatar", cmd_muse_avatar));
    ESP_ERROR_CHECK(innoedge_register_command("dispense", cmd_dispense));
    ESP_ERROR_CHECK(innoedge_register_command("request_payment", cmd_request_payment));
    ESP_ERROR_CHECK(innoedge_register_command("status", cmd_status));

    ESP_ERROR_CHECK(innoedge_start());
    ESP_LOGI(TAG, "Meta Muse Gadget AI Kiosk đã sẵn sàng!");
    update_avatar(MUSE_AVATAR_IDLE, "Xin chào! Bấm nút BOOT hoặc ra lệnh bằng giọng nói để chọn món.");

    // Vòng lặp nút bấm mô phỏng khách gọi món Cà phê sữa đá (25.000 đ)
    bool was_down = false;
    while (true) {
        bool down = (gpio_get_level(BOOT_BUTTON_GPIO) == 0);
        if (down && !was_down) {
            ESP_LOGI(TAG, "Khách ấn nút chọn món: Cà phê sữa đá (25.000 đ) -> Tạo VietQR...");
            update_avatar(MUSE_AVATAR_THINKING, "Đang khởi tạo giao dịch VietQR 25.000 đ...");
            esp_err_t err = innoedge_request_qr(25000);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Chưa tạo được QR: %s (cần kết nối online với cloud)", esp_err_to_name(err));
                update_avatar(MUSE_AVATAR_ERROR, "Thiết bị chưa online. Vui lòng thử lại!");
            }
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
