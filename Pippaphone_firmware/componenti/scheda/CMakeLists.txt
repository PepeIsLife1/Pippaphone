# Driver della PCB del Pippaphone (rev. 1.1): expander, tastiera, batteria, display, audio, GNSS.
# Con CONFIG_SCHEDA_TDECK_PLUS al posto della PCB si compilano i driver del LilyGO T-Deck Plus
# (cartella src/tdeck); i file della PCB in comune hanno le parti del T-Deck in blocchi #if.
if(CONFIG_SCHEDA_TDECK_PLUS)
    set(srcs "src/i2c.cpp"
             "src/tastiera.cpp"
             "src/audio.cpp"
             "src/gnss.cpp"
             "src/tdeck/gnss_pilota.cpp"
             "src/tdeck/scheda_tdeck.cpp"
             "src/tdeck/display_tdeck.cpp"
             "src/tdeck/tastiera_tdeck.cpp"
             "src/tdeck/batteria_tdeck.cpp"
             "src/tdeck/es7210.cpp")
else()
    set(srcs "src/i2c.cpp"
             "src/expander.cpp"
             "src/tastiera.cpp"
             "src/batteria.cpp"
             "src/display.cpp"
             "src/audio.cpp"
             "src/gnss.cpp"
             "src/scheda.cpp")
endif()
idf_component_register(SRCS ${srcs}
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_ledc esp_driver_i2s esp_driver_uart
                                esp_lcd esp_timer esp_hw_support freertos esp_driver_spi
                       PRIV_REQUIRES esp_adc esp_driver_usb_serial_jtag)
