idf_component_register(
    SRCS "lora_receiver_main.c"
    PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_driver_uart
    INCLUDE_DIRS ""
)
