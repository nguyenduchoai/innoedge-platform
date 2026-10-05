# InnoEdge HW Drivers (`innoedge_hw`)

Sample hardware peripheral drivers for the [InnoEdge IoT monetization platform](https://components.espressif.com/components/nguyenduchoai/innoedge).

## Features

- **Pulse Input (`gtek_pulse_input`)**: Hardware pulse counter with software debouncing for coin/bill acceptors (multi-pulse accumulator, timeout emission).
- **Relay Control (`gtek_relay_control`)**: Multi-channel GPIO relay driver with non-blocking pulse dispense mode for vending actuators.
- **Wash Session Controller (`gtek_wash_control`)**: Budget-driven timed session controller for self-service carwash kiosks.
- **MDB Protocol (`innoedge_mdb`)**: Multi-Drop Bus (MDB / ICP) cashless payment protocol support.
- **Modbus RTU (`innoedge_modbus`)**: Industrial Modbus RTU RS485 communication with CRC16 validation.

## Installation

Add to your ESP-IDF project:

```bash
idf.py add-dependency "nguyenduchoai/innoedge_hw^0.1.0"
```

Or declare in `idf_component.yml`:

```yaml
dependencies:
  nguyenduchoai/innoedge: "^0.1.2"
  nguyenduchoai/innoedge_hw: "^0.1.0"
```

## License

Apache-2.0 License.
