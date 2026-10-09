<a id="top"></a>
# InnoEdge HW Drivers (`innoedge_hw`) — Driver Phần Cứng Ngoại Vi

> 🇻🇳 **Tài liệu Tiếng Việt** | [🇬🇧 English Documentation](#english)

Bộ driver phần cứng ngoại vi mẫu cho nền tảng IoT vận hành bằng tiền [InnoEdge Platform](https://components.espressif.com/components/nguyenduchoai/innoedge).

---

## 🇻🇳 Tính năng chính (Features)

- **Bộ đếm xung tiền (`innoedge_pulse_input`)**: Đếm xung phần cứng kèm thuật toán chống rung tiếp điểm (software debouncing) chuyên dụng cho đầu đọc tiền xu, đầu nhận tiền giấy (tích lũy xung, phát sự kiện khi hết thời gian timeout).
- **Điều khiển Relay (`innoedge_relay_control`)**: Driver điều khiển relay GPIO đa kênh với chế độ kích xung nhả hàng / nhả xu không chặn (non-blocking pulse dispense) cho máy bán hàng tự động.
- **Điều khiển phiên rửa xe (`innoedge_wash_control`)**: Bộ điều khiển phiên dịch vụ theo ngân sách thời gian linh hoạt cho máy rửa xe tự phục vụ và trạm dịch vụ tính giờ.
- **Giao thức MDB (`innoedge_mdb`)**: Hỗ trợ giao thức thanh toán không tiền mặt Multi-Drop Bus (MDB / ICP) chuẩn quốc tế cho máy bán hàng tự động.
- **Modbus RTU (`innoedge_modbus`)**: Giao tiếp công nghiệp Modbus RTU RS485 kèm kiểm tra toàn vẹn CRC16.

## Cài đặt vào dự án ESP-IDF

Thêm component vào dự án ESP-IDF của bạn:

```bash
idf.py add-dependency "nguyenduchoai/innoedge_hw^0.1.2"
```

Hoặc khai báo trực tiếp trong `main/idf_component.yml`:

```yaml
dependencies:
  nguyenduchoai/innoedge: "^0.2.0"
  nguyenduchoai/innoedge_hw: "^0.1.2"
```

---

<a id="english"></a>
## 🇬🇧 English Documentation

> [🇻🇳 Quay lại Tiếng Việt](#top)

Sample hardware peripheral drivers for the [InnoEdge IoT monetization platform](https://components.espressif.com/components/nguyenduchoai/innoedge).

### Features

- **Pulse Input (`innoedge_pulse_input`)**: Hardware pulse counter with software debouncing for coin/bill acceptors (multi-pulse accumulator, timeout emission).
- **Relay Control (`innoedge_relay_control`)**: Multi-channel GPIO relay driver with non-blocking pulse dispense mode for vending actuators.
- **Wash Session Controller (`innoedge_wash_control`)**: Budget-driven timed session controller for self-service carwash kiosks.
- **MDB Protocol (`innoedge_mdb`)**: Multi-Drop Bus (MDB / ICP) cashless payment protocol support.
- **Modbus RTU (`innoedge_modbus`)**: Industrial Modbus RTU RS485 communication with CRC16 validation.

### Installation

Add to your ESP-IDF project:

```bash
idf.py add-dependency "nguyenduchoai/innoedge_hw^0.1.2"
```

Or declare in `idf_component.yml`:

```yaml
dependencies:
  nguyenduchoai/innoedge: "^0.2.0"
  nguyenduchoai/innoedge_hw: "^0.1.2"
```

### License

Apache-2.0 License.
