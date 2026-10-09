// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// Test host cho command bus — phần rủi ro nhất của SDK: registry runtime +
// chống trùng lệnh. Chống trùng hỏng = máy NHẢ TIỀN HAI LẦN hoặc KHÔNG NHẢ.
//
//   ./tests/run.sh

#include "ie_command_bus.h"
#include "ie_command_journal.h"
#include "ie_ws_client.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// ── Cấy giả NVS (sống qua "reboot" = gọi lại ie_command_bus_init) ────────────
static int64_t g_nvs_last_cmd;
static unsigned char g_nvs_journal[sizeof(ie_command_journal_t)];
static int g_nvs_journal_exists;
static int g_nvs_write_fails;

int64_t ie_config_store_last_command_id(void) { return g_nvs_last_cmd; }

esp_err_t ie_config_store_save_last_command_id(int64_t id)
{
    if (g_nvs_write_fails) return ESP_FAIL;
    g_nvs_last_cmd = id;
    return ESP_OK;
}

esp_err_t ie_config_store_load_blob(const char *key, void *data, size_t len)
{
    assert(strcmp(key, "cmd_journal") == 0);
    if (!g_nvs_journal_exists) return ESP_ERR_NOT_FOUND;
    if (len != sizeof(g_nvs_journal)) return ESP_ERR_INVALID_SIZE;
    memcpy(data, g_nvs_journal, len);
    return ESP_OK;
}

esp_err_t ie_config_store_save_blob(const char *key, const void *data, size_t len)
{
    assert(strcmp(key, "cmd_journal") == 0);
    if (g_nvs_write_fails) return ESP_FAIL;
    assert(len == sizeof(g_nvs_journal));
    memcpy(g_nvs_journal, data, len);
    g_nvs_journal_exists = 1;
    return ESP_OK;
}

static int g_faults;
void ie_fault_set(const char *code, const char *sev, const char *msg)
{ (void)code; (void)sev; (void)msg; g_faults++; }

// Ack cuối cùng bus gửi đi — test kiểm tra nội dung ack.
static struct { int64_t id; char status[16]; char message[80]; int count; } g_ack;

esp_err_t ie_ws_client_send_command_ack(int64_t id, const char *status,
                                        const char *message, const char *result)
{
    (void)result;
    g_ack.id = id;
    snprintf(g_ack.status, sizeof(g_ack.status), "%s", status ? status : "");
    snprintf(g_ack.message, sizeof(g_ack.message), "%s", message ? message : "");
    g_ack.count++;
    return ESP_OK;
}

// ── Handler thử ─────────────────────────────────────────────────────────────
static int g_dispense_runs;
static unsigned char g_snapshot[sizeof(ie_command_journal_t)];

static esp_err_t h_dispense(cJSON *p, char *result, size_t rl, char *msg, size_t ml)
{
    (void)p; (void)msg; (void)ml;
    g_dispense_runs++;
    snprintf(result, rl, "{\"pulses\":2}");
    return ESP_OK;
}

// Chụp NVS ngay lúc handler chạy = trạng thái nếu mất điện giữa chừng.
static esp_err_t h_dispense_snapshot(cJSON *p, char *r, size_t rl, char *m, size_t ml)
{
    memcpy(g_snapshot, g_nvs_journal, sizeof(g_snapshot));
    return h_dispense(p, r, rl, m, ml);
}

static esp_err_t h_fail(cJSON *p, char *result, size_t rl, char *msg, size_t ml)
{
    (void)p; (void)result; (void)rl;
    g_dispense_runs++;
    snprintf(msg, ml, "het xu");
    return ESP_FAIL;
}

static esp_err_t h_other(cJSON *p, char *result, size_t rl, char *msg, size_t ml)
{
    (void)p; (void)result; (void)rl; (void)msg; (void)ml;
    return ESP_OK;
}

static void wipe_nvs(void)
{
    g_nvs_last_cmd = 0;
    g_nvs_journal_exists = 0;
    memset(g_nvs_journal, 0, sizeof(g_nvs_journal));
}

// "Bật máy": NVS giữ nguyên, RAM về 0, đăng ký lại handler như app thật.
static void boot(void)
{
    g_nvs_write_fails = 0;
    g_dispense_runs = 0;
    g_faults = 0;
    memset(&g_ack, 0, sizeof(g_ack));
    assert(ie_command_bus_init() == ESP_OK);
    assert(ie_command_bus_register("dispense", h_dispense) == ESP_OK);
}

static int is_uncertain(void)
{
    return strcmp(g_ack.status, "error") == 0 && strstr(g_ack.message, "uncertain") != NULL;
}

