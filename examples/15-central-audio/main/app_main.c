// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// 15 — Hệ Thống Âm Thanh & Loa Thông Báo Tập Trung (Centralized Multi-Zone IP Audio)
//
// Ứng dụng thương mại: Chuỗi siêu thị, trường học, tòa nhà, nhà xưởng, đô thị thông minh.
// - Phát nhạc nền BGM (Background Music) liên tục từ Cloud.
// - Phân vùng âm thanh (Multi-Zone): phát riêng từng tầng hoặc phát toàn khu vực (All Zones).
// - Ngắt ưu tiên thông báo khẩn cấp (Priority Paging): tự động giảm hoặc ngắt nhạc nền khi có
//   thông báo thoại hoặc còi báo cháy, sau đó tự động tiếp tục phát nhạc nền.
// - Dịch vụ âm nhạc theo yêu cầu (Jukebox): khách quét VietQR (10.000 đ) để order bài hát!

#include "innoedge.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "audio-pa";

#define BOOT_BUTTON_GPIO    0
#define STATUS_LED_GPIO     2
#define DEFAULT_ZONE        1

typedef enum {
    AUDIO_MODE_IDLE = 0,
    AUDIO_MODE_BGM,         // Nhạc nền du dương
    AUDIO_MODE_PAGING,      // Thông báo thoại ưu tiên
    AUDIO_MODE_EMERGENCY    // Còi báo cháy khẩn cấp
} audio_mode_t;

static volatile audio_mode_t s_audio_mode = AUDIO_MODE_BGM;
static volatile int s_volume = 45; // 0 - 100%
static volatile int s_current_zone = DEFAULT_ZONE;
static volatile int s_paging_remaining_sec = 0;
static char s_current_bgm[64] = "Acoustic Cafe Chill Playlist";
static char s_current_announcement[128] = "";

static const char *audio_mode_str(audio_mode_t m)
{
    switch (m) {
    case AUDIO_MODE_IDLE:      return "IDLE (Nghỉ)";
    case AUDIO_MODE_BGM:       return "BGM (Nhạc nền)";
    case AUDIO_MODE_PAGING:    return "PAGING (Thông báo thoại)";
    case AUDIO_MODE_EMERGENCY: return "EMERGENCY (Báo động khẩn cấp)";
    default:                   return "UNKNOWN";
    }
}

static void log_audio_state(void)
{
    ESP_LOGI(TAG, "🔊 [ZONE %d] Chế độ: %s | Âm lượng: %d%%",
             s_current_zone, audio_mode_str(s_audio_mode), s_volume);
    if (s_audio_mode == AUDIO_MODE_BGM) {
        ESP_LOGI(TAG, "   🎵 Đang phát nhạc nền: \"%s\"", s_current_bgm);
    } else if (s_audio_mode == AUDIO_MODE_PAGING) {
        ESP_LOGI(TAG, "   🗣️ Đang phát thông báo: \"%s\" (%ds còn lại)", s_current_announcement, s_paging_remaining_sec);
    } else if (s_audio_mode == AUDIO_MODE_EMERGENCY) {
        ESP_LOGE(TAG, "   🚨🚨 CÒI BÁO CHÁY ĐANG HÚ LIÊN TỤC! 🚨🚨");
    }
}

