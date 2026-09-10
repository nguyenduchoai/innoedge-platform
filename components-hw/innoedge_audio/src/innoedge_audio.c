// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge

#include "innoedge_audio.h"

#include "innoedge.h"
#include "gtek_ws_client.h" // API nội bộ SDK — driver mẫu được phép, app thì không
#include "cJSON.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "ie.audio";

#define SAMPLE_RATE      16000
#define FRAME_MS         20                              // một khung WS = 20ms
#define FRAME_SAMPLES    (SAMPLE_RATE * FRAME_MS / 1000) // 320
#define FRAME_BYTES      (FRAME_SAMPLES * 2)             // 640 — dưới buffer 1024 của WS client
#define PLAY_BUF_BYTES   (SAMPLE_RATE * 2 * 3 / 2)       // 1,5 giây đệm phát

static innoedge_audio_events_t s_ev;
static i2s_chan_handle_t s_rx, s_tx;
static StreamBufferHandle_t s_play_buf;
static volatile bool s_listening, s_playing;
static TaskHandle_t s_capture_task;

// ── I2S ─────────────────────────────────────────────────────────────────────

static esp_err_t init_mic(void)
{
#if CONFIG_IE_AUDIO_MIC_BCLK_GPIO < 0
    ESP_LOGW(TAG, "mic: tắt (IE_AUDIO_MIC_BCLK_GPIO=-1)");
    return ESP_OK;
#else
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, NULL, &s_rx), TAG, "mic channel");
    // INMP441: 24-bit trong slot 32-bit, kênh trái (L/R nối GND).
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_IE_AUDIO_MIC_BCLK_GPIO,
            .ws = CONFIG_IE_AUDIO_MIC_WS_GPIO,
            .dout = I2S_GPIO_UNUSED,
            .din = CONFIG_IE_AUDIO_MIC_DIN_GPIO,
        },
    };
    std.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std), TAG, "mic std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "mic enable");
    ESP_LOGI(TAG, "mic: I2S0 bclk=%d ws=%d din=%d", CONFIG_IE_AUDIO_MIC_BCLK_GPIO,
             CONFIG_IE_AUDIO_MIC_WS_GPIO, CONFIG_IE_AUDIO_MIC_DIN_GPIO);
    return ESP_OK;
#endif
}

static esp_err_t init_speaker(void)
{
#if CONFIG_IE_AUDIO_SPK_BCLK_GPIO < 0
    ESP_LOGW(TAG, "loa: tắt (IE_AUDIO_SPK_BCLK_GPIO=-1)");
    return ESP_OK;
#else
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan, &s_tx, NULL), TAG, "spk channel");
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = CONFIG_IE_AUDIO_SPK_BCLK_GPIO,
            .ws = CONFIG_IE_AUDIO_SPK_WS_GPIO,
            .dout = CONFIG_IE_AUDIO_SPK_DOUT_GPIO,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std), TAG, "spk std");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "spk enable");
    ESP_LOGI(TAG, "loa: I2S1 bclk=%d ws=%d dout=%d", CONFIG_IE_AUDIO_SPK_BCLK_GPIO,
             CONFIG_IE_AUDIO_SPK_WS_GPIO, CONFIG_IE_AUDIO_SPK_DOUT_GPIO);
    return ESP_OK;
#endif
}

// ── Thu: mic → WS binary, 20ms/khung ────────────────────────────────────────

