idf_component_register(
    SRCS "app_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        innoedge
        relay_control
        wash_control
        net
        json
)
