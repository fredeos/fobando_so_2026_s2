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

add_executable(pico
    main.c
)

target_link_libraries(pico
    pico_stdlib
)

# Use USB for serial I/O, not the UART pins.
pico_enable_stdio_usb(pico 1)
pico_enable_stdio_uart(pico 0)

pico_add_extra_outputs(pico)