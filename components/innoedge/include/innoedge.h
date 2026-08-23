#pragma once
//
// InnoEdge SDK cho ESP32 — API công khai v1.
//
// SDK chỉ lo HẠ TẦNG: WiFi/provisioning, kết nối cloud, hàng đợi bền, lệnh từ
// xa, cấu hình động, OTA, cảnh báo. SDK KHÔNG chứa nghiệp vụ sản phẩm (motor,
// giá bán, luồng màn hình, quy tắc cảm biến) — phần đó nằm ở application.
//
// Vòng đời tối thiểu:
//     innoedge_init(&cfg);
//     innoedge_start();
// Xong. Máy tự lên mạng, tự vào cloud, tự OTA, tự gửi lại giao dịch tồn.
//
#include "cJSON.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define INNOEDGE_SDK_VERSION "1.0.0"

// ── Sự kiện thanh toán gửi lên cloud ────────────────────────────────────────
typedef enum {
    INNOEDGE_PAY_COIN = 0,  // xu khách bỏ vào  → count = số xu
    INNOEDGE_PAY_CASH,      // tiền mặt/bill    → amount_vnd = VND
    INNOEDGE_PAY_TICKET,    // vé thưởng đã hủy → count = số vé (KHÔNG phải tiền)
    INNOEDGE_PAY_COIN_OUT,  // xu TRẢ RA cho khách → count = số xu (đối soát hopper)
} innoedge_payment_kind_t;

// ── Handler lệnh từ xa ──────────────────────────────────────────────────────
// params: object JSON server gửi kèm (NULL nếu lệnh không có params).
// result_out: chuỗi JSON object trả về cloud, vd "{\"pulses\":3}" (để rỗng nếu không).
// msg_out: thông điệp người-đọc.
// Trả ESP_OK → ack "ok"; lỗi khác → ack "error".
// Handler chạy trên task WS — KHÔNG block quá vài giây, KHÔNG gọi từ ISR.
typedef esp_err_t (*innoedge_command_fn)(cJSON *params,
                                         char *result_out, size_t result_len,
                                         char *msg_out, size_t msg_len);

// ── Callback sự kiện từ cloud ───────────────────────────────────────────────
// Mọi trường có thể để NULL. Chạy trên task WS — giữ ngắn, đẩy việc nặng sang
// task riêng.
typedef struct {
    // Cloud đã ghi nhận một khoản tiền (duplicate=true là frame gửi lại).
    void (*on_payment_ack)(const char *method, int coins, int64_t amount_vnd,
                           int64_t rate_vnd, bool duplicate);
    // QR động đã sinh xong cho innoedge_request_qr(): render `payload` NGUYÊN VĂN.
    void (*on_qr)(const char *payload, int64_t amount_vnd, const char *ref_code,
                  int expires_sec, int64_t intent_id);
    // Cổng thanh toán lỗi — hiện `message` cho khách + nút thử lại.
    void (*on_qr_error)(const char *message);
    // Khách đã trả tiền QR thành công.
    void (*on_paid)(int64_t intent_id, int64_t amount_vnd);
    // QR tĩnh của máy (cloud đẩy xuống, đã cache NVS).
    void (*on_static_qr)(const char *payload, const char *ref_code);
    // Máy vừa được gán cho một đối tác → bắt đầu phục vụ được.
    void (*on_assigned)(void);
    // Máy chưa/không còn được gán → nên hiện màn "chưa đăng ký".
    void (*on_unassigned)(void);
    // Cấu hình vận hành đã tải xong (đọc bằng innoedge_config_json).
    void (*on_config)(int version);
    // Đang chờ cài WiFi (BLE/SoftAP đã mở) → hiện hướng dẫn ghép nối.
    void (*on_provisioning)(void);
} innoedge_events_t;

// ── Cấu hình khởi tạo ───────────────────────────────────────────────────────
// Mọi trường đều có mặc định hợp lý; `{0}` là cấu hình chạy được.
typedef struct {
    const char *fw_version;         // NULL → CONFIG_GTEK_FW_VERSION
    const innoedge_events_t *events;// NULL → không nhận callback nào
    uint32_t heartbeat_sec;         // 0 → 30
    bool disable_ota;               // true → không tự kiểm tra bản mới
    bool disable_config_fetch;      // true → không tải cấu hình vận hành
    bool disable_ntp;               // true → không đồng bộ giờ
    // true = khách đang giữa giao dịch → SDK HOÃN tải/reboot OTA. NULL = cập
    // nhật ngay khi có bản mới.
    bool (*busy_check)(void);
} innoedge_config_t;

