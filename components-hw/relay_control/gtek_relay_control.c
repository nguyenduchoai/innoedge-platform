#include "gtek_relay_control.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "gtek_fault.h"
#include "sdkconfig.h"

static const char *TAG = "gtek.relay";

// GPIO mặc định cho các kênh chưa khai báo trong Kconfig (an toàn = -1).
#ifndef CONFIG_GTEK_WASH_GPIO_WATER
#define CONFIG_GTEK_WASH_GPIO_WATER (-1)
#endif
#ifndef CONFIG_GTEK_WASH_GPIO_FOAM
#define CONFIG_GTEK_WASH_GPIO_FOAM (-1)
#endif
#ifndef CONFIG_GTEK_WASH_GPIO_AIR
#define CONFIG_GTEK_WASH_GPIO_AIR (-1)
#endif
#ifndef CONFIG_GTEK_WASH_GPIO_VACUUM
#define CONFIG_GTEK_WASH_GPIO_VACUUM (-1)
#endif
#ifndef CONFIG_GTEK_RELAY_GPIO
#define CONFIG_GTEK_RELAY_GPIO (-1)
#endif
#ifndef CONFIG_GTEK_RELAY_PULSE_MS
#define CONFIG_GTEK_RELAY_PULSE_MS 80
#endif

// Bảng kênh: GPIO + tên. Thứ tự khớp gtek_relay_channel_t.
static const struct {
    int gpio;
    const char *name;
} s_channels[GTEK_RELAY_COUNT] = {
    [GTEK_RELAY_WATER] = {CONFIG_GTEK_WASH_GPIO_WATER, "water"},
    [GTEK_RELAY_FOAM] = {CONFIG_GTEK_WASH_GPIO_FOAM, "foam"},
    [GTEK_RELAY_AIR] = {CONFIG_GTEK_WASH_GPIO_AIR, "air"},
    [GTEK_RELAY_VACUUM] = {CONFIG_GTEK_WASH_GPIO_VACUUM, "vacuum"},
    [GTEK_RELAY_DISPENSE] = {CONFIG_GTEK_RELAY_GPIO, "dispense"},
};

static bool gpio_ok(int gpio)
{
    return gpio >= 0 && gpio < GPIO_NUM_MAX;
}

const char *gtek_relay_channel_name(gtek_relay_channel_t channel)
{
    if (channel < 0 || channel >= GTEK_RELAY_COUNT) {
        return "?";
    }
    return s_channels[channel].name;
}

esp_err_t gtek_relay_control_init(void)
{
    uint64_t mask = 0;
    for (int i = 0; i < GTEK_RELAY_COUNT; i++) {
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
    // GPIO 39-42 mặc định là JTAG (MTCK/MTDO/MTDI/MTMS) — reset về GPIO thường
    // trước khi config, không thì pulse out 42 có thể câm (gotcha board mới).
    for (int i = 0; i < GTEK_RELAY_COUNT; i++) {
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
    // An toàn khi boot: tắt hết relay đã cấu hình.
    for (int i = 0; i < GTEK_RELAY_COUNT; i++) {
        if (gpio_ok(s_channels[i].gpio)) {
            gpio_set_level(s_channels[i].gpio, 0);
        }
    }
    ESP_LOGI(TAG, "init xong, tất cả relay OFF");
    return ESP_OK;
}

esp_err_t gtek_relay_set(gtek_relay_channel_t channel, bool on)
{
    if (channel < 0 || channel >= GTEK_RELAY_COUNT) {
        return ESP_ERR_INVALID_ARG;
    }
    int gpio = s_channels[channel].gpio;
    if (!gpio_ok(gpio)) {
        // Chưa đấu dây → chỉ log, vẫn coi như thành công để logic chạy bình thường.
        ESP_LOGI(TAG, "[no-op] %s %s (GPIO=-1)", s_channels[channel].name, on ? "ON" : "OFF");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "%s %s (GPIO%d)", s_channels[channel].name, on ? "ON" : "OFF", gpio);
    esp_err_t err = gpio_set_level(gpio, on ? 1 : 0);
    if (err != ESP_OK) {
        // Relay đã đấu dây nhưng đặt mức GPIO lỗi — cơ cấu không chạy đúng.
        gtek_fault_set("relay_set_failed", "critical", "Khong dieu khien duoc relay");
    }
    return err;
}

esp_err_t gtek_relay_set_exclusive(gtek_relay_channel_t channel)
{
    esp_err_t first_err = ESP_OK;
    // Tắt mọi kênh rửa khác trước (bảo vệ bơm: chỉ 1 ON cùng lúc).
    for (int i = GTEK_RELAY_WATER; i <= GTEK_RELAY_VACUUM; i++) {
        if (i == (int)channel) {
            continue;
        }
        esp_err_t err = gtek_relay_set((gtek_relay_channel_t)i, false);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    // Bật kênh mong muốn (nếu là kênh rửa hợp lệ).
    if (channel >= GTEK_RELAY_WATER && channel <= GTEK_RELAY_VACUUM) {
        esp_err_t err = gtek_relay_set(channel, true);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}

esp_err_t gtek_relay_all_wash_off(void)
{
    esp_err_t first_err = ESP_OK;
    for (int i = GTEK_RELAY_WATER; i <= GTEK_RELAY_VACUUM; i++) {
        esp_err_t err = gtek_relay_set((gtek_relay_channel_t)i, false);
        if (err != ESP_OK && first_err == ESP_OK) {
            first_err = err;
        }
    }
    return first_err;
}

esp_err_t gtek_relay_control_pulse(uint32_t pulse_ms)
{
    int gpio = s_channels[GTEK_RELAY_DISPENSE].gpio;
    if (!gpio_ok(gpio)) {
        // Yêu cầu nhả tiền nhưng relay dispense CHƯA đấu dây → credit không ra
        // dù khách đã trả. Đây là lỗi nghiêm trọng, không được im lặng coi như OK.
        gtek_fault_set("dispense_relay_not_configured", "critical",
                       "Relay nha tien chua duoc cau hinh - khach tra tien khong ra credit");
        return ESP_ERR_INVALID_STATE;
    }
    if (pulse_ms == 0) {
        pulse_ms = CONFIG_GTEK_RELAY_PULSE_MS;
    }
    esp_err_t on_err = gpio_set_level(gpio, 1);
    if (on_err != ESP_OK) {
        gtek_fault_set("dispense_relay_error", "critical", "Khong kich duoc relay nha tien");
        return on_err;
    }
    vTaskDelay(pdMS_TO_TICKS(pulse_ms));
    esp_err_t off_err = gpio_set_level(gpio, 0);
    if (off_err != ESP_OK) {
        // Không tắt được relay = nguy cơ KẸT BẬT (tràn tiền/chạy mãi) → critical.
        gtek_fault_set("relay_stuck_on", "critical", "Relay nha tien khong tat duoc - nguy co ket bat");
    }
    return off_err;
}
