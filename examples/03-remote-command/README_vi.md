# 03 — Remote Command

[English](README.md) | [Tiếng Việt](README_vi.md)


Cloud gửi lệnh xuống máy, máy thực thi và trả kết quả về.

## Phần cứng
Devkit + 1 LED ở GPIO2 (đổi `LED_GPIO` cho bo của bạn). Không có LED vẫn chạy được.

## Giao thức (SDK lo, để tham khảo)
```jsonc
// cloud → máy
{"type":"command","commandId":91,"action":"led","params":{"on":true}}
// máy → cloud
{"type":"command_ack","commandId":91,"status":"ok","message":"LED da bat","result":{"on":true}}
```

## Viết một lệnh mới
```c
static esp_err_t cmd_xyz(cJSON *params, char *result, size_t result_len,
                         char *msg, size_t msg_len)
{
    // ... làm việc ...
    snprintf(result, result_len, "{\"count\":%d}", n); // vào field "result"
    snprintf(msg, msg_len, "xong");                    // vào field "message"
    return ESP_OK;                                     // lỗi khác → ack "error"
}

innoedge_register_command("xyz", cmd_xyz);   // 1 dòng, xong
```

## Ba luật bắt buộc

**1. Không block lâu.** Handler chạy trên task WebSocket. Việc > vài giây (tải
file, chờ khách bấm) → `xTaskCreate` rồi trả `ESP_OK` ngay.

**2. Không reboot thẳng trong handler.** `esp_restart()` chạy trước khi ack kịp
gửi → cloud tưởng lệnh hỏng và gửi lại. Dùng `innoedge_reboot_after_ack()`.

**3. `action` phải là string literal.** Registry giữ con trỏ, không copy chuỗi.

## Chống trùng (quan trọng nhất khi lệnh có tiền)

Cloud gửi lại lệnh sau khi mạng rớt là chuyện bình thường. SDK giữ
**high-watermark `commandId` trong NVS**: mọi `commandId ≤` watermark bị coi là
đã xử lý → chỉ ack lại, **không gọi handler lần hai**. Watermark sống qua
reboot, nên mất điện giữa lúc nhả tiền cũng không nhả hai lần.

Đánh dấu xảy ra **trước** khi handler chạy — cố ý: thà bỏ sót một lệnh còn hơn
nhả tiền hai lần.

Kiểm chứng: gửi cùng `commandId` hai lần → lần hai trả
`{"status":"ok","message":"duplicate"}` và LED không đổi.

## Troubleshooting
| Triệu chứng | Nguyên nhân |
|---|---|
| `unknown action` | Quên `innoedge_register_command`, hoặc gõ sai tên action |
| Lệnh chạy nhưng cloud báo timeout | Handler block quá lâu → đẩy sang task riêng |
| Lệnh chạy hai lần | Đang gọi `gtek_command_bus_dispatch` thủ công thay vì để SDK gọi |
| `registry đầy` | Quá 24 action — sửa `GTEK_CMD_REGISTRY_MAX` |
