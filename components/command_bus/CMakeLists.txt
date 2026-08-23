idf_component_register(
    SRCS "gtek_command_bus.c"
    INCLUDE_DIRS "."
    REQUIRES
        config_store
        net
        json
)
