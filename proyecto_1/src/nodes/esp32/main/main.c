#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"

#include "serial/esp32.h"
#include "serial/serial.h"
#include "gpio/gpio.h"

#define LED0 26
#define UART_PORT   UART_NUM_0
#define BAUD_RATE   115200
#define BUFFER_SIZE 128
#define MESSAGE_LEN 64

void app_main(void)
{
    // UART0 is commonly connected to the DevKit USB-UART bridge.
    // GPIO pins are left at their existing UART assignments.
    const uart_config_t config = {
        .baud_rate = BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT
    };

    ESP_ERROR_CHECK(
        uart_driver_install(UART_PORT, 1024, 0, 0, NULL, 0)
    );

    ESP_ERROR_CHECK(
        uart_param_config(UART_PORT, &config)
    );

    uart_flush_input(UART_PORT);
    // Keep application logs off the serial protocol channel.
    esp_log_level_set("*", ESP_LOG_NONE);

    // << Setup ESP32 serial interface >>
    esp32_uart_t uart = {
        .port_num = UART_PORT,
        .ticks = pdMS_TO_TICKS(100)
    };

    uint8_t in[BUFFER_SIZE];
    serial_packet_t input = {
        .mode = SERIAL_IN,
        .buffer = in,
        .length = sizeof(in),
        .complete =  false,
        .processed = 0
    };

    uint8_t out[BUFFER_SIZE];
    serial_packet_t output = {
        .mode = SERIAL_OUT,
        .buffer = out,
        .length = sizeof(out),
        .complete =  false,
        .processed = 0
    };
    // << Setup basic messages >>
    const char *pong = "PONG from ESP32";
    const char *heartbeat = "HEARTBEAT from ESP32";
    const char *default_msg = "ESP32 didn't understand message";
    
    char message [MESSAGE_LEN];
    char response[MESSAGE_LEN];
    // << Procedure loop >>
    int send_events = 2;
    int recv_type = 0;
    while (1) {
        // [Read from serial]
        // read bytes
        size_t rd = 0;
        serial_errno_t rd_errno = serial_read(
            &uart, &input, &rd
        );

        if (rd_errno == SERIAL_FAILURE)
            serial_restart_packet(&input);
        
        if (input.complete){
            // Convert bytes
            char* buffer = (char*) in;
            for (unsigned int i = 0; i < sizeof(response); i++){
                char ch = buffer[i];
                if (ch == '\n')
                    response[i] = '\0';
                else
                    response[i] = ch;
            }
            // Restart packet for next message
            serial_restart_packet(&input);
            // Check response
            if (strcmp(response, "PING") == 0){
                recv_type = 1;
                if (send_events == 2) send_events = 0;
            } else if (strcmp(response, "CHECK") == 0){
                recv_type = 2;
                if (send_events == 2) send_events = 0;
            }
        }
        // [Send with serial]
        if (send_events == 0){ // setup message
            // select message
            const char *msg = default_msg;
            if (recv_type == 1) msg = pong;
            if (recv_type == 2) msg = heartbeat;
            size_t len = strlen(msg);
            // build message
            for (size_t i = 0; i < sizeof(message); i++){
                if (i < len)
                    message[i] = msg[i];
                else if (i == len)
                    message[i] = '\n';
                else
                    message[i] = '\0';
            }
            // set to buffer
            const uint8_t* buffer = (uint8_t*) message;
            for (size_t i = 0; i < sizeof(out); i++){
                if (i < sizeof(message))
                    out[i] = buffer[i];
                else
                    out[i] = 0x00u;
            }
            // move to send
            send_events = 1;
        }
        if (send_events == 1){ // send message
            // send message
            size_t wd = 0;
            serial_errno_t wd_errno = serial_write(
                &uart, &output, &wd
            );

            if (wd_errno == SERIAL_FAILURE)
                serial_restart_packet(&output);
            
            if (output.complete){
                serial_restart_packet(&output);
                // Move to wait
                send_events = 2;
            }
        }
    }
}