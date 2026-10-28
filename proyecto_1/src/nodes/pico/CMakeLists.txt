cmake_minimum_required(VERSION 3.13)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if(EXISTS ${picoVscode})
    include(${picoVscode})
endif()
set(PICO_BOARD pico CACHE STRING "Board type")
include(pico_sdk_import.cmake)
project(pico C CXX ASM)
pico_sdk_init()

option(PICO_LOCAL_TEST "Prueba autonoma RMS/EDF sin comunicacion" OFF)
set(LOCAL_ALGORITHM "RMS" CACHE STRING "RMS o EDF para la prueba local")
set_property(CACHE LOCAL_ALGORITHM PROPERTY STRINGS RMS EDF)
set(LOCAL_TICK_MS "1000" CACHE STRING "Duracion del tick local en ms")
set(LOCAL_MISS_LED_PIN "25" CACHE STRING "GPIO de plazo perdido; -1 desactivado")

if(PICO_LOCAL_TEST)
    if(NOT LOCAL_ALGORITHM MATCHES "^(RMS|EDF)$")
        message(FATAL_ERROR "LOCAL_ALGORITHM debe ser RMS o EDF")
    endif()
    if(NOT LOCAL_TICK_MS MATCHES "^[1-9][0-9]*$" OR LOCAL_TICK_MS GREATER 1000)
        message(FATAL_ERROR "LOCAL_TICK_MS debe estar entre 1 y 1000")
    endif()
    if(NOT LOCAL_MISS_LED_PIN MATCHES "^(-1|[0-9]+)$" OR
       LOCAL_MISS_LED_PIN GREATER 28 OR
       LOCAL_MISS_LED_PIN MATCHES "^(10|11|12|13|18)$")
        message(FATAL_ERROR "LOCAL_MISS_LED_PIN debe ser -1 o un GPIO libre de 0 a 28")
    endif()
    add_executable(pico local_test_main.c gpio/gpio.c src/calibrator.c
        ../../common/ut.c
        ../scheduler/scheduler.c ../scheduler/rms.c ../scheduler/edf.c)
    target_compile_definitions(pico PRIVATE
        LOCAL_MISS_LED_PIN=${LOCAL_MISS_LED_PIN}
        LOCAL_TICK_MS=${LOCAL_TICK_MS}
        LOCAL_USE_EDF=$<STREQUAL:${LOCAL_ALGORITHM},EDF>
        PICO_STDIO_USB_STDOUT_TIMEOUT_US=0)
else()
    file(GLOB COMMON_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_LIST_DIR}/../../common/*.c")
    add_executable(pico main.c ${COMMON_SOURCES}
        src/calibrator.c
        ../scheduler/task_conversion.c
        ../scheduler/scheduler_runtime.c
        ../scheduler/scheduler.c ../scheduler/rms.c ../scheduler/edf.c
        ../../../third_party/coreJSON/core_json.c
        gpio/gpio.c serial/serial.c)
endif()
target_include_directories(pico PRIVATE
    src ../scheduler ../../../include ../../../third_party/coreJSON)
target_compile_options(pico PRIVATE -Wall -Wextra)
target_link_libraries(pico pico_stdlib)
pico_enable_stdio_usb(pico 1)
pico_enable_stdio_uart(pico 0)
pico_add_extra_outputs(pico)

# Compatibilidad de las macros de formato de 64 bits en Newlib/GCC ARM.
# No modifica el serializer compartido ni afecta las fuentes ensamblador.
target_compile_options(pico PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-include> $<$<COMPILE_LANGUAGE:C>:sys/types.h>)
