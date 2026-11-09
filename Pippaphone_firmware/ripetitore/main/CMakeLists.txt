# I file dello schermo e della batteria servono solo alle schede che li hanno. (Le REQUIRES
# invece non possono dipendere dalla configurazione: si leggono prima di sdkconfig.)
set(srcs "main.cpp")
if(CONFIG_RIPETITORE_SCHEDA_HELTEC_V3 OR CONFIG_RIPETITORE_SCHEDA_T3S3)
    list(APPEND srcs "oled.cpp" "schermo.cpp")
endif()
if(CONFIG_RIPETITORE_SCHEDA_T3S3)
    list(APPEND srcs "batteria.cpp")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "."
                       REQUIRES pippanet pippa_radio scheda nvs_flash esp_driver_gpio esp_driver_usb_serial_jtag
                                esp_vfs_console esp_driver_i2c esp_driver_uart esp_adc)
