#ifndef SERIAL_ESP32_H
#define SERIAL_ESP32_H

#include "driver/uart.h"

/*
 * Structure for ESP32 serial interface using UART
 * 
*/
typedef struct {
    uart_port_t port_num;
    uint32_t ticks;
} esp32_uart_t;


/*
 * Structure for ESP32 serial interface configurations for UART
 *
*/
typedef struct {
    uart_port_t port_num;
    int baudrate;
    int rx_buffer_size;
    int tx_buffer_size;
    int tick_ms;
} esp32_uart_configs_t;

#endif // SERIAL_ESP32_H