// Task nền giám sát thời lượng thông báo và trả về nhạc nền khi hết giờ
static void audio_manager_task(void *arg)
{
    (void)arg;
    while (true) {
        if (s_audio_mode == AUDIO_MODE_PAGING) {
            gpio_set_level(STATUS_LED_GPIO, 1);
            if (s_paging_remaining_sec > 0) {
                s_paging_remaining_sec--;
                if (s_paging_remaining_sec == 0) {
                    ESP_LOGI(TAG, "Thông báo kết thúc -> Tự động khôi phục phát nhạc nền BGM.");
                    s_audio_mode = AUDIO_MODE_BGM;
                    s_volume = 45; // Trả về âm lượng chuẩn
                    log_audio_state();
                }
            }
        } else if (s_audio_mode == AUDIO_MODE_EMERGENCY) {
            // Chớp đèn còi cảnh báo
            gpio_set_level(STATUS_LED_GPIO, (xTaskGetTickCount() / 250) % 2);
        } else {
            gpio_set_level(STATUS_LED_GPIO, 0);
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// ── InnoEdge Event Handlers ──────────────────────────────────────────────────

static void on_qr(const char *payload, int64_t amount_vnd, const char *ref_code,
                  int expires_sec, int64_t intent_id)
{
    ESP_LOGI(TAG, "Mã VietQR Chọn Bài Hát Theo Yêu Cầu (Jukebox): %lld đ | Ref: %s (Hạn: %ds, intent=%lld)",
             (long long)amount_vnd, ref_code, expires_sec, (long long)intent_id);
    ESP_LOGI(TAG, "Quét mã QR để ưu tiên phát bài hát của bạn: %s", payload);
}

static void on_paid(int64_t intent_id, int64_t amount_vnd)
{
    ESP_LOGI(TAG, "✓ XÁC NHẬN TIỀN VỀ: %lld đ (intent=%lld) -> XẾP BÀI HÁT VÀO HÀNG ĐỢI PHÁT!",
             (long long)amount_vnd, (long long)intent_id);

    // Kích hoạt bài hát theo yêu cầu của khách
    snprintf(s_current_bgm, sizeof(s_current_bgm), "Yêu Cầu #%lld: Bài Ca Tình Yêu", (long long)intent_id);
    s_audio_mode = AUDIO_MODE_BGM;
    s_volume = 60; // Tăng âm lượng nhẹ
    log_audio_state();

    innoedge_publish_event("jukebox_track_played", "{\"status\":\"playing\",\"source\":\"paid_request\"}");
}

// ── InnoEdge Remote Command Handlers ─────────────────────────────────────────

// Phát thông báo ưu tiên ngắt nhạc nền:
// {"action":"audio_announce", "params":{"message":"Kính mời khách hàng tới quầy 2", "seconds":6, "zone":1}}
static esp_err_t cmd_announce(cJSON *params, char *result, size_t result_len,
                              char *msg, size_t msg_len)
{
    cJSON *z = params ? cJSON_GetObjectItem(params, "zone") : NULL;
    int target_zone = cJSON_IsNumber(z) ? z->valueint : 0; // 0 = all zones

    if (target_zone != 0 && target_zone != s_current_zone) {
        snprintf(msg, msg_len, "bo qua: lenh gui cho zone %d, may o zone %d", target_zone, s_current_zone);
        return ESP_OK; // Không phải vùng của loa này
    }

    cJSON *m = params ? cJSON_GetObjectItem(params, "message") : NULL;
    cJSON *sec = params ? cJSON_GetObjectItem(params, "seconds") : NULL;

    const char *text = cJSON_IsString(m) ? m->valuestring : "Thông báo chung";
    int s = cJSON_IsNumber(sec) ? sec->valueint : 5;

    ESP_LOGW(TAG, "🔔 [DING-DONG] NGẮT NHẠC NỀN -> PHÁT THÔNG BÁO ƯU TIÊN!");
    strncpy(s_current_announcement, text, sizeof(s_current_announcement) - 1);
    s_audio_mode = AUDIO_MODE_PAGING;
    s_volume = 85; // Âm lượng thông báo lớn
    s_paging_remaining_sec = s;

    log_audio_state();

    snprintf(result, result_len, "{\"status\":\"announced\",\"duration\":%d,\"zone\":%d}", s, s_current_zone);
    snprintf(msg, msg_len, "da phat thong bao uu tien");
    return ESP_OK;
}

// Cảnh báo cháy khẩn cấp: {"action":"audio_emergency", "params":{"active":true}}
static esp_err_t cmd_emergency(cJSON *params, char *result, size_t result_len,
                               char *msg, size_t msg_len)
{
    cJSON *act = params ? cJSON_GetObjectItem(params, "active") : NULL;
    bool active = cJSON_IsBool(act) ? cJSON_IsTrue(act) : true;

    if (active) {
        s_audio_mode = AUDIO_MODE_EMERGENCY;
        s_volume = 100; // Tối đa 100%
        ESP_LOGE(TAG, "🚨 KÍCH HOẠT HỆ THỐNG CÒI BÁO CHÁY TẬP TRUNG TOÀN KHU VỰC!");
    } else {
        s_audio_mode = AUDIO_MODE_BGM;
        s_volume = 45;
        ESP_LOGI(TAG, "Đã hủy chế độ khẩn cấp, trở về phát nhạc nền.");
    }

    log_audio_state();
    snprintf(result, result_len, "{\"emergency\":%s}", active ? "true" : "false");
    snprintf(msg, msg_len, "ok");
    return ESP_OK;
}

// Điều chỉnh âm lượng: {"action":"audio_set_volume", "params":{"volume":65}}
static esp_err_t cmd_set_volume(cJSON *params, char *result, size_t result_len,
                                char *msg, size_t msg_len)
{
    cJSON *v = params ? cJSON_GetObjectItem(params, "volume") : NULL;
    int vol = cJSON_IsNumber(v) ? v->valueint : 50;
    if (vol < 0) vol = 0;
    if (vol > 100) vol = 100;

    s_volume = vol;
    log_audio_state();

    snprintf(result, result_len, "{\"volume\":%d}", vol);
    snprintf(msg, msg_len, "da dat am luong %d%%", vol);
    return ESP_OK;
}

// Kiểm tra trạng thái: {"action":"audio_status"}
static esp_err_t cmd_status(cJSON *params, char *result, size_t result_len,
                            char *msg, size_t msg_len)
{
    (void)params;
    snprintf(result, result_len,
             "{\"device\":\"%s\",\"zone\":%d,\"mode\":\"%s\",\"volume\":%d,\"bgm\":\"%s\"}",
             innoedge_device_id(), s_current_zone, audio_mode_str(s_audio_mode), s_volume, s_current_bgm);
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

    // Khởi chạy task quản lý chuyển đổi chế độ âm thanh
    xTaskCreate(audio_manager_task, "audio_mgr", 3072, NULL, 5, NULL);

    // Đăng ký lệnh với InnoEdge Command Bus
    ESP_ERROR_CHECK(innoedge_register_command("audio_announce", cmd_announce));
    ESP_ERROR_CHECK(innoedge_register_command("audio_emergency", cmd_emergency));
    ESP_ERROR_CHECK(innoedge_register_command("audio_set_volume", cmd_set_volume));
    ESP_ERROR_CHECK(innoedge_register_command("audio_status", cmd_status));

    ESP_ERROR_CHECK(innoedge_start());
    ESP_LOGI(TAG, "Hệ Thống Âm Thanh Thông Báo Tập Trung (Central Audio) Zone %d sẵn sàng!", s_current_zone);
    log_audio_state();

    // Nút BOOT mô phỏng khách quét QR order bài hát (10.000 đ)
    bool was_down = false;
    while (true) {
        bool down = (gpio_get_level(BOOT_BUTTON_GPIO) == 0);
        if (down && !was_down) {
            ESP_LOGI(TAG, "Khách ấn nút chọn bài hát Jukebox (10.000 đ) -> Tạo VietQR...");
            esp_err_t err = innoedge_request_qr(10000);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Chưa tạo được QR: %s", esp_err_to_name(err));
            }
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