// ── Vòng đời ────────────────────────────────────────────────────────────────

// Khởi tạo NVS/netif/event loop + nạp cấu hình thiết bị + hàng đợi + command
// bus. Gọi MỘT LẦN, đầu app_main. Chưa đụng tới mạng.
esp_err_t innoedge_init(const innoedge_config_t *cfg);

// Đưa máy lên mạng: chưa có WiFi → mở provisioning (BLE/SoftAP) rồi dừng ở đó;
// đã có WiFi → kết nối, mở WebSocket tới cloud, chạy heartbeat/OTA/config/gửi
// lại hàng đợi. KHÔNG chặn: trả về ngay, mọi thứ chạy nền. WiFi lên chậm cũng
// không sao — SDK tự khởi động dịch vụ khi có mạng.
esp_err_t innoedge_start(void);

bool innoedge_is_online(void);      // WebSocket tới cloud đang mở?
bool innoedge_is_assigned(void);    // máy đã được gán cho đối tác chưa?
const char *innoedge_device_id(void);   // MAC — định danh máy trên cloud

// ── Gửi dữ liệu lên cloud ───────────────────────────────────────────────────

// Ghi nhận một khoản tiền. LUÔN vào hàng đợi bền (NVS) TRƯỚC khi gửi → mất
// mạng/mất điện không mất giao dịch; SDK tự gửi lại tới khi cloud ack.
// count dùng cho COIN/TICKET/COIN_OUT; amount_vnd dùng cho CASH.
esp_err_t innoedge_publish_payment(innoedge_payment_kind_t kind, int count,
                                   int64_t amount_vnd);

// Số giao dịch còn tồn chưa gửi được lên cloud (0 = đã đồng bộ hết).
uint32_t innoedge_queue_depth(void);

// Báo sự cố (kẹt xu, hết giấy, lỗi bơm...). active=false = báo ĐÃ HẾT lỗi.
// Có latch theo code → gọi trong vòng lặp cũng không spam cloud.
// An toàn khi offline (trả lỗi, không crash).
esp_err_t innoedge_alert(const char *code, const char *severity,
                         const char *message, bool active);

// ── Nhận lệnh từ cloud ──────────────────────────────────────────────────────

// Đăng ký nghiệp vụ cho một action. `action` phải là string literal/static.
// SDK lo: nhận frame → chống trùng (bền qua reboot) → gọi handler → ack.
// Gọi SAU innoedge_init(), trước hoặc sau innoedge_start() đều được.
esp_err_t innoedge_register_command(const char *action, innoedge_command_fn fn);

// Hẹn khởi động lại máy sau ~800ms. Dùng TRONG handler lệnh "reboot": gọi
// esp_restart() thẳng sẽ reboot trước khi ack kịp bay đi, cloud tưởng lệnh hỏng
// và gửi lại.
void innoedge_reboot_after_ack(void);

// ── QR động ─────────────────────────────────────────────────────────────────

// Xin cloud sinh QR cho `amount_vnd`. Kết quả về qua events->on_qr (hoặc
// on_qr_error). Cần máy đã gán + đang online.
esp_err_t innoedge_request_qr(int64_t amount_vnd);

// ── Cấu hình vận hành động ──────────────────────────────────────────────────

// Đọc cấu hình đã cache trong NVS (combo/giá/tham số). Chạy được cả khi OFFLINE.
// version có thể NULL. Trả ESP_ERR_NOT_FOUND nếu chưa từng tải được.
esp_err_t innoedge_config_json(char *out, size_t out_len, int *version);

// Tải lại cấu hình từ cloud ngay. Lỗi mạng → giữ nguyên cache cũ.
esp_err_t innoedge_config_reload(void);

// ── OTA ─────────────────────────────────────────────────────────────────────

// Kiểm tra bản mới ngay (SDK tự chạy khi boot nếu không disable_ota).
// Có bản mới → tải, xác thực SHA-256, ghi partition, reboot. busy_check bận →
// hoãn tới lần sau. Bản mới lỗi → bootloader tự rollback bản cũ.
esp_err_t innoedge_ota_check(void);

#ifdef __cplusplus
}
#endif
