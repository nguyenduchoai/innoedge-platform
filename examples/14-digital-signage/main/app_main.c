// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// 14 — Bảng Quảng Cáo Thông Minh (Digital Signage & Smart Billboard)
//
// Ứng dụng thương mại: Màn hình quảng cáo tại thang máy, cửa hàng tiện lợi, cây xăng.
// - Quản lý chiến dịch tập trung: nhận danh sách phát (playlist) động từ Cloud.
// - Bằng chứng phát sóng (Proof of Play): ghi nhận số lần chiếu và gửi telemetry lên Cloud.
// - Chế độ thông báo khẩn cấp: ngắt quảng cáo để hiển thị cảnh báo an ninh, báo cháy.
// - Doanh thu tự phục vụ: khách quét VietQR (50.000 đ) để mua slot chiếu lời chúc / quảng cáo mini!

#include "innoedge.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "signage";

#define BOOT_BUTTON_GPIO    0
#define STATUS_LED_GPIO     2
#define MAX_SLOTS           8

typedef struct {
    char title[64];
    char text[128];
    int duration_sec;
    int play_count;
} ad_slot_t;

static ad_slot_t s_playlist[MAX_SLOTS] = {
    { .title = "Khuyến Mãi", .text = "Siêu Sale Mùa Hè - Giảm 50% toàn bộ sản phẩm!", .duration_sec = 6, .play_count = 0 },
    { .title = "Trà Sữa Oolong", .text = "Mua 1 Tặng 1 từ 14h - 18h mỗi ngày tại quầy!", .duration_sec = 5, .play_count = 0 },
    { .title = "Thanh Toán Số", .text = "Chấp nhận VietQR / Napas 247 tiện lợi!", .duration_sec = 5, .play_count = 0 }
};
static int s_playlist_count = 3;
static int s_current_index = 0;

static bool s_emergency_active = false;
static char s_emergency_msg[128] = "";

static void render_screen(const char *header, const char *body, const char *footer)
{
    ESP_LOGI(TAG, "╔══════════════════════════════════════════════════════════════╗");
    ESP_LOGI(TAG, "║ [%s] %-52s ║", s_emergency_active ? "KHẨN CẤP" : "QUẢNG CÁO", header);
    ESP_LOGI(TAG, "║                                                              ║");
    ESP_LOGI(TAG, "║   >> %-55s ║", body);
    ESP_LOGI(TAG, "║                                                              ║");
    if (footer && strlen(footer) > 0) {
        ESP_LOGI(TAG, "║   [QR/INFO]: %-48s ║", footer);
    }
    ESP_LOGI(TAG, "╚══════════════════════════════════════════════════════════════╝");
}

