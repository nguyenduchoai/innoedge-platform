idf_component_register(
    SRCS "gtek_config_store.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_hw_support
        nvs_flash
)
