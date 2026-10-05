# InnoEdge Linux SBC Daemon Agent (`innoedge-agent`)

Daemon tiến trình nền nhẹ (Single Binary < 10MB) chạy trên **Raspberry Pi, Banana Pi, Orange Pi và Linux x86/ARM64**.

---

## 💡 Tại Sao Cần `innoedge-agent`?

Trên các máy tính nhúng Linux SBC như Raspberry Pi hay Banana Pi, lập trình viên thường xây dựng giao diện Kiosk cảm ứng bằng các framework đồ họa cao cấp như **Electron, React, Vue, Flutter, Qt C++** hoặc **Python Tkinter**.

Thay vì mỗi ứng dụng giao diện phải tự cài đặt WebSocket và xử lý sổ cái NVS:
* **`innoedge-agent` chạy như một systemd service:** Lo toàn bộ việc kết nối Cloud, duy trì sổ cái bền vững, chống trùng lặp lệnh `commandId`, và kết nối cổng thanh toán VietQR.
* **Mọi ứng dụng Frontend chỉ cần giao tiếp qua REST API cục bộ (`http://127.0.0.1:8089`):**
  * Xin mã VietQR: `curl -X POST http://127.0.0.1:8089/api/pay/qr -d '{"amount_vnd":25000}'`
  * Nhận sự kiện tiền về thời gian thực: Mở EventSource Server-Sent Events tại `http://127.0.0.1:8089/api/events`

---

## 🚀 Hướng Dẫn Cài Đặt Trên Raspberry Pi / Banana Pi

```bash
# 1. Biên dịch binary
cd innoedge-sdk-esp32/linux/agent
go build -o innoedge-agent main.go

# 2. Cài đặt vào hệ thống
sudo cp innoedge-agent /usr/local/bin/
sudo cp innoedge.service /etc/systemd/system/

# 3. Kích hoạt dịch vụ tự khởi động cùng máy
sudo systemctl daemon-reload
sudo systemctl enable innoedge
sudo systemctl start innoedge

# Kiểm tra log
sudo journalctl -u innoedge -f
```
