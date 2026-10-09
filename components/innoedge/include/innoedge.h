// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
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

#define INNOEDGE_SDK_VERSION "0.2.0"

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
    // Khung binary từ cloud (audio PCM, ảnh…). Có thể tới theo mảnh. Chạy trên
    // task WS — chỉ đẩy vào buffer/queue, đừng xử lý nặng ở đây.
    void (*on_binary)(const uint8_t *data, size_t len);
    // Khung text có "type" SDK không biết → application tự xử lý (vd "tts",
    // "stt" của innoedge_audio). raw_json sống tới khi callback trả về.
    void (*on_frame)(const char *type, const char *raw_json);
} innoedge_events_t;

// ── Cấu hình khởi tạo ───────────────────────────────────────────────────────
// Mọi trường đều có mặc định hợp lý; `{0}` là cấu hình chạy được.
typedef struct {
    // NULL (khuyên dùng) → version trong mô tả app = PROJECT_VER của project,
    // dạng X.Y.Z. SDK đối chiếu version trong image OTA với manifest nên không
    // bao giờ tải lại mãi một bản. Đặt chuỗi chỉ khi không đặt được PROJECT_VER
    // (Arduino): khi đó phải tự tăng cùng bản build.
    const char *fw_version;
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

// Gửi một sự kiện tuỳ ý lên cloud (nút bấm, cảm biến, lựa chọn của người dùng…).
//   {"type":"event","name":"<name>","seq":N,"ts":T,"data":<data_json>}
// name: chỉ [A-Za-z0-9_.-]. data_json: object JSON hợp lệ hoặc NULL (→ {}).
// Cả hai được KIỂM trước khi vào hàng đợi — sai trả ESP_ERR_INVALID_ARG, vì một
// khung hỏng ở đầu hàng đợi bền sẽ chặn mọi giao dịch tiền phía sau, qua reboot.
// Cùng hàng đợi bền với tiền: mất mạng vẫn giữ, cloud phải trả
// {"type":"event_ack","seq":N}. Giới hạn cả khung ~190 byte — data dài hơn
// ~120 byte trả ESP_ERR_INVALID_SIZE.
esp_err_t innoedge_publish_event(const char *name, const char *data_json);

// Gửi một khung binary lên cloud (audio, ảnh, dump cảm biến). KHÔNG qua hàng
// đợi bền — mất mạng là mất, đúng cho dữ liệu dòng. Thread-safe.
esp_err_t innoedge_send_binary(const uint8_t *data, size_t len);

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
// Có bản mới → tải, kiểm size + SHA-256 + version trong image, ghi partition,
// reboot. busy_check bận → thử lại sau 10 phút. Bản mới lỗi → bootloader tự
// rollback bản cũ. Khi OTA nền đang chạy, hàm chỉ đánh thức nó rồi trả về ngay.
esp_err_t innoedge_ota_check(void);

#ifdef __cplusplus
}
#endif
