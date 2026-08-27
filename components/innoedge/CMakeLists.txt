# InnoEdge SDK — MỘT component duy nhất.
#
# Trước đây tách thành 6 component (net/config_store/…). Gộp lại vì tên component
# là TOÀN CỤC trong một project ESP-IDF: phát hành ra registry công khai một
# component tên "net" hay "config_store" là đặt mìn xung đột cho mọi người dùng.
# Một component = một tên, một phiên bản, một lệnh cài.
idf_component_register(
    SRCS
        "src/gtek_asset_client.c"
        "src/gtek_command_bus.c"
        "src/gtek_config_client.c"
        "src/gtek_config_store.c"
        "src/gtek_fault.c"
        "src/gtek_ota_client.c"
        "src/gtek_payment_queue.c"
        "src/gtek_provisioning.c"
        "src/gtek_voucher_client.c"
        "src/gtek_wifi_manager.c"
        "src/gtek_ws_client.c"
        "src/innoedge.c"
    # Hợp đồng công khai DUY NHẤT là include/innoedge.h. src/ cũng nằm trong
    # include path vì lớp sản phẩm (firmware-gtek, example 08) còn gọi vài API
    # nội bộ — dùng được, nhưng KHÔNG cam kết giữ tương thích.
    INCLUDE_DIRS "include" "src"
    REQUIRES
        esp_event
        esp_netif
        esp_wifi
        json
        nvs_flash
    PRIV_REQUIRES
        app_update
        bt
        esp_hw_support
        esp_http_client
        esp_http_server
        esp_https_ota
        esp_websocket_client
        freertos
)
