# Radio SX1262 (RadioLib) collegata a un nodo PippaNet, comune a telefono e ripetitore.
idf_component_register(SRCS "src/esp_hal.cpp" "src/radio_link.cpp"
                       INCLUDE_DIRS "include"
                       REQUIRES RadioLib pippanet esp_driver_spi esp_driver_gpio esp_timer freertos)
