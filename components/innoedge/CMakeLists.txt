idf_component_register(
    SRCS "innoedge.c"
    INCLUDE_DIRS "include"
    REQUIRES
        command_bus
        config_store
        net
        payment_queue
        provisioning
        wifi_manager
        esp_event
        esp_netif
        esp_wifi
        json
        nvs_flash
)
