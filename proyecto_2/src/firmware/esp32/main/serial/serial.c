// > ESP32 serial.h implementation file
#include "serial/serial.h"
#include "esp32.h"


// ======================================================================================
/*
 * ESP32 implementation of serial configuration
 *
*/
serial_errno_t serial_configure(void* _obj, void* _config){
    // << Exceptions >>
    if (_obj == NULL || _config == NULL)
        return SERIAL_INVALID_ARG;
    // << Get the UART port >>
    esp32_uart_t* uart = (esp32_uart_t*) _obj;
    esp32_uart_configs_t* conf = (esp32_uart_configs_t*) _config;
    // << Configure the port >>
    const uart_config_t config = {
        .baud_rate = conf->baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    esp_err_t driver_errno = uart_driver_install(
        conf->port_num, 
        conf->rx_buffer_size, 
        conf->tx_buffer_size, 
        0, NULL, 0
    );
    if (driver_errno == ESP_FAIL)
        return SERIAL_FAILURE;

    esp_err_t conf_errno = uart_param_config(conf->port_num, &config);
    if (conf_errno == ESP_FAIL)
        return SERIAL_FAILURE;

    uart_flush_input(conf->port_num);
    // << Set the UART port >>
    uart->port_num = conf->port_num;
    uart->ticks = pdMS_TO_TICKS(conf->tick_ms);
    // << Return >>
    return SERIAL_OK;
}

// ======================================================================================
/*
 * ESP32 implementation of serial data write
 *
*/
serial_errno_t serial_write(
    void *_obj, 
    serial_packet_t *_packet, 
    size_t* wd_bytes
){
    // << Exceptions >>
    if (_obj == NULL || _packet == NULL || wd_bytes == NULL)
        return SERIAL_INVALID_ARG;
    // << Cast object >>
    esp32_uart_t* uart = (esp32_uart_t*) _obj;
    // << Configure packet >>
    if (_packet->mode != SERIAL_OUT)
        return SERIAL_INVALID_MODE; // Error: incorrect packet mode
    *wd_bytes = 0;
    if (_packet->complete)
        return SERIAL_OK;
    // << Attempt to send packet >>
    uint8_t *ptr = _packet->buffer + _packet->processed;
    size_t  rem  = _packet->length - _packet->processed;
    int n = uart_write_bytes(
        uart->port_num, 
        ptr, 
        rem
    );
    if (n < 0)
        return SERIAL_FAILURE;   // Error: failed to send data
    // << Detect if full packet sent >>
    size_t wd = (size_t) n;
    _packet->processed += wd;
    _packet->complete = (_packet->processed == _packet->length);
    // << Return >>
    *wd_bytes = wd;
    return (n == 0) ? SERIAL_NO_DATA : SERIAL_OK;
}

// ======================================================================================
/*
 * ESP32 implementation of serial data read
 *
*/
serial_errno_t serial_read(
    void *_obj, 
    serial_packet_t *_packet, 
    size_t* rd_bytes
){  
    // << Exceptions >>
    if (_obj == NULL || _packet == NULL || rd_bytes == NULL)
        return SERIAL_INVALID_ARG;
    // << Cast object >>
    esp32_uart_t* uart = (esp32_uart_t*) _obj;
    // << Configure packet >>
    if (_packet->mode != SERIAL_IN)
        return SERIAL_INVALID_MODE; // Error: incorrect packet mode
    *rd_bytes = 0;
    if (_packet->complete)
        return SERIAL_OK;
    // << Attempt to receive packet >>
    uint8_t *ptr = _packet->buffer + _packet->processed;
    size_t  rem  = _packet->length - _packet->processed;
    int n = uart_read_bytes(
        uart->port_num, 
        ptr, 
        rem, 
        uart->ticks
    );
    if (n < 0)
        return SERIAL_FAILURE;   // Error: failed to read data
    // << Detect if full packet received >>
    size_t rd = (size_t) n;
    _packet->processed += rd;
    _packet->complete = (_packet->processed == _packet->length);
    // << Return >>
    *rd_bytes = rd;
    return (n == 0) ? SERIAL_NO_DATA : SERIAL_OK;
}

// =====================================================================================
/*
 * ESP32 implementation of serial data restart
 *
 * 
*/
serial_errno_t serial_restart_packet(
    serial_packet_t *_packet
){
    if (_packet == NULL)
        return SERIAL_INVALID_ARG;
    _packet->processed = 0;
    _packet->complete  = false;
    return SERIAL_OK;
}