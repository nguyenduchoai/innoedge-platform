idf_component_register(
    SRCS "gtek_wifi_manager.c"
    INCLUDE_DIRS "."
    REQUIRES
        config_store
        esp_http_server
        esp_event
        esp_netif
        esp_wifi
        json
)
