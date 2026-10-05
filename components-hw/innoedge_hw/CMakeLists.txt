# Driver phần cứng mẫu — KHÔNG thuộc SDK, KHÔNG cam kết API ổn định.
#
# Gộp một component vì lý do như components/innoedge: tên component là toàn cục
# trong project ESP-IDF, phát hành "relay_control" ra registry công khai là đặt
# mìn xung đột.
idf_component_register(
    SRCS
        "src/innoedge_pulse_input.c"
        "src/innoedge_relay_control.c"
        "src/innoedge_wash_control.c"
        "src/innoedge_mdb.c"
        "src/innoedge_modbus.c"
    INCLUDE_DIRS "src"
    REQUIRES innoedge
    PRIV_REQUIRES driver esp_timer
)
