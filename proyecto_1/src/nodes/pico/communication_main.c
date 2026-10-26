
/*

#include <stdio.h>
#include <inttypes.h>

#include "pico/stdlib.h"
#include "calibrator.h"

// -----------------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------------

#define TEST_TICK_US 10000u
#define TEST_RESERVE_PERCENT 20u
#define TEST_COUNT 5u

// -----------------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------------

int main(void) {
    stdio_init_all();

    printf("\n");
    printf("----------------------------------------\n");
    printf(" Calibration Test - Raspberry Pi Pico\n");
    printf("----------------------------------------\n");

    printf("Tick duration: %" PRIu32 " us\n", TEST_TICK_US);
    printf("System reserve: %" PRIu32 " %%\n", TEST_RESERVE_PERCENT);

    for (uint32_t i = 0; i < TEST_COUNT; i++) {
        CalibrationResult result;

        bool success = calibrator_run(
            TEST_TICK_US,
            TEST_RESERVE_PERCENT,
            &result
        );

        if (success) {
            printf(
                "Test %" PRIu32 ": %" PRIu32 " UT/tick\n",
                i + 1,
                result.units_per_tick
            );
        } else {
            printf("Test %" PRIu32 ": Calibration failed\n", i + 1);
        }
    }

    printf("----------------------------------------\n");
    printf("Calibration test finished.\n");

    while (true) {
        tight_loop_contents();
    }

    return 0;
}


#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#define BUFFER_SIZE 128

int main(void)
{
    stdio_init_all();

    // Wait for the computer to connect to the USB serial port.
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    char line[BUFFER_SIZE];
    size_t used = 0;

    while (1) {
        int ch = getchar_timeout_us(0);

        if (ch == PICO_ERROR_TIMEOUT) {
            sleep_ms(1);
            continue;
        }

        if (ch == '\r')
            continue;

        if (ch == '\n') {
            line[used] = '\0';

            if (strcmp(line, "PING") == 0) {
                printf("PONG from Pico\n");
                fflush(stdout);
            }

            used = 0;
        } else if (used < sizeof(line) - 1) {
            line[used++] = (char)ch;
        } else {
            // Discard an oversized line.
            used = 0;
        }
    }
}
*/



#include <string.h>

#include "pico/stdlib.h"

#include "serial/pico.h"
#include "serial/serial.h"
#include "common/protocol.h"
#include "gpio/gpio.h"

#define LED0 26
#define LED1 25
#define LED2 33
#define LED3 32

#define MESSAGE_LEN 80

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

    if (!setup_fail) setup_fail = (conf_errno != SERIAL_OK);
    // << Setup the protocol interface >>
    // [TX:output]
    protocol_frame_t tx_frame;
    protocol_tx_t tx;
    serial_packet_t tx_packet;
    protocol_errno_t tx_setup_errno = protocol_tx_init(&tx, &tx_packet);

    // [RX:input]
    protocol_frame_t rx_frame;
    protocol_rx_t rx;
    serial_packet_t rx_packet;
    protocol_errno_t rx_setup_errno = protocol_rx_init(&rx, &rx_packet);

    if (!setup_fail) setup_fail = (tx_setup_errno != PROTOCOL_OK || rx_setup_errno != PROTOCOL_OK);
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
    const char *pong = "PONG from Pico";
    const char *default_msg = "Pico didn't understand message";
    const char *polo = "POLO from Pico";
    
    char message [MESSAGE_LEN];
    char response[MESSAGE_LEN];
    // << Procedure loop >>
    int state = 0;

    if (setup_fail) board_gpio_write(LED0, GPIO_LOW);
    else board_gpio_write(LED1, GPIO_LOW);
    while (1) {
        if (setup_fail) continue;
        // [RX state]
        if (state == 0){
            board_gpio_write(LED2, GPIO_HIGH);
            board_gpio_write(LED3, GPIO_LOW);
            // [Poll RX state machine]
            protocol_errno_t rx_errno = protocol_rx_poll(
                &serial,
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
                    else if (strcmp(message, "MARCO") == 0 && rx_frame.relevance > 0)
                        msg = polo;
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
                &serial,
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