# Driver audio mẫu — KHÔNG thuộc SDK, KHÔNG cam kết API ổn định.
idf_component_register(
    SRCS "src/innoedge_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES innoedge
    PRIV_REQUIRES driver json esp_timer
)
