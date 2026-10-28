#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"

#include "serial/esp32.h"
#include "serial/serial.h"
#include "gpio/gpio.h"

#define LED0 26
#define LED1 25

#define UART_PORT   UART_NUM_0
#define BAUD_RATE   115200
#define BUFFER_SIZE 128
#define MESSAGE_LEN 64

void app_main(void){
    bool setup_fail = false;
    // << Setup ESP32 serial interface >>
    esp32_uart_t uart;
    esp32_uart_configs_t uart_conf = {
        .port_num = UART_PORT,
        .baudrate = SERIAL_BAUDRATE,
        .rx_buffer_size = 1024,
        .tx_buffer_size = 0,
        .tick_ms = 100
    };

    serial_errno_t conf_errno = serial_configure(
        &uart, &uart_conf
    );
    setup_fail = (conf_errno != SERIAL_OK);

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
    // << Setup GPIO >>
    board_gpio_init(LED0, GPIO_OUTPUT);
    board_gpio_init(LED1, GPIO_OUTPUT);
    
    board_gpio_write(LED0, GPIO_LOW);
    board_gpio_write(LED1, GPIO_LOW);
    // << Setup basic messages >>
    const char *pong = "PONG from ESP32";
    const char *heartbeat = "HEARTBEAT from ESP32";
    const char *default_msg = "ESP32 didn't understand message";
    
    char message [MESSAGE_LEN];
    char response[MESSAGE_LEN];
    // << Procedure loop >>
    int send_events = 2;
    int recv_type = 0;

    if (setup_fail) board_gpio_write(LED0, GPIO_HIGH);
    else board_gpio_write(LED1, GPIO_HIGH);
    while (1) {
        if (setup_fail) continue;
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