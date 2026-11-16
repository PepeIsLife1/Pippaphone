file(GLOB APP_SRCS ${CMAKE_CURRENT_LIST_DIR}/app/*.cpp)
file(GLOB UI_SRCS ${CMAKE_CURRENT_LIST_DIR}/ui/*.cpp)
file(GLOB FONT_SRCS ${CMAKE_CURRENT_LIST_DIR}/ui/font/*.c)
# minimp3 (musica), il driver della USB-Serial-JTAG (mappe dal computer) e usb_audio (cuffie USB-C) li usa
# solo il T-Deck Plus: nella fase in cui ESP-IDF raccoglie i componenti la configurazione non si vede
# ancora, quindi sono sempre nell'elenco; sulla PCB il codice non li chiama e il linker non li mette nel
# firmware. usb_audio sulla PCB e' vuoto (solo header) e non porta dentro il componente usb di ESP-IDF
# (vedi componenti/usb_audio/CMakeLists.txt).
set(EXTRA_REQUIRES minimp3 esp_driver_usb_serial_jtag usb_audio)
idf_component_register(SRCS "main.cpp" ${APP_SRCS} ${UI_SRCS} ${FONT_SRCS}
                       INCLUDE_DIRS "."
                       REQUIRES pippanet pippa_radio scheda lvgl codec2 nvs_flash fatfs wear_levelling esp_wifi
                                esp_http_server app_update esp_app_format esp_netif esp_event esp_timer esp_lcd
                                esp_hw_support esp_partition freertos sdmmc esp_driver_sdspi ${EXTRA_REQUIRES})
# I font generati includono "lvgl/lvgl.h".
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
