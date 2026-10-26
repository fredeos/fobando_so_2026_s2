file(GLOB COMMON_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/*.c"
)
# ESP-IDF evalúa componentes en modo script; registra el serializer explícitamente.
list(FILTER COMMON_SOURCES EXCLUDE REGEX "/message_serializer\\.c$")
list(FILTER COMMON_SOURCES EXCLUDE REGEX "/task_store\\.c$")

idf_component_register(
    SRCS 
        "main.c"
        ${COMMON_SOURCES}
        "${CMAKE_CURRENT_LIST_DIR}/../../../common/message_serializer.c"
        "${CMAKE_CURRENT_LIST_DIR}/../../../common/task_store.c"
        "${CMAKE_CURRENT_LIST_DIR}/../src/calibrator.c"
        "${CMAKE_CURRENT_LIST_DIR}/../../scheduler/task_conversion.c"
        "${CMAKE_CURRENT_LIST_DIR}/../../scheduler/scheduler_runtime.c"
        "${CMAKE_CURRENT_LIST_DIR}/../../scheduler/scheduler.c"
        "${CMAKE_CURRENT_LIST_DIR}/../../scheduler/rms.c"
        "${CMAKE_CURRENT_LIST_DIR}/../../scheduler/edf.c"
        "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/coreJSON/core_json.c"
        "gpio/gpio.c"
        "serial/serial.c"

    INCLUDE_DIRS 
        "."
        "${CMAKE_CURRENT_LIST_DIR}/../src"
        "${CMAKE_CURRENT_LIST_DIR}/../../scheduler"
        "${CMAKE_CURRENT_LIST_DIR}/../../../../include"
        "${CMAKE_CURRENT_LIST_DIR}/../../../../third_party/coreJSON"
    
    REQUIRES 
        esp_driver_uart
        esp_driver_gpio
        esp_timer
)
