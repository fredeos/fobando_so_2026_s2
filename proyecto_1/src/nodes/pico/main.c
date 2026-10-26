
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
#include <stdio.h>

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
#include <stdio.h>

#include "pico/stdlib.h"

#include "serial/pico.h"
#include "serial/serial.h"
#include "common/protocol.h"
#include "common/message_parser.h"
#include "common/messages.h"
#include "gpio/gpio.h"

#define LED0 26
#define LED1 25
#define LED2 33
#define LED3 32

#define NODE_ID 2u
#define MESSAGE_LEN (PROTOCOL_PAYLOAD_BYTES + 1u)

static bool prepare_response(
    const protocol_frame_t *received,
    char *response,
    size_t response_capacity,
    uint32_t *response_length
)
{
    char request[MESSAGE_LEN];
    size_t request_length = received->length;

    if (request_length > PROTOCOL_PAYLOAD_BYTES)
        return false;
    memcpy(request, received->payload, request_length);
    request[request_length] = '\0';
    if (request_length > 0u && request[request_length - 1u] == '\0')
        --request_length;

    Message incoming;
    if (message_parse(request, request_length, &incoming) == MESSAGE_PARSE_OK) {
        if (incoming.type != MSG_CONFIG || incoming.node_id != NODE_ID)
            return false;

        Message reply = {
            .type = MSG_ACK,
            .node_id = NODE_ID,
            .request_id = incoming.request_id,
            .data.reply = {.type = MSG_CONFIG}
        };
        size_t json_length = 0u;
        if (message_serialize(&reply, response, response_capacity,
                              &json_length) != MESSAGE_SERIALIZE_OK) {
            return false;
        }
        *response_length = (uint32_t)json_length;
        return true;
    }

    const char *legacy = "Pico didn't understand message";
    if (request_length == 4u && memcmp(request, "PING", 4u) == 0)
        legacy = "PONG from Pico";
    else if (request_length == 5u && memcmp(request, "MARCO", 5u) == 0 &&
             received->relevance > 0u)
        legacy = "POLO from Pico";

    size_t legacy_length = strlen(legacy) + 1u;
    if (legacy_length > response_capacity)
        return false;
    memcpy(response, legacy, legacy_length);
    *response_length = (uint32_t)legacy_length;
    return true;
}

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
                uint32_t response_length = 0u;
                if (rx.valid){
                    protocol_errno_t rx_frame_errno = protocol_set_buffer_to_frame(
                        &rx_frame,
                        rx.buffer,
                        sizeof(rx.buffer)
                    );
                    if (rx_frame_errno == PROTOCOL_OK)
                        (void)prepare_response(&rx_frame, response,
                            sizeof(response), &response_length);
                }
                if (response_length == 0u) {
                    const char *fallback = "Pico didn't understand message";
                    response_length = (uint32_t)strlen(fallback) + 1u;
                    memcpy(response, fallback, response_length);
                }

                tx_frame.relevance = 0;
                tx_frame.length = response_length;
                memcpy(tx_frame.payload, response, response_length);

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