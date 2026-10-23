#include <string.h>

#include "pico/stdlib.h"

#include "serial/pico.h"
#include "serial/serial.h"
#include "gpio/gpio.h"

#define LED0 25
#define LED1 26

#define BUFFER_SIZE 128
#define MESSAGE_LEN 64

void main(void){
    bool setup_fail = false;

    // << Setup Pico serial interface >>
    pico_serial_t serial;
    pico_serial_configs_t serial_conf = {
        .timeout_us = 100000
    };

    serial_errno_t conf_errno = serial_configure(
        &serial,
        &serial_conf
    );

    setup_fail = (conf_errno != SERIAL_OK);
    // << Setup input packet >>
    uint8_t in[BUFFER_SIZE];
    serial_packet_t input = {
        .mode = SERIAL_IN,
        .buffer = in,
        .length = sizeof(in),
        .complete = false,
        .processed = 0
    };

    // << Setup output packet >>
    uint8_t out[BUFFER_SIZE];
    serial_packet_t output = {
        .mode = SERIAL_OUT,
        .buffer = out,
        .length = sizeof(out),
        .complete = false,
        .processed = 0
    };

    // << Setup GPIO >>
    board_gpio_init(LED0, GPIO_OUTPUT);
    board_gpio_init(LED1, GPIO_OUTPUT);

    board_gpio_write(LED0, GPIO_LOW);
    board_gpio_write(LED1, GPIO_LOW);
    // << Setup basic messages >>
    const char *pong ="PONG from Pico";
    const char *heartbeat = "HEARTBEAT from Pico";
    const char *default_msg = "Pico didn't understand message";

    char message[MESSAGE_LEN];
    char response[MESSAGE_LEN];
    // << Procedure loop >>
    int send_events = 2;
    int recv_type = 0;

    if (setup_fail) board_gpio_write(LED0, GPIO_HIGH);
    else board_gpio_write(LED1, GPIO_HIGH);
    while (1) {
        if (setup_fail) continue;
        // [Read from serial]
        size_t rd = 0;
        serial_errno_t rd_errno = serial_read(
            &serial,
            &input,
            &rd
        );

        if (rd_errno == SERIAL_FAILURE)
            serial_restart_packet(&input);

        if (input.complete) {
            // Convert bytes
            char *buffer = (char *)in;
            for (unsigned int i = 0; i < sizeof(response); i++) {
                char ch = buffer[i];
                if (ch == '\n')
                    response[i] = '\0';
                else
                    response[i] = ch;
            }
            // Restart packet for next message
            serial_restart_packet(&input);
            // Check response
            if (strcmp(response, "PING") == 0) {
                recv_type = 1;
                if (send_events == 2)send_events = 0;
            } else if (strcmp(response, "CHECK") == 0) {
                recv_type = 2;
                if (send_events == 2) send_events = 0;
            }
        }

        // [Send with serial]
        if (send_events == 0) {
            // Select message
            const char *msg = default_msg;
            if (recv_type == 1) msg = pong;
            if (recv_type == 2) msg = heartbeat;
            size_t len = strlen(msg);

            // Build message
            for (size_t i = 0; i < sizeof(message); i++) {
                if (i < len)
                    message[i] = msg[i];
                else if (i == len)
                    message[i] = '\n';
                else
                    message[i] = '\0';
            }

            // Set output buffer
            const uint8_t *buffer = (uint8_t *)message;
            for (size_t i = 0; i < sizeof(out); i++) {
                if (i < sizeof(message))
                    out[i] = buffer[i];
                else
                    out[i] = 0x00u;
            }

            // Move to send
            send_events = 1;
        }

        if (send_events == 1) { 
            // Send message
            size_t wd = 0;
            serial_errno_t wd_errno = serial_write(
                &serial,
                &output,
                &wd
            );

            if (wd_errno == SERIAL_FAILURE)
                serial_restart_packet(&output);

            if (output.complete) {
                serial_restart_packet(&output);
                // Move to wait
                send_events = 2;
            }
        }
    }
}