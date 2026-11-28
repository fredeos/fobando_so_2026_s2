option(SCREEN_COMM_DEMO "Build the screen communication LED demo instead of main.c" OFF)

set(COMMON_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/conversion.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/protocol/protocol_misc.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/protocol/protocol_rx.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/protocol/protocol_tx.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/json/json_misc.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/json/json_parse.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/json/json_parse_utils.c"
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/json/json_serialize.c"
)

if(SCREEN_COMM_DEMO)
    set(ESP32_MAIN_SOURCE "main_demo_screen_comm.c")
else()
    set(ESP32_MAIN_SOURCE "main.c")
endif()

file(GLOB THIRD_PARTY
    "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party"
)

idf_component_register(
    SRCS 
        # Main prorgram
        "${ESP32_MAIN_SOURCE}"
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