// Task nền phát sóng vòng lặp bảng quảng cáo
static void signage_loop_task(void *arg)
{
    (void)arg;
    while (true) {
        if (s_emergency_active) {
            // Khi có cảnh báo khẩn cấp: dừng mọi quảng cáo, phát liên tục thông điệp an toàn
            render_screen("CẢNH BÁO AN TOÀN", s_emergency_msg, "Lối thoát hiểm ở cửa phía Đông");
            gpio_set_level(STATUS_LED_GPIO, (xTaskGetTickCount() / 500) % 2); // Chớp đèn liên tục
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        if (s_playlist_count > 0) {
            ad_slot_t *slot = &s_playlist[s_current_index];
            render_screen(slot->title, slot->text, "Quét VietQR để mua lượt quảng cáo");
            gpio_set_level(STATUS_LED_GPIO, 1);

            // Chờ hết thời lượng chiếu slot
            vTaskDelay(pdMS_TO_TICKS(slot->duration_sec * 1000));

            slot->play_count++;

            // Báo cáo Proof of Play (POW) lên Cloud để đối soát doanh thu quảng cáo
            char pop[160];
            snprintf(pop, sizeof(pop), "{\"ad_title\":\"%s\",\"duration\":%d,\"total_views\":%d}",
                     slot->title, slot->duration_sec, slot->play_count);
            innoedge_publish_event("proof_of_play", pop);

            s_current_index = (s_current_index + 1) % s_playlist_count;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }
}

// ── InnoEdge Event Handlers ──────────────────────────────────────────────────

static void on_qr(const char *payload, int64_t amount_vnd, const char *ref_code,
                  int expires_sec, int64_t intent_id)
{
    ESP_LOGI(TAG, "Mã VietQR Mua Lượt Phát Sóng: %lld đ | Ref: %s (Hết hạn: %ds, intent=%lld)",
             (long long)amount_vnd, ref_code, expires_sec, (long long)intent_id);
    render_screen("MUA SLOT QUẢNG CÁO TỰ PHỤC VỤ (50.000 đ)", "Quét mã để hiển thị lời nhắn của bạn lên bảng LED!", payload);
}

static void on_paid(int64_t intent_id, int64_t amount_vnd)
{
    ESP_LOGI(TAG, "✓ XÁC NHẬN TIỀN VỀ: %lld đ (intent=%lld) -> KÍCH HOẠT QUẢNG CÁO CỦA KHÁCH!",
             (long long)amount_vnd, (long long)intent_id);

    // Chèn nội dung của khách vừa thanh toán vào danh sách ưu tiên
    if (s_playlist_count < MAX_SLOTS) {
        ad_slot_t *user_ad = &s_playlist[s_playlist_count];
        snprintf(user_ad->title, sizeof(user_ad->title), "Khách Hàng #%lld", (long long)intent_id);
        snprintf(user_ad->text, sizeof(user_ad->text), "Chúc mừng sinh nhật! Chúc bạn ngập tràn niềm vui!");
        user_ad->duration_sec = 8;
        user_ad->play_count = 0;
        s_current_index = s_playlist_count; // Phát ngay lập tức
        s_playlist_count++;
    }

    innoedge_publish_event("ad_slot_activated", "{\"status\":\"active\",\"slot_type\":\"premium\"}");
}

// ── InnoEdge Remote Command Handlers ─────────────────────────────────────────

// {"action":"signage_show_text", "params":{"title":"Thông Báo", "text":"Khuyến mãi 30%", "duration":5}}
static esp_err_t cmd_show_text(cJSON *params, char *result, size_t result_len,
                               char *msg, size_t msg_len)
{
    cJSON *title = params ? cJSON_GetObjectItem(params, "title") : NULL;
    cJSON *text = params ? cJSON_GetObjectItem(params, "text") : NULL;
    cJSON *dur = params ? cJSON_GetObjectItem(params, "duration") : NULL;

    const char *t_str = cJSON_IsString(title) ? title->valuestring : "Tin Mới";
    const char *b_str = cJSON_IsString(text) ? text->valuestring : "";
    int d = cJSON_IsNumber(dur) ? dur->valueint : 5;

    if (s_playlist_count < MAX_SLOTS) {
        ad_slot_t *slot = &s_playlist[s_playlist_count];
        strncpy(slot->title, t_str, sizeof(slot->title) - 1);
        strncpy(slot->text, b_str, sizeof(slot->text) - 1);
        slot->duration_sec = d;
        slot->play_count = 0;
        s_playlist_count++;
    }

    snprintf(result, result_len, "{\"added\":true,\"total_slots\":%d}", s_playlist_count);
    snprintf(msg, msg_len, "da them quang cao vao danh sach phat");
    return ESP_OK;
}

// {"action":"signage_emergency", "params":{"message":"BÁO CHÁY: Di tản ngay!"}}
static esp_err_t cmd_emergency(cJSON *params, char *result, size_t result_len,
                               char *msg, size_t msg_len)
{
    cJSON *m = params ? cJSON_GetObjectItem(params, "message") : NULL;
    const char *str = cJSON_IsString(m) ? m->valuestring : "KHẨN CẤP: Chú ý an toàn!";

    s_emergency_active = true;
    strncpy(s_emergency_msg, str, sizeof(s_emergency_msg) - 1);

    snprintf(result, result_len, "{\"emergency\":true}");
    snprintf(msg, msg_len, "da kich hoat che do khan cap");
    return ESP_OK;
}

// {"action":"signage_clear_emergency"}
static esp_err_t cmd_clear_emergency(cJSON *params, char *result, size_t result_len,
                                     char *msg, size_t msg_len)
{
    (void)params;
    s_emergency_active = false;
    snprintf(result, result_len, "{\"emergency\":false}");
    snprintf(msg, msg_len, "da tro ve che do phat binh thuong");
    return ESP_OK;
}

// {"action":"signage_status"}
static esp_err_t cmd_status(cJSON *params, char *result, size_t result_len,
                            char *msg, size_t msg_len)
{
    (void)params;
    snprintf(result, result_len,
             "{\"device\":\"%s\",\"emergency\":%s,\"total_slots\":%d,\"current_slot\":%d}",
             innoedge_device_id(), s_emergency_active ? "true" : "false",
             s_playlist_count, s_current_index);
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
        .fw_version = "1.0.0",
        .events = &events,
    };

    ESP_ERROR_CHECK(innoedge_init(&cfg));

    gpio_config_t out_io = {
        .pin_bit_mask = (1ULL << STATUS_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&out_io));

    gpio_config_t btn_io = {
        .pin_bit_mask = (1ULL << BOOT_BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_io));

    // Khởi chạy task vòng lặp phát sóng
    xTaskCreate(signage_loop_task, "signage_loop", 3072, NULL, 4, NULL);

    // Đăng ký lệnh với InnoEdge Command Bus
    ESP_ERROR_CHECK(innoedge_register_command("signage_show_text", cmd_show_text));
    ESP_ERROR_CHECK(innoedge_register_command("signage_emergency", cmd_emergency));
    ESP_ERROR_CHECK(innoedge_register_command("signage_clear_emergency", cmd_clear_emergency));
    ESP_ERROR_CHECK(innoedge_register_command("signage_status", cmd_status));

    ESP_ERROR_CHECK(innoedge_start());
    ESP_LOGI(TAG, "Bảng Quảng Cáo Kỹ Thuật Số (Digital Signage) đã sẵn sàng!");

    // Nút BOOT mô phỏng khách quét QR mua slot phát quảng cáo (50.000 đ)
    bool was_down = false;
    while (true) {
        bool down = (gpio_get_level(BOOT_BUTTON_GPIO) == 0);
        if (down && !was_down) {
            ESP_LOGI(TAG, "Khách ấn nút mua lượt phát sóng (50.000 đ) -> Tạo VietQR...");
            esp_err_t err = innoedge_request_qr(50000);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Chưa tạo được QR: %s", esp_err_to_name(err));
            }
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
