#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    GTEK_PULSE_COIN = 0,
    GTEK_PULSE_BILL = 1,
} gtek_pulse_kind_t;

typedef void (*gtek_pulse_input_cb_t)(gtek_pulse_kind_t kind, uint32_t pulses,
                                      int64_t amount_vnd, void *ctx);

// Nguồn đọc "coin active" ngoài GPIO (vd IO-expander trên board có màn hình).
// Board wiring (main) đăng ký TRƯỚC gtek_pulse_input_init; component này không
// biết board nào cung cấp — giữ pulse_input độc lập board (hiến pháp I).
typedef esp_err_t (*gtek_pulse_expander_read_fn)(bool *active);
void gtek_pulse_input_set_expander_reader(gtek_pulse_expander_read_fn fn);

esp_err_t gtek_pulse_input_init(gtek_pulse_input_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
