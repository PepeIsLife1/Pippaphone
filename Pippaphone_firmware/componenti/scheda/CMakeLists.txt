# Driver della PCB del Pippaphone (rev. 1.1): expander, tastiera, batteria, display, audio, GNSS.
idf_component_register(SRCS "src/i2c.cpp"
                            "src/expander.cpp"
                            "src/tastiera.cpp"
                            "src/batteria.cpp"
                            "src/display.cpp"
                            "src/audio.cpp"
                            "src/gnss.cpp"
                            "src/scheda.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_driver_i2c esp_driver_gpio esp_driver_ledc esp_driver_i2s esp_driver_uart
                                esp_lcd esp_timer esp_hw_support freertos)
