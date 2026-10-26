idf_component_register(SRCS "board.c" "usb_host_fifo.c" "st7796.c" "storage_probe.c" INCLUDE_DIRS "." REQUIRES esp_lcd esp_driver_i2c esp_driver_gpio esp_driver_spi esp_timer lvgl fatfs hal)

target_link_options(${COMPONENT_LIB} INTERFACE "-Wl,--wrap=usb_dwc_hal_set_fifo_bias" "-Wl,-u,__wrap_usb_dwc_hal_set_fifo_bias")
