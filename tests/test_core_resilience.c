// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 InnoEdge
// Test suite kiểm thử toàn diện các phân hệ cốt lõi tăng cường (CORE Resilience):
// 1. Blackbox Crash Recorder & Diagnostics
// 2. Deterministic Memory Pool & Ring Buffer
// 3. Multi-WAN Failover Engine
// 4. Fleet Clustering Protocol
// 5. Hardware Cryptographic Signing (TAC)

#include "gtek_blackbox.h"
#include "gtek_cluster.h"
#include "gtek_crypto.h"
#include "gtek_mem_pool.h"
#include "gtek_net_failover.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifndef ESP_PLATFORM
void gtek_blackbox_set_mock_reset_reason(int reason);
#endif

// ── 1. Kiểm tra Blackbox Crash Flight Recorder ──────────────────────────────
static void test_blackbox(void)
{
    printf("  [Blackbox] Kiểm tra ghi vết breadcrumb và báo cáo chẩn đoán...\n");

#ifndef ESP_PLATFORM
    gtek_blackbox_set_mock_reset_reason(5); // giả lập TASK_WDT
#endif
    assert(gtek_blackbox_init() == ESP_OK);
    assert(gtek_blackbox_has_pending_report() == true);

    gtek_blackbox_record_breadcrumb("CMD", "dispense_start");
    gtek_blackbox_record_breadcrumb("RELAY", "pulse_1");
    gtek_blackbox_record_breadcrumb("RELAY", "pulse_2");

    gtek_breadcrumb_t crumbs[10];
    size_t count = gtek_blackbox_get_breadcrumbs(crumbs, 10);
    assert(count >= 3);

    char report[512];
    assert(gtek_blackbox_format_report(report, sizeof(report)) == ESP_OK);
    assert(strstr(report, "\"type\":\"diagnostic\"") != NULL);
    assert(strstr(report, "dispense_start") != NULL);
    assert(strstr(report, "pulse_2") != NULL);

    gtek_blackbox_clear_report();
    assert(gtek_blackbox_has_pending_report() == false);

    printf("  [Blackbox] PASS: Hộp đen chẩn đoán hoạt động chính xác!\n");
}

// ── 2. Kiểm tra Deterministic Memory Pool & Ring Buffer ──────────────────────
static void test_mem_pool_and_ring_buf(void)
{
    printf("  [MemPool] Kiểm tra Static Memory Pool và Ring Buffer...\n");

    assert(gtek_mem_pool_init() == ESP_OK);

    // Cấp phát khối nhỏ 64B từ pool 128B
    void *p1 = gtek_mem_pool_alloc(64);
    assert(p1 != NULL);
    memset(p1, 0xAA, 64);

    // Cấp phát khối lớn 1024B từ pool 2048B
    void *p2 = gtek_mem_pool_alloc(1024);
    assert(p2 != NULL);

    // Giải phóng và tái cấp phát (không gây phân mảnh)
    gtek_mem_pool_free(p1);
    void *p3 = gtek_mem_pool_alloc(128);
    assert(p3 == p1); // tái sử dụng chính xác khối vừa giải phóng
    gtek_mem_pool_free(p2);
    gtek_mem_pool_free(p3);

    // Kiểm tra Ring Buffer
    uint8_t storage[256];
    gtek_ring_buf_t rb;
    assert(gtek_ring_buf_init(&rb, storage, sizeof(storage)) == ESP_OK);

    uint8_t write_data[100];
    for (int i = 0; i < 100; i++) write_data[i] = (uint8_t)i;

    assert(gtek_ring_buf_write(&rb, write_data, 100) == 100);
    assert(gtek_ring_buf_available(&rb) == 100);

    uint8_t read_data[50];
    assert(gtek_ring_buf_read(&rb, read_data, 50) == 50);
    assert(read_data[0] == 0 && read_data[49] == 49);
    assert(gtek_ring_buf_available(&rb) == 50);

    // Ghi vòng (Wrap-around write)
    uint8_t fill[180];
    memset(fill, 0xBB, sizeof(fill));
    assert(gtek_ring_buf_write(&rb, fill, sizeof(fill)) == sizeof(fill));
    assert(gtek_ring_buf_available(&rb) == 50 + 180);

    printf("  [MemPool] PASS: Bộ nhớ xác định & Ring Buffer hoạt động hoàn hảo!\n");
}

