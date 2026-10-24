#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"

#include "common/protocol.h"
#include "serial/esp32.h"
#include "serial/serial.h"
#include "gpio/gpio.h"

#define LED0 26
#define LED1 25
#define LED2 33
#define LED3 32

#define UART_PORT   UART_NUM_0

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
    if (!setup_fail) setup_fail = (conf_errno != SERIAL_OK);

    // << Setup the protocol interface >>
    // [TX:output]
    protocol_frame_t tx_frame;
    protocol_tx_t tx = {
        .complete = false,
        .valid = false,
        .ack_state = PROTOCOL_ACK_UNKNOWN,
        .rd_busy = false,
        .wd_busy = false,
        .state = PROTOCOL_TX_INIT,
        .offset = 0,
        .count = 0
    };
    serial_packet_t tx_packet = {
        .mode = SERIAL_OUT,
        .buffer = NULL,
        .length = 0,
        .complete  = false,
        .processed = 0
    };
    // [RX:input]
    protocol_frame_t rx_frame;
    protocol_rx_t rx = {
        .complete = false,
        .valid = false,
        .ack_state = PROTOCOL_ACK_UNKNOWN,
        .rd_busy = false,
        .wd_busy = false,
        .state = PROTOCOL_RX_INIT,
        .offset = 0,
        .count = 0
    };
    serial_packet_t rx_packet = {
        .mode = SERIAL_IN,
        .buffer = NULL,
        .length = 0,
        .complete  = false,
        .processed = 0
    };

    // << Setup GPIO >>
    board_gpio_init(LED0, GPIO_OUTPUT);
    board_gpio_init(LED1, GPIO_OUTPUT);
    board_gpio_init(LED2, GPIO_OUTPUT);
    board_gpio_init(LED3, GPIO_OUTPUT);
    
    board_gpio_write(LED0, GPIO_LOW);
    board_gpio_write(LED1, GPIO_LOW);
    board_gpio_write(LED2, GPIO_LOW);
    board_gpio_write(LED3, GPIO_LOW);
    // << Setup basic messages >>
    const char *pong = "PONG from ESP32";
    const char *default_msg = "ESP32 didn't understand message";
    
    char message [MESSAGE_LEN];
    char response[MESSAGE_LEN];
    // << Procedure loop >>
    int state = 0;

    if (setup_fail) board_gpio_write(LED0, GPIO_HIGH);
    else board_gpio_write(LED1, GPIO_HIGH);
    while (1) {
        if (setup_fail) continue;
        // [RX state]
        if (state == 0){
            board_gpio_write(LED2, GPIO_HIGH);
            board_gpio_write(LED3, GPIO_LOW);
            // [Poll RX state machine]
            protocol_errno_t rx_errno = protocol_rx_poll(
                &uart,
                &rx,
                &rx_packet
            );
            // [Check RX]
            if (rx.complete){
                // [Check message]
                const char *msg = default_msg;
                if (rx.valid){
                    // (Read buffer to frame)
                    protocol_errno_t rx_frame_errno = protocol_set_buffer_to_frame(
                        &rx_frame,
                        rx.buffer,
                        sizeof(rx.buffer)
                    );
                    // (Load to message)
                    memcpy(
                        message,
                        rx_frame.payload,
                        rx_frame.length
                    );
                    // (Check message)
                    if (strcmp(message, "PING") == 0 && rx_frame.relevance == 0)
                        msg = pong;
                }
                // [Setup response]
                uint32_t size = strlen(msg) + 1; // copy response until '\0'

                tx_frame.relevance = 0;
                tx_frame.length = size;
                memcpy(tx_frame.payload, msg, size);

                protocol_errno_t tx_frame_errno = protocol_set_frame_to_buffer(
                    &tx_frame,
                    tx.buffer,
                    sizeof(tx.buffer)
                );
                // [Move to TX]
                state = 1;
            }
        }
        // [TX state]
        if (state == 1){
            board_gpio_write(LED2, GPIO_LOW);
            board_gpio_write(LED3, GPIO_HIGH);
            // [Poll TX state machine]
            protocol_errno_t tx_errno = protocol_tx_poll(
                &uart,
                &tx,
                &tx_packet
            );
            // [Check TX]
            if (tx.complete && tx.valid){
                // [Move to RX]
                state = 0;
            }
        }
    }
}