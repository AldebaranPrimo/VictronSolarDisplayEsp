# Simple display with Victron BLE - NO LVGL, NO WiFi
set(SIMPLE_SOURCES
    "main_simple.c"
    "simple_display.c"
)

idf_component_register(
    SRCS       ${SIMPLE_SOURCES}
    INCLUDE_DIRS "."
    PRIV_REQUIRES
        driver
        esp_lcd
        nvs_flash
        bt
        victron_ble
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    LOG_LOCAL_LEVEL=ESP_LOG_VERBOSE
)