// ── 3. Kiểm tra Multi-WAN Failover Engine ────────────────────────────────────
static void test_net_failover(void)
{
    printf("  [Failover] Kiểm tra chuyển mạch WiFi ↔ 4G LTE...\n");

    assert(gtek_net_failover_init() == ESP_OK);
    assert(gtek_net_failover_get_active() == INNOEDGE_NET_PRIMARY);

    // 1 lần mất ping → suy giảm (DEGRADED), nhưng chưa chuyển mạch
    gtek_net_failover_report_ping_lost();
    assert(gtek_net_failover_get_active() == INNOEDGE_NET_PRIMARY);
    assert(gtek_net_failover_get_state() == GTEK_FAILOVER_STATE_DEGRADED);

    // Thêm 2 lần mất ping nữa (tổng cộng 3) → kích hoạt failover sang 4G LTE
    gtek_net_failover_report_ping_lost();
    gtek_net_failover_report_ping_lost();
    assert(gtek_net_failover_get_active() == INNOEDGE_NET_SECONDARY);
    assert(gtek_net_failover_get_state() == GTEK_FAILOVER_STATE_SECONDARY_ACTIVE);

    // WiFi phục hồi → chuyển sang trạng thái chờ ổn định
    gtek_net_failover_report_link(INNOEDGE_NET_PRIMARY, true);
    assert(gtek_net_failover_get_state() == GTEK_FAILOVER_STATE_RECOVERING);

    // Giả lập 30 giây ổn định
    for (int sec = 0; sec < 29; sec++) {
        assert(gtek_net_failover_tick() == false); // chưa chuyển
    }
    // Giây thứ 30 → chuyển ngược lại WiFi chính
    assert(gtek_net_failover_tick() == true);
    assert(gtek_net_failover_get_active() == INNOEDGE_NET_PRIMARY);
    assert(gtek_net_failover_get_state() == GTEK_FAILOVER_STATE_PRIMARY_OK);

    printf("  [Failover] PASS: Tự động chuyển mạng và phục hồi thành công!\n");
}

// ── 4. Kiểm tra Fleet Clustering Protocol ───────────────────────────────────
static void test_cluster_protocol(void)
{
    printf("  [Cluster] Kiểm tra giao thức gom cụm Master-Worker...\n");

    assert(gtek_cluster_init(INNOEDGE_CLUSTER_MASTER) == ESP_OK);
    assert(gtek_cluster_get_role() == INNOEDGE_CLUSTER_MASTER);

    char pay_buf[256];
    assert(gtek_cluster_pack_payment("WASHER_04", 0, 3, 30000, pay_buf, sizeof(pay_buf)) == ESP_OK);
    assert(strstr(pay_buf, "\"subnode\":\"WASHER_04\"") != NULL);
    assert(strstr(pay_buf, "\"amount\":30000") != NULL);

    const char *cmd_json = "{\"type\":\"cluster_cmd\",\"subnode\":\"WASHER_04\",\"commandId\":108,\"action\":\"unlock_hatch\",\"params\":{\"sec\":10}}";
    char subnode[32];
    long long cmd_id = 0;
    char action[32];
    char params[64];
    assert(gtek_cluster_unpack_command(cmd_json, subnode, sizeof(subnode), &cmd_id,
                                       action, sizeof(action), params, sizeof(params)) == ESP_OK);
    assert(strcmp(subnode, "WASHER_04") == 0);
    assert(cmd_id == 108);
    assert(strcmp(action, "unlock_hatch") == 0);

    char ack_buf[256];
    assert(gtek_cluster_pack_command_ack("WASHER_04", 108, "ok", "unlocked", ack_buf, sizeof(ack_buf)) == ESP_OK);
    assert(strstr(ack_buf, "\"status\":\"ok\"") != NULL);

    printf("  [Cluster] PASS: Gói tin Mesh Master/Worker chính xác!\n");
}

// ── 5. Kiểm tra Hardware Cryptographic Signing (TAC) ────────────────────────
static void test_crypto_signing(void)
{
    printf("  [Crypto] Kiểm tra sinh và xác thực chữ ký giao dịch TAC HMAC-SHA256...\n");

    uint8_t test_key[32] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    assert(gtek_crypto_init(test_key, 8) == ESP_OK);

    const char *mac = "AABBCCDDEEFF";
    char tac[GTEK_TAC_HEX_LEN + 1];

    assert(gtek_crypto_sign_transaction(mac, 1042, 0, 2, 20000, tac, sizeof(tac)) == ESP_OK);
    assert(strlen(tac) == 64);

    // Xác thực hợp lệ
    assert(gtek_crypto_verify_transaction(mac, 1042, 0, 2, 20000, tac) == true);

    // Phát hiện sửa đổi trái phép dữ liệu (ví dụ: hacker sửa amount trong NVS từ 20000 thành 50000)
    assert(gtek_crypto_verify_transaction(mac, 1042, 0, 2, 50000, tac) == false);
    // Sửa seq
    assert(gtek_crypto_verify_transaction(mac, 1043, 0, 2, 20000, tac) == false);

    printf("  [Crypto] PASS: Chữ ký TAC bảo vệ giao dịch chống giả mạo 100%%!\n");
}

int main(void)
{
    printf("====================================================\n");
    printf("  BẮT ĐẦU KIỂM THỬ CORE RESILIENCE (INNOEDGE SDK)\n");
    printf("====================================================\n");

    test_blackbox();
    test_mem_pool_and_ring_buf();
    test_net_failover();
    test_cluster_protocol();
    test_crypto_signing();

    printf("====================================================\n");
    printf("PASS — Toàn bộ kiểm tra CORE Tăng Cường đều thành công!\n");
    printf("====================================================\n");
    return 0;
}
