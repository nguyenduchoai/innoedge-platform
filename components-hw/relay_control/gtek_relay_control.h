#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ── Relay đa kênh ───────────────────────────────────────────────────────────
// Mỗi kênh map tới 1 GPIO cấu hình qua Kconfig (mặc định -1 = vô hiệu hoá →
// kênh chỉ ghi log, không chạm phần cứng, vẫn build + boot được khi chưa đấu
// dây). Dùng cho máy rửa xe tự phục vụ: WATER/FOAM/AIR/VACUUM. DISPENSE giữ
// nguyên relay nhả tiền/credit cũ (CONFIG_GTEK_RELAY_GPIO).
typedef enum {
    GTEK_RELAY_WATER = 0,  // xịt nước
    GTEK_RELAY_FOAM,       // phun bọt
    GTEK_RELAY_AIR,        // nén khí
    GTEK_RELAY_VACUUM,     // hút bụi
    GTEK_RELAY_DISPENSE,   // relay nhả tiền/credit (giữ tương thích cũ)
    GTEK_RELAY_COUNT
} gtek_relay_channel_t;

// Khởi tạo tất cả kênh có GPIO hợp lệ về mức 0 (OFF). An toàn khi boot: mọi
// relay đều tắt. Kênh GPIO=-1 bị bỏ qua. Gọi một lần khi khởi động.
esp_err_t gtek_relay_control_init(void);

// Bật/tắt một kênh. GPIO=-1 → no-op (chỉ log). Không ràng buộc kênh khác.
esp_err_t gtek_relay_set(gtek_relay_channel_t channel, bool on);

// Bật DUY NHẤT một kênh, tắt mọi kênh "rửa" khác (WATER/FOAM/AIR/VACUUM) để bảo
// vệ bơm (chỉ 1 relay ON cùng lúc). Không đụng tới DISPENSE. channel=GTEK_RELAY_COUNT
// → tắt hết các kênh rửa.
esp_err_t gtek_relay_set_exclusive(gtek_relay_channel_t channel);

// Tắt toàn bộ kênh rửa (WATER/FOAM/AIR/VACUUM). Dùng khi kết thúc/dừng/lỗi.
esp_err_t gtek_relay_all_wash_off(void);

// Tên kênh để log/UI ("water"/"foam"/"air"/"vacuum"/"dispense").
const char *gtek_relay_channel_name(gtek_relay_channel_t channel);

// ── Tương thích ngược: relay nhả tiền dạng xung ─────────────────────────────
// Phát một xung trên kênh DISPENSE (mức 1 trong pulse_ms rồi về 0). pulse_ms=0
// → dùng CONFIG_GTEK_RELAY_PULSE_MS.
esp_err_t gtek_relay_control_pulse(uint32_t pulse_ms);

#ifdef __cplusplus
}
#endif