int main(void)
{
    // 1. Đăng ký + dispatch cơ bản.
    wipe_nvs(); boot();
    ie_command_bus_dispatch(10, "dispense", NULL);
    assert(g_dispense_runs == 1);
    assert(strcmp(g_ack.status, "ok") == 0);

    // 2. Action chưa đăng ký → ack error, KHÔNG chạy handler nào.
    ie_command_bus_dispatch(11, "khong_ton_tai", NULL);
    assert(g_dispense_runs == 1);
    assert(strcmp(g_ack.message, "unknown action") == 0);

    // 3. Thiếu action → ack error.
    ie_command_bus_dispatch(12, NULL, NULL);
    assert(strcmp(g_ack.message, "missing action") == 0);

    // 4. TRÙNG cùng phiên → chỉ ack lại "ok/duplicate", KHÔNG nhả tiền lần hai.
    wipe_nvs(); boot();
    ie_command_bus_dispatch(20, "dispense", NULL);
    ie_command_bus_dispatch(20, "dispense", NULL);
    assert(g_dispense_runs == 1);
    assert(strcmp(g_ack.status, "ok") == 0 && strcmp(g_ack.message, "duplicate") == 0);

    // 5. TRÙNG QUA REBOOT — cloud gửi lại id đã xong sau khi máy khởi động lại.
    boot();
    ie_command_bus_dispatch(20, "dispense", NULL);
    assert(g_dispense_runs == 0);
    assert(strcmp(g_ack.message, "duplicate") == 0);
    ie_command_bus_dispatch(21, "dispense", NULL); // lệnh MỚI vẫn chạy
    assert(g_dispense_runs == 1);

    // 6. LỆCH THỨ TỰ — lỗi của high-watermark cũ: 50 tới trước 40 làm 40 bị bỏ mà
    //    vẫn ack ok (khách trả tiền, máy không nhả). Journal phải chạy cả hai.
    wipe_nvs(); boot();
    ie_command_bus_dispatch(50, "dispense", NULL);
    ie_command_bus_dispatch(40, "dispense", NULL);
    assert(g_dispense_runs == 2);
    assert(strcmp(g_ack.status, "ok") == 0 && g_ack.id == 40);

    // 7. Handler lỗi → ack "error" + message handler; gửi lại KHÔNG chạy lại và
    //    KHÔNG ack ok (cloud phải đối soát, không phải tưởng đã xong).
    wipe_nvs(); boot();
    assert(ie_command_bus_register("fail", h_fail) == ESP_OK);
    ie_command_bus_dispatch(60, "fail", NULL);
    assert(strcmp(g_ack.message, "het xu") == 0);
    ie_command_bus_dispatch(60, "fail", NULL);
    assert(g_dispense_runs == 1);
    assert(strcmp(g_ack.status, "error") == 0 && strstr(g_ack.message, "failed"));

    // 8. MẤT ĐIỆN GIỮA HANDLER: RUNNING đã ghi, kết quả chưa. Sau reboot lệnh
    //    gửi lại KHÔNG chạy lại, KHÔNG ack ok, và có cảnh báo critical.
    wipe_nvs(); boot();
    assert(ie_command_bus_register("snap", h_dispense_snapshot) == ESP_OK);
    ie_command_bus_dispatch(70, "snap", NULL);
    memcpy(g_nvs_journal, g_snapshot, sizeof(g_nvs_journal)); // kết quả chưa kịp ghi
    boot();
    assert(g_faults == 1); // command_interrupted
    assert(ie_command_bus_register("snap", h_dispense_snapshot) == ESP_OK);
    ie_command_bus_dispatch(70, "snap", NULL);
    assert(g_dispense_runs == 0);
    assert(is_uncertain());

    // 9. commandId <= 0 → không chống trùng được → KHÔNG chạy.
    wipe_nvs(); boot();
    ie_command_bus_dispatch(0, "dispense", NULL);
    ie_command_bus_dispatch(-3, "dispense", NULL);
    assert(g_dispense_runs == 0);
    assert(strcmp(g_ack.status, "error") == 0);

    // 10. Ghi journal hỏng → KHÔNG chạy (thà từ chối còn hơn chạy mà không nhớ).
    //     NVS hồi phục → chính lệnh đó chạy được bình thường.
    wipe_nvs(); boot();
    g_nvs_write_fails = 1;
    ie_command_bus_dispatch(80, "dispense", NULL);
    assert(g_dispense_runs == 0);
    assert(strcmp(g_ack.status, "error") == 0);
    g_nvs_write_fails = 0;
    ie_command_bus_dispatch(80, "dispense", NULL);
    assert(g_dispense_runs == 1);

    // 11. NÂNG CẤP từ firmware chỉ có watermark (last_cmd=20, chưa có journal):
    //     id <= 20 có thể đã chạy → không chạy lại; id mới chạy.
    wipe_nvs(); g_nvs_last_cmd = 20; boot();
    ie_command_bus_dispatch(20, "dispense", NULL);
    ie_command_bus_dispatch(15, "dispense", NULL);
    assert(g_dispense_runs == 0 && is_uncertain());
    ie_command_bus_dispatch(21, "dispense", NULL);
    assert(g_dispense_runs == 1);
    assert(g_nvs_last_cmd == 21); // watermark vẫn được giữ cho bản cũ nếu rollback

    // 12. Journal HỎNG trong flash → vẫn khởi động (máy còn nhận lệnh vá), dựng lại
    //     từ watermark: lệnh cũ không chạy lại, lệnh mới chạy.
    wipe_nvs(); boot();
    ie_command_bus_dispatch(30, "dispense", NULL);
    memset(g_nvs_journal, 0xFF, sizeof(g_nvs_journal));
    boot();
    assert(g_faults == 1); // command_journal_reset
    ie_command_bus_dispatch(30, "dispense", NULL);
    assert(g_dispense_runs == 0 && is_uncertain());
    ie_command_bus_dispatch(31, "dispense", NULL);
    assert(g_dispense_runs == 1);

    // 13. Rơi khỏi cửa sổ 64 slot → "không chắc", không bao giờ thành ok/chạy lại.
    wipe_nvs(); boot();
    for (int64_t id = 100; id < 100 + IE_COMMAND_JOURNAL_SLOTS + 1; id++) {
        ie_command_bus_dispatch(id, "dispense", NULL);
    }
    int runs = g_dispense_runs;
    ie_command_bus_dispatch(100, "dispense", NULL);
    assert(g_dispense_runs == runs && is_uncertain());

    // 15. Lệch thứ tự SAU REBOOT: 11 chạy, máy khởi động lại, 10 mới tới → vẫn phải
    //     chạy (watermark last_cmd=11 không được biến 10 thành "quá cũ").
    wipe_nvs(); boot();
    ie_command_bus_dispatch(11, "dispense", NULL);
    boot();
    ie_command_bus_dispatch(10, "dispense", NULL);
    assert(g_dispense_runs == 1 && strcmp(g_ack.status, "ok") == 0);

    // 16. Firmware cũ (chỉ watermark) chạy lệnh 12, 13 trong lúc rollback rồi nâng
    //     cấp lại: last_cmd=13 vượt journal → 12, 13 "không chắc", 14 chạy.
    wipe_nvs(); boot();
    ie_command_bus_dispatch(11, "dispense", NULL);
    g_nvs_last_cmd = 13; // bản cũ ghi watermark, không biết journal
    boot();
    ie_command_bus_dispatch(12, "dispense", NULL);
    assert(g_dispense_runs == 0 && is_uncertain());
    ie_command_bus_dispatch(14, "dispense", NULL);
    assert(g_dispense_runs == 1);

    // 14. Registry: đăng ký lại = ghi đè; tham số sai bị từ chối; đầy thì báo lỗi.
    wipe_nvs(); boot();
    assert(ie_command_bus_register("a", h_dispense) == ESP_OK);
    assert(ie_command_bus_register("a", h_other) == ESP_OK);
    ie_command_bus_dispatch(90, "a", NULL);
    assert(g_dispense_runs == 0); // handler cũ đã bị thay
    assert(ie_command_bus_register(NULL, h_other) == ESP_ERR_INVALID_ARG);
    assert(ie_command_bus_register("", h_other) == ESP_ERR_INVALID_ARG);
    assert(ie_command_bus_register("b", NULL) == ESP_ERR_INVALID_ARG);
    char names[40][8];
    int accepted = 0;
    for (int i = 0; i < 40; i++) {
        snprintf(names[i], sizeof(names[i]), "a%d", i);
        if (ie_command_bus_register(names[i], h_other) == ESP_OK) accepted++;
    }
    assert(accepted == 24 - 2); // IE_CMD_REGISTRY_MAX trừ "dispense" và "a" đã có
    assert(ie_command_bus_register("tran", h_other) == ESP_ERR_NO_MEM);

    printf("PASS — 16 nhóm kiểm tra command bus (journal)\n");
    return 0;
}
