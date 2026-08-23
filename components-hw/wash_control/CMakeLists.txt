idf_component_register(
    SRCS "gtek_wash_control.c"
    INCLUDE_DIRS "."
    REQUIRES
        relay_control
        net
        driver
        esp_timer
)
