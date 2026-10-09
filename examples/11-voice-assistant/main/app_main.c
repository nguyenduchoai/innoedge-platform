// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

// 11 — Voice Assistant: nói với máy, máy trả lời bằng giọng. Kiểu Xiaozhi,
// trên InnoEdge core.
//
// Giữ BOOT để nói, thả ra để gửi. Máy: thu mic → PCM → cloud (ASR → Claude →
// TTS) → PCM về → loa. LED sáng khi máy đang nói.
//
// Toàn bộ audio nằm trong innoedge_audio (driver mẫu). SDK core chỉ thêm đúng
// hai thứ để việc này thành sự thật: kênh binary và callback frame lạ. Đây là
// cách một nền tảng "đa năng" giữ được lõi nhỏ: tính năng mới là component
// mới cắm vào, không phải SDK phình ra.

#include "innoedge.h"
#include "innoedge_audio.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "voice";

#define BOOT_BUTTON_GPIO 0
#define LED_GPIO         2

static void on_stt(const char *text)
{
    ESP_LOGI(TAG, "🎤 nghe được: \"%s\"", text);
}

static void on_tts(bool playing)
{
    gpio_set_level(LED_GPIO, playing);
    ESP_LOGI(TAG, "%s", playing ? "🔊 máy đang nói…" : "🔇 xong");
}

void app_main(void)
{
    static const innoedge_events_t events = {
        // Hai callback này là toàn bộ chỗ SDK "chạm" vào audio.
        .on_frame = innoedge_audio_on_frame,
        .on_binary = innoedge_audio_on_binary,
    };
    innoedge_config_t cfg = { .events = &events };
    ESP_ERROR_CHECK(innoedge_init(&cfg));

    static const innoedge_audio_events_t audio_events = { .on_stt = on_stt, .on_tts = on_tts };
    ESP_ERROR_CHECK(innoedge_audio_init(&audio_events));

    gpio_config_t led = { .pin_bit_mask = 1ULL << LED_GPIO, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&led));
    gpio_config_t btn = {
        .pin_bit_mask = 1ULL << BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn));

    ESP_ERROR_CHECK(innoedge_start());
    ESP_LOGI(TAG, "sẵn sàng — chạy `mock-cloud -ai -voice`, GIỮ nút BOOT để nói");

    // Push-to-talk. Không wake word, không VAD — thêm khi cần, không phải bây giờ.
    bool was_down = false;
    while (true) {
        bool down = gpio_get_level(BOOT_BUTTON_GPIO) == 0;
        if (down && !was_down) {
            esp_err_t err = innoedge_audio_listen_start();
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "chưa nói được: %s (máy offline hoặc chưa có mic)", esp_err_to_name(err));
            }
        } else if (!down && was_down) {
            innoedge_audio_listen_stop();
        }
        was_down = down;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
