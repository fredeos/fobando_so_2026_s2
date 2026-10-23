#ifndef ESP32_SERIAL_H
#define ESP32_SERIAL_H

#include "driver/uart.h"

/*
 * Structure for ESP32 serial interface using UART
 * 
*/
typedef struct {
    uart_port_t port_num;
    uint32_t ticks;
} esp32_uart_t;

#endif // ESP32_SERIAL_H