idf_component_register(
    SRCS "gtek_provisioning.c"
    INCLUDE_DIRS "."
    REQUIRES
        bt
        config_store
        freertos
        json
        wifi_manager
)
