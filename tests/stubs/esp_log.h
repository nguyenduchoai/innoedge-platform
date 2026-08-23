#pragma once
#include <stdio.h>
// Log im lặng khi chạy test, nhưng compiler VẪN kiểm tra format string —
// bắt được lỗi kiểu %d cho int64_t (in sai số tiền trên máy thật).
#define IE_TEST_LOG(tag, ...) do { (void)(tag); if (0) printf(__VA_ARGS__); } while (0)
#define ESP_LOGE(tag, ...) IE_TEST_LOG(tag, __VA_ARGS__)
#define ESP_LOGW(tag, ...) IE_TEST_LOG(tag, __VA_ARGS__)
#define ESP_LOGI(tag, ...) IE_TEST_LOG(tag, __VA_ARGS__)
#define ESP_LOGD(tag, ...) IE_TEST_LOG(tag, __VA_ARGS__)
