file(GLOB COMMON_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/*.c"
)

idf_component_register(
    SRCS 
        "main.c"
        ${COMMON_SOURCES}
        "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/coreJSON/core_json.c"
        "gpio/gpio.c"
        "serial/serial.c"

    INCLUDE_DIRS 
        "."
        "${CMAKE_CURRENT_LIST_DIR}/../../../../include"
        "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/coreJSON"
    
    REQUIRES 
        esp_driver_uart
        esp_driver_gpio
)
