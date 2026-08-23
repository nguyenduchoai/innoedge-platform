// Test host cho command bus — phần rủi ro nhất của SDK: registry runtime +
// chống trùng lệnh. Chống trùng hỏng = máy NHẢ TIỀN HAI LẦN.
//
//   ./tests/run.sh

#include "gtek_command_bus.h"
#include "gtek_ws_client.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

// ── Cấy giả các phụ thuộc ───────────────────────────────────────────────────
static long long g_nvs_last_command_id;   // giả lập NVS (sống qua "reboot")
static int g_nvs_write_fails;

long long gtek_config_store_last_command_id(void) { return g_nvs_last_command_id; }

esp_err_t gtek_config_store_save_last_command_id(long long id)
{
    if (g_nvs_write_fails) return ESP_FAIL;
    g_nvs_last_command_id = id;
    return ESP_OK;
}

void gtek_fault_set(const char *code, const char *sev, const char *msg)
{ (void)code; (void)sev; (void)msg; }

// Ack cuối cùng bus gửi đi — test kiểm tra nội dung ack.
static struct { long long id; char status[16]; char message[64]; int count; } g_ack;

esp_err_t gtek_ws_client_send_command_ack(long long id, const char *status,
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

static esp_err_t h_dispense(cJSON *p, char *result, size_t rl, char *msg, size_t ml)
{
    (void)p; (void)msg; (void)ml;
    g_dispense_runs++;
    snprintf(result, rl, "{\"pulses\":2}");
    return ESP_OK;
}

static esp_err_t h_fail(cJSON *p, char *result, size_t rl, char *msg, size_t ml)
{
    (void)p; (void)result; (void)rl;
    snprintf(msg, ml, "het xu");
    return ESP_FAIL;
}

static esp_err_t h_other(cJSON *p, char *result, size_t rl, char *msg, size_t ml)
{
    (void)p; (void)result; (void)rl; (void)msg; (void)ml;
    return ESP_OK;
}

static void reset(long long nvs_last)
{
    g_nvs_last_command_id = nvs_last;
    g_nvs_write_fails = 0;
    g_dispense_runs = 0;
    memset(&g_ack, 0, sizeof(g_ack));
    assert(gtek_command_bus_init() == ESP_OK);
}

int main(void)
{
    // 1. Đăng ký + dispatch cơ bản.
    reset(0);
    assert(gtek_command_bus_register("dispense", h_dispense) == ESP_OK);
    gtek_command_bus_dispatch(10, "dispense", NULL);
    assert(g_dispense_runs == 1);
    assert(strcmp(g_ack.status, "ok") == 0);

    // 2. Action chưa đăng ký → ack error, KHÔNG chạy handler nào.
    gtek_command_bus_dispatch(11, "khong_ton_tai", NULL);
    assert(g_dispense_runs == 1);
    assert(strcmp(g_ack.status, "error") == 0);
    assert(strcmp(g_ack.message, "unknown action") == 0);

    // 3. Thiếu action → ack error.
    gtek_command_bus_dispatch(12, NULL, NULL);
    assert(strcmp(g_ack.message, "missing action") == 0);

    // 4. TRÙNG cùng phiên: cloud gửi lại id cũ → chỉ ack lại, KHÔNG nhả tiền lần hai.
    reset(0);
    assert(gtek_command_bus_register("dispense", h_dispense) == ESP_OK);
    gtek_command_bus_dispatch(20, "dispense", NULL);
    gtek_command_bus_dispatch(20, "dispense", NULL);
    assert(g_dispense_runs == 1);
    assert(strcmp(g_ack.status, "ok") == 0);
    assert(strcmp(g_ack.message, "duplicate") == 0);

    // 5. TRÙNG QUA REBOOT — ca đã từng gây nhả tiền hai lần ngoài hiện trường.
    //    NVS giữ watermark=20; sau reboot cloud gửi lại id 20 và cả id 15 cũ hơn.
    reset(20);
    assert(gtek_command_bus_register("dispense", h_dispense) == ESP_OK);
    gtek_command_bus_dispatch(20, "dispense", NULL);
    gtek_command_bus_dispatch(15, "dispense", NULL);
    assert(g_dispense_runs == 0); // không chạy lần nào
    gtek_command_bus_dispatch(21, "dispense", NULL); // lệnh MỚI vẫn phải chạy
    assert(g_dispense_runs == 1);
    assert(g_nvs_last_command_id == 21); // watermark đã nâng

    // 6. Watermark chỉ NÂNG, không hạ (lệnh đến không đúng thứ tự).
    reset(0);
    assert(gtek_command_bus_register("x", h_other) == ESP_OK);
    gtek_command_bus_dispatch(50, "x", NULL);
    gtek_command_bus_dispatch(40, "x", NULL); // cũ hơn → coi là trùng
    assert(g_nvs_last_command_id == 50);

    // 7. Handler lỗi → ack "error" kèm message của handler; vẫn tính là đã xử lý
    //    (không gửi lại vô hạn).
    reset(0);
    assert(gtek_command_bus_register("fail", h_fail) == ESP_OK);
    gtek_command_bus_dispatch(60, "fail", NULL);
    assert(strcmp(g_ack.status, "error") == 0);
    assert(strcmp(g_ack.message, "het xu") == 0);

    // 8. commandId <= 0 → không dùng cơ chế dedupe, luôn chạy.
    reset(0);
    assert(gtek_command_bus_register("dispense", h_dispense) == ESP_OK);
    gtek_command_bus_dispatch(0, "dispense", NULL);
    gtek_command_bus_dispatch(0, "dispense", NULL);
    assert(g_dispense_runs == 2);

    // 9. Đăng ký lại cùng action = ghi đè handler.
    reset(0);
    assert(gtek_command_bus_register("a", h_dispense) == ESP_OK);
    assert(gtek_command_bus_register("a", h_other) == ESP_OK);
    gtek_command_bus_dispatch(70, "a", NULL);
    assert(g_dispense_runs == 0); // handler cũ đã bị thay

    // 10. Tham số đăng ký sai → từ chối, không làm hỏng registry.
    assert(gtek_command_bus_register(NULL, h_other) == ESP_ERR_INVALID_ARG);
    assert(gtek_command_bus_register("", h_other) == ESP_ERR_INVALID_ARG);
    assert(gtek_command_bus_register("b", NULL) == ESP_ERR_INVALID_ARG);

    // 11. Registry đầy → báo lỗi rõ ràng, không tràn mảng.
    reset(0);
    char names[64][8];
    int accepted = 0;
    for (int i = 0; i < 40; i++) {
        snprintf(names[i], sizeof(names[i]), "a%d", i);
        if (gtek_command_bus_register(names[i], h_other) == ESP_OK) accepted++;
    }
    assert(accepted == 24); // GTEK_CMD_REGISTRY_MAX
    assert(gtek_command_bus_register("tran", h_other) == ESP_ERR_NO_MEM);

    // 12. NVS ghi hỏng → vẫn ack, vẫn dedupe trong phiên (chỉ mất tính bền).
    reset(0);
    assert(gtek_command_bus_register("dispense", h_dispense) == ESP_OK);
    g_nvs_write_fails = 1;
    gtek_command_bus_dispatch(80, "dispense", NULL);
    gtek_command_bus_dispatch(80, "dispense", NULL);
    assert(g_dispense_runs == 1);

    printf("PASS — 12 nhóm kiểm tra command bus\n");
    return 0;
}
