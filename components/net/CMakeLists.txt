idf_component_register(
    SRCS
        "gtek_asset_client.c"
        "gtek_config_client.c"
        "gtek_fault.c"
        "gtek_ota_client.c"
        "gtek_voucher_client.c"
        "gtek_ws_client.c"
    INCLUDE_DIRS "."
    REQUIRES
        app_update
        config_store
        esp_http_client
        esp_https_ota
        esp_websocket_client
        json
)
