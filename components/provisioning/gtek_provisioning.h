#pragma once

#include "esp_err.h"
#include "gtek_config_store.h"

#ifdef __cplusplus
extern "C" {
#endif

// Thông báo UI "đang chờ cài đặt" — board wiring (main) đăng ký; provisioning
// không phụ thuộc ui_app/board nào (hiến pháp I). NULL = bỏ qua.
typedef void (*gtek_provisioning_ui_fn)(void);
void gtek_provisioning_set_ui_notify(gtek_provisioning_ui_fn fn);

esp_err_t gtek_provisioning_start(const gtek_device_config_t *config);

#ifdef __cplusplus
}
#endif
