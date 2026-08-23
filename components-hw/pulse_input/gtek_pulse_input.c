#include "gtek_pulse_input.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gtek_fault.h"
#include "gtek_ws_client.h"
#include "sdkconfig.h"

static const char *TAG = "gtek.pulse";

typedef struct {
    volatile uint32_t pulses;
    volatile TickType_t last_tick;
} pulse_counter_t;

static pulse_counter_t s_coin;
static pulse_counter_t s_bill;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static gtek_pulse_input_cb_t s_cb;
static void *s_cb_ctx;
static gtek_pulse_expander_read_fn s_expander_read;

void gtek_pulse_input_set_expander_reader(gtek_pulse_expander_read_fn fn)
{
    s_expander_read = fn;
}

static void record_pulse(gtek_pulse_kind_t kind, TickType_t now, bool from_isr)
{
    TickType_t min_ticks = pdMS_TO_TICKS(CONFIG_GTEK_PULSE_MIN_MS);

    if (from_isr) {
        portENTER_CRITICAL_ISR(&s_mux);
    } else {
        portENTER_CRITICAL(&s_mux);
    }
    pulse_counter_t *counter = kind == GTEK_PULSE_COIN ? &s_coin : &s_bill;
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
    gtek_pulse_kind_t kind = (gtek_pulse_kind_t)(intptr_t)arg;
    record_pulse(kind, xTaskGetTickCountFromISR(), true);
}

static void poll_expander_coin(TickType_t now)
{
#if CONFIG_GTEK_COIN_PULSE_PCA9554_P0
    static bool was_active;
    static TickType_t active_since;
    static bool stuck_reported;
    if (!s_expander_read) {
        return; // chưa có board nào đăng ký nguồn đọc (fault đã báo lúc init)
    }
    bool active = false;
    esp_err_t err = s_expander_read(&active);
    if (err != ESP_OK) {
        if (err != ESP_ERR_INVALID_STATE) {
            // Đầu đọc xu lỗi (I2C) = máy KHÔNG nhận được xu nhưng vẫn "chạy" → mất
            // doanh thu âm thầm. gtek_fault tự latch nên không spam.
            gtek_fault_set("coin_reader_fault", "critical",
                           "Dau doc xu loi - may khong nhan duoc xu");
        }
        return;
    }
    gtek_fault_clear("coin_reader_fault"); // đọc lại được → hết lỗi
    if (active && !was_active) {
        active_since = now;
        record_pulse(GTEK_PULSE_COIN, now, false);
    }
    // Sensor xu "active" liên tục quá lâu = xu/cò KẸT trên đầu đọc → máy không
    // nhận xu mới nhưng vẫn chạy. Báo để nhân viên thông kẹt.
    if (active && (now - active_since) > pdMS_TO_TICKS(5000)) {
        if (!stuck_reported) {
            gtek_fault_set("coin_acceptor_stuck", "critical",
                           "Xu/co bi ket tren dau doc - khong nhan xu moi");
            stuck_reported = true;
        }
    } else if (!active && stuck_reported) {
        gtek_fault_clear("coin_acceptor_stuck");
        stuck_reported = false;
    }
    was_active = active;
#else
    (void)now;
#endif
}

static void pulse_task(void *arg)
{
    (void)arg;
    const TickType_t gap_ticks = pdMS_TO_TICKS(CONFIG_GTEK_PULSE_GAP_MS);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10));
        TickType_t now = xTaskGetTickCount();
        poll_expander_coin(now);
        uint32_t coin_pulses = 0;
        uint32_t bill_pulses = 0;

        portENTER_CRITICAL(&s_mux);
        if (s_coin.pulses > 0 && now - s_coin.last_tick >= gap_ticks) {
            coin_pulses = s_coin.pulses;
            s_coin.pulses = 0;
        }
        if (s_bill.pulses > 0 && now - s_bill.last_tick >= gap_ticks) {
            bill_pulses = s_bill.pulses;
            s_bill.pulses = 0;
        }
        portEXIT_CRITICAL(&s_mux);

        if (coin_pulses > 0 && s_cb) {
            int64_t amount = (int64_t)coin_pulses * CONFIG_GTEK_COIN_VND_PER_PULSE;
            ESP_LOGI(TAG, "coin pulse train pulses=%u amount=%lld",
                     (unsigned)coin_pulses, (long long)amount);
            s_cb(GTEK_PULSE_COIN, coin_pulses, amount, s_cb_ctx);
        }
        if (bill_pulses > 0 && s_cb) {
            int64_t amount = (int64_t)bill_pulses * CONFIG_GTEK_BILL_VND_PER_PULSE;
            ESP_LOGI(TAG, "bill pulse train pulses=%u amount=%lld",
                     (unsigned)bill_pulses, (long long)amount);
            s_cb(GTEK_PULSE_BILL, bill_pulses, amount, s_cb_ctx);
        }
    }
}

static esp_err_t configure_input(gpio_num_t gpio, gtek_pulse_kind_t kind)
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

esp_err_t gtek_pulse_input_init(gtek_pulse_input_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_cb_ctx = ctx;
    bool coin_gpio_enabled = CONFIG_GTEK_COIN_PULSE_GPIO >= 0;
    bool bill_enabled = CONFIG_GTEK_BILL_PULSE_GPIO >= 0;
    bool coin_enabled = coin_gpio_enabled || CONFIG_GTEK_COIN_PULSE_PCA9554_P0;
    if (!coin_enabled && !bill_enabled) {
        ESP_LOGW(TAG, "pulse inputs disabled");
        return ESP_OK;
    }

#if CONFIG_GTEK_COIN_PULSE_PCA9554_P0
    // Config bảo đọc xu qua expander mà board wiring quên đăng ký reader →
    // máy "chạy" nhưng không nhận xu = mất doanh thu âm thầm. Báo ngay lúc init.
    if (!s_expander_read) {
        gtek_fault_set("coin_reader_init_failed", "critical",
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
        err = configure_input((gpio_num_t)CONFIG_GTEK_COIN_PULSE_GPIO, GTEK_PULSE_COIN);
        if (err != ESP_OK) {
            gtek_fault_set("coin_reader_init_failed", "critical",
                           "Khoi tao dau doc xu that bai - may khong nhan duoc xu");
            return err;
        }
    }
    if (bill_enabled) {
        err = configure_input((gpio_num_t)CONFIG_GTEK_BILL_PULSE_GPIO, GTEK_PULSE_BILL);
        if (err != ESP_OK) {
            gtek_fault_set("bill_reader_init_failed", "critical",
                           "Khoi tao dau doc tien that bai");
            return err;
        }
    }
    BaseType_t ok = xTaskCreate(pulse_task, "gtek_pulse", 4096, NULL, 6, NULL);
    if (ok != pdPASS) {
        gtek_fault_set("coin_reader_init_failed", "critical",
                       "Khong tao duoc task doc xu");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "pulse inputs coin_gpio=%d coin_pca_p0=%d bill_gpio=%d",
             CONFIG_GTEK_COIN_PULSE_GPIO, CONFIG_GTEK_COIN_PULSE_PCA9554_P0 ? 1 : 0,
             CONFIG_GTEK_BILL_PULSE_GPIO);
    return ESP_OK;
}
