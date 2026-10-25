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

file(GLOB COMMON_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/../../common/*.c"
)

add_executable(pico
    main.c

    ${COMMON_SOURCES}
    ${CMAKE_CURRENT_LIST_DIR}/../../../third_party/coreJSON/core_json.c

    gpio/gpio.c
    serial/serial.c
)

target_include_directories(pico PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../../include
    ${CMAKE_CURRENT_LIST_DIR}/../../../third_party/coreJSON
)

target_link_libraries(pico
    pico_stdlib
)

# -----------------------------------------------------------------------------------
# USB serial configuration
# -----------------------------------------------------------------------------------

pico_enable_stdio_usb(pico 1)
pico_enable_stdio_uart(pico 0)

<<<<<<< HEAD
# -----------------------------------------------------------------------------------
# Generate output binaries
# -----------------------------------------------------------------------------------

pico_add_extra_outputs(pico)
=======
pico_add_extra_outputs(pico)
>>>>>>> origin/feature/messages
