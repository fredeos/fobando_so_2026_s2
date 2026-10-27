# main.c es el programa de comunicación; local_test_main.c es la prueba opcional.
# Se compila un solo app_main() por firmware.
if(ESP_LOCAL_TEST)
    set(NODE_SOURCES
        "local_test_main.c" "gpio/gpio.c" "../src/calibrator.c"
        "../../../common/ut.c"
        "../../scheduler/scheduler.c"
        "../../scheduler/rms.c"
        "../../scheduler/edf.c")
else()
    # No usar CONFIGURE_DEPENDS en el registro temprano de ESP-IDF.
    file(GLOB COMMON_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../../../common/*.c")
    list(FILTER COMMON_SOURCES EXCLUDE REGEX "/message_serializer\\.c$")
    list(FILTER COMMON_SOURCES EXCLUDE REGEX "/task_store\\.c$")
    set(NODE_SOURCES
        "main.c"
        ${COMMON_SOURCES}
        "../../../common/message_serializer.c"
        "../../../common/task_store.c"
        "../src/calibrator.c"
        "../../scheduler/task_conversion.c"
        "../../scheduler/scheduler_runtime.c"
        "../../scheduler/scheduler.c"
        "../../scheduler/rms.c"
        "../../scheduler/edf.c"
        "../../../../third_party/coreJSON/core_json.c"
        "gpio/gpio.c" "serial/serial.c")
endif()

# Los requisitos se declaran incondicionalmente: ESP-IDF los evalúa también
# en una fase temprana que no recibe las opciones particulares del proyecto.
idf_component_register(
    SRCS ${NODE_SOURCES}
    INCLUDE_DIRS "." "../src" "../../scheduler" "../../../../include"
                 "../../../../third_party/coreJSON"
    REQUIRES esp_driver_uart esp_driver_gpio esp_driver_gptimer esp_timer freertos log
)
if(ESP_LOCAL_TEST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        LOCAL_TICK_MS=${LOCAL_TICK_MS}
        LOCAL_USE_EDF=$<STREQUAL:${LOCAL_ALGORITHM},EDF>
    )
endif()
