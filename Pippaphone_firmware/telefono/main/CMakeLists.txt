file(GLOB APP_SRCS ${CMAKE_CURRENT_LIST_DIR}/app/*.cpp)
file(GLOB UI_SRCS ${CMAKE_CURRENT_LIST_DIR}/ui/*.cpp)
file(GLOB FONT_SRCS ${CMAKE_CURRENT_LIST_DIR}/ui/font/*.c)
idf_component_register(SRCS "main.cpp" ${APP_SRCS} ${UI_SRCS} ${FONT_SRCS}
                       INCLUDE_DIRS "."
                       REQUIRES pippanet pippa_radio scheda lvgl codec2 nvs_flash fatfs wear_levelling esp_wifi
                                esp_http_server app_update esp_app_format esp_netif esp_event esp_timer esp_lcd
                                esp_hw_support esp_partition freertos)
# I font generati includono "lvgl/lvgl.h".
target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_LVGL_H_INCLUDE_SIMPLE)