static void capture_task(void *arg)
{
    (void)arg;
    static int32_t raw[FRAME_SAMPLES];
    static int16_t pcm[FRAME_SAMPLES];
    const int64_t deadline_us = esp_timer_get_time() +
                                (int64_t)CONFIG_IE_AUDIO_MAX_LISTEN_SEC * 1000000;

    while (s_listening) {
        if (esp_timer_get_time() > deadline_us) {
            ESP_LOGW(TAG, "thu quá %ds — tự dừng", CONFIG_IE_AUDIO_MAX_LISTEN_SEC);
            break;
        }
        size_t got = 0;
        if (!s_rx || i2s_channel_read(s_rx, raw, sizeof(raw), &got, pdMS_TO_TICKS(100)) != ESP_OK) {
            continue;
        }
        size_t n = got / sizeof(int32_t);
        for (size_t i = 0; i < n; i++) {
            // 24-bit MSB-aligned trong 32 → lấy 16 bit cao sau khi khuếch đại.
            int32_t v = raw[i] << CONFIG_IE_AUDIO_MIC_GAIN_SHIFT;
            pcm[i] = (int16_t)(v >> 16);
        }
        // Mất mạng thì rơi khung — audio dòng không vào hàng đợi bền, đúng thiết kế.
        innoedge_send_binary((const uint8_t *)pcm, n * sizeof(int16_t));
    }
    s_listening = false;
    gtek_ws_client_send_text("{\"type\":\"listen\",\"state\":\"stop\"}");
    ESP_LOGI(TAG, "listen stop");
    s_capture_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t innoedge_audio_listen_start(void)
{
    if (!s_rx) {
        return ESP_ERR_INVALID_STATE; // không có mic
    }
    if (s_listening) {
        return ESP_OK;
    }
    if (!innoedge_is_online()) {
        return ESP_ERR_INVALID_STATE;
    }
    // Đang phát trả lời mà người dùng bấm nói → ngắt phát (barge-in đơn giản).
    if (s_playing) {
        xStreamBufferReset(s_play_buf);
        s_playing = false;
        if (s_ev.on_tts) s_ev.on_tts(false);
    }
    esp_err_t err = gtek_ws_client_send_text(
        "{\"type\":\"listen\",\"state\":\"start\",\"format\":\"pcm16\",\"rate\":16000}");
    if (err != ESP_OK) {
        return err;
    }
    s_listening = true;
    if (xTaskCreate(capture_task, "ie_capture", 4096, NULL, 6, &s_capture_task) != pdPASS) {
        s_listening = false;
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "listen start");
    return ESP_OK;
}

esp_err_t innoedge_audio_listen_stop(void)
{
    s_listening = false; // capture_task tự gửi listen stop rồi thoát
    return ESP_OK;
}

bool innoedge_audio_is_listening(void) { return s_listening; }
bool innoedge_audio_is_playing(void) { return s_playing; }

// ── Phát: WS binary → stream buffer → I2S ───────────────────────────────────
// Task WS chỉ đẩy vào buffer (không block quá 50ms). Task phát kéo ra và ghi
// I2S; I2S write block theo nhịp thật nên buffer là bộ hấp thụ jitter mạng.

static void player_task(void *arg)
{
    (void)arg;
    static int16_t chunk[FRAME_SAMPLES];
    while (true) {
        size_t n = xStreamBufferReceive(s_play_buf, chunk, sizeof(chunk), pdMS_TO_TICKS(200));
        if (n == 0) {
            if (s_playing && xStreamBufferIsEmpty(s_play_buf)) {
                // Hết dữ liệu sau khi cloud báo stop → kết thúc phát.
            }
            continue;
        }
        if (s_tx) {
            size_t written = 0;
            i2s_channel_write(s_tx, chunk, n, &written, portMAX_DELAY);
        }
    }
}

void innoedge_audio_on_binary(const uint8_t *data, size_t len)
{
    if (!s_play_buf || !s_tx) {
        return;
    }
    if (!s_playing) {
        s_playing = true; // cloud có thể gửi binary trước khung tts start
        if (s_ev.on_tts) s_ev.on_tts(true);
    }
    size_t put = xStreamBufferSend(s_play_buf, data, len, pdMS_TO_TICKS(50));
    if (put < len) {
        ESP_LOGW(TAG, "đệm phát đầy, rơi %u byte", (unsigned)(len - put));
    }
}

void innoedge_audio_on_frame(const char *type, const char *raw_json)
{
    cJSON *root = cJSON_Parse(raw_json);
    if (!root) {
        return;
    }
    if (strcmp(type, "tts") == 0) {
        cJSON *state = cJSON_GetObjectItem(root, "state");
        const char *st = cJSON_IsString(state) ? state->valuestring : "";
        if (strcmp(st, "start") == 0) {
            xStreamBufferReset(s_play_buf);
            s_playing = true;
            if (s_ev.on_tts) s_ev.on_tts(true);
        } else if (strcmp(st, "stop") == 0) {
            // Để phần còn lại trong buffer phát hết rồi mới báo xong.
            while (s_tx && !xStreamBufferIsEmpty(s_play_buf)) {
                vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
            }
            s_playing = false;
            if (s_ev.on_tts) s_ev.on_tts(false);
        }
    } else if (strcmp(type, "stt") == 0) {
        cJSON *text = cJSON_GetObjectItem(root, "text");
        if (cJSON_IsString(text) && s_ev.on_stt) {
            s_ev.on_stt(text->valuestring);
        }
    }
    cJSON_Delete(root);
}

esp_err_t innoedge_audio_init(const innoedge_audio_events_t *ev)
{
    s_ev = ev ? *ev : (innoedge_audio_events_t){0};
    ESP_RETURN_ON_ERROR(init_mic(), TAG, "mic");
    ESP_RETURN_ON_ERROR(init_speaker(), TAG, "loa");
    if (s_tx) {
        s_play_buf = xStreamBufferCreate(PLAY_BUF_BYTES, FRAME_BYTES);
        if (!s_play_buf) {
            return ESP_ERR_NO_MEM;
        }
        if (xTaskCreate(player_task, "ie_player", 4096, NULL, 7, NULL) != pdPASS) {
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}
