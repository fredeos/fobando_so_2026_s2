if(ESP_LOCAL_TEST)
    set(NODE_SOURCES "main.c" "gpio/gpio.c" "../src/calibrator.c"
        "../../../common/ut.c"
        "../../scheduler/scheduler.c"
        "../../scheduler/rms.c"
        "../../scheduler/edf.c")
else()
    file(GLOB COMMON_SOURCES "${CMAKE_CURRENT_LIST_DIR}/../../../common/*.c")
    set(NODE_SOURCES "communication_main.c" ${COMMON_SOURCES}
        "../../../../third_party/coreJSON/core_json.c"
        "gpio/gpio.c" "serial/serial.c")
endif()

# La expansión temprana de requisitos de ESP-IDF no recibe opciones del proyecto.
# Declarar las dependencias aquí para ambos modos, sin condicionar los requisitos.
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
