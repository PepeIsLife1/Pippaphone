idf_component_register(SRCS "main.cpp"
                       INCLUDE_DIRS "."
                       REQUIRES pippanet pippa_radio scheda nvs_flash esp_driver_gpio esp_driver_usb_serial_jtag
                                esp_vfs_console)
