file(GLOB COMMON_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/*.c"
)

idf_component_register(
    SRCS 
        "main.c"
        ${COMMON_SOURCES}
        "gpio/gpio.c"
        "serial/serial.c"

    INCLUDE_DIRS 
        "."
        "../../../../include"
    
    REQUIRES 
        esp_driver_uart
        esp_driver_gpio
)