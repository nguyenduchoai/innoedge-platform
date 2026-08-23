idf_component_register(
    SRCS "gtek_payment_queue.c"
    INCLUDE_DIRS "."
    REQUIRES
        json
        nvs_flash
)
