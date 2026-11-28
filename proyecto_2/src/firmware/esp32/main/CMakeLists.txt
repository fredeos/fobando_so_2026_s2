file(GLOB COMMON_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/**/*.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/*.c"
)

file(GLOB THIRD_PARTY
    "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party"
)

idf_component_register(
    SRCS 
        # Main prorgram
        "main.c"
        # Sources
        ${COMMON_SOURCES}
        "gpio/gpio.c"
        "serial/serial.c"
        "timing/timing.c"
        # Third party
        "${THIRD_PARTY}/coreJSON/core_json.c"
        

    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_LIST_DIR}/../../../../include"
        ${THIRD_PARTY}
    
    REQUIRES 
        esp_driver_uart
        esp_driver_gpio
        esp_timer
)