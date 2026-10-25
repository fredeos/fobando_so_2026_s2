#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "serial/serial.h"
#include "serial/posix.h"
#include "common/protocol.h"
#include "host/report_session.h"

#define MESSAGE_LEN 64u

int main(int argc, char *argv[])
{
    /* Modo aislado para verificar reportes y cierre sin hardware ni protocolo */
    if (argc >= 2 && strcmp(argv[1], "--report-demo") == 0) {
        if (argc > 3) {
            fprintf(stderr, "Usage: %s --report-demo [output_directory]\n", argv[0]);
            return 1;
        }
        char directory[128];
        if (argc == 2 && mkdir("output", 0775) != 0 && errno != EEXIST) {
            perror("output");
            return 1;
        }
        snprintf(directory, sizeof(directory), "output/report_%ld_%ld", (long)time(NULL), (long)getpid());
        return report_demo_run(argc == 3 ? argv[2] : directory);
    }

    if (argc != 2) {
        fprintf(stderr, "Usage: %s /dev/ttyUSB0 | --report-demo [output_directory]\n", argv[0]);
        return 1;
    }
    // << Configure serial interface >>
    // [Configure]
    bool setup_fail = false;
    const char* dev = argv[1];

    int fd;
    posix_serial_config_t ser_conf = {
        .device = dev,
        .baudrate = SERIAL_BAUDRATE
    };

    serial_errno_t conf_errno = serial_configure(
        &fd,
        &ser_conf
    );
    if (!setup_fail) setup_fail = (conf_errno == SERIAL_FAILURE);

    // << Setup protocol interface
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
    // << Message handling >>
    char response[MESSAGE_LEN]; // received
    
    char message [MESSAGE_LEN]; // sent
    const char *default_msg  = "PING";
    uint32_t size = strlen(default_msg) + 1;
    // << Configure TX frame >>
    tx_frame.relevance = 0;
    tx_frame.length = size;
    memcpy(tx_frame.payload, default_msg, size); // copy whole message until '\0'
    
    protocol_errno_t tx_frame_errno = protocol_set_frame_to_buffer(
        &tx_frame,
        tx.buffer,
        sizeof(tx.buffer)
    );
    if (!setup_fail) setup_fail = (tx_frame_errno == PROTOCOL_ERROR);

    // << Procedure loop >>
    int recv_events = 0;
    int ack_wait = 0;
    int state = 0;

    if (setup_fail) printf("Something went wrong when setting up serial connection!\n");
    else {
        printf("Starting procedure...\n");
        printf("TX initial state: %d\n", tx.state);
        printf("RX initial state: %d\n", rx.state);
    }
    while (!setup_fail){
        if (recv_events == 5) break;
        // [TX state]
        if (state == 0){
            // [Poll TX state machine]
            protocol_errno_t tx_errno = protocol_tx_poll(
                &fd,
                &tx,
                &tx_packet
            );
            // [Check TX]
            if (tx_errno != PROTOCOL_OK) printf("protocol_tx_poll(): Something went wrong!\n");
            if (tx.state == PROTOCOL_TX_ACK && ack_wait == 0) {
                printf("Waiting for device response...\n");
                ack_wait++;
            }
            if (tx.complete){
                if (tx.ack_state == PROTOCOL_ACK) printf("Device understood response!\n");
                else if (tx.ack_state == PROTOCOL_NACK) printf("Device didn't understood response!\n");
                else if (tx.ack_state == PROTOCOL_ACK_UNKNOWN) printf("Device didn't confirm response!\n");
                else if (tx.ack_state == PROTOCOL_ACK_TIMEOUT) printf("Device disconnected!\n");
                // [Check if message sent]
                if (tx.valid){
                    memcpy(message, tx_frame.payload, tx_frame.length);
                    printf("Sent: %s\n", message);
                    state = 1;
                }
                ack_wait = 0;
            }
        }
        // [RX state]
        if (state == 1){
            // [Poll RX state machine]
            protocol_errno_t rx_errno = protocol_rx_poll(
                &fd,
                &rx,
                &rx_packet
            );
            // [Check RX]
            if (rx_errno != PROTOCOL_OK) printf("protocol_rx_poll(): Something went wrong!\n");
            if (rx.state == PROTOCOL_RX_HEADER) printf("Waiting for header...\n");
            else if (rx.state == PROTOCOL_RX_ACK) printf("Sending confirmation...\n");
            if (rx.complete){
                if (rx.ack_state == PROTOCOL_ACK) printf("Response received correctly!\n");
                else if (rx.ack_state == PROTOCOL_NACK) printf("Response received was not correct!\n");
                // [Check if message received]
                if (rx.valid){
                    protocol_errno_t rx_frame_errno = protocol_set_buffer_to_frame(
                        &rx_frame,
                        rx.buffer,
                        sizeof(rx.buffer)
                    );
                    if (rx_frame_errno == PROTOCOL_OK){
                        memcpy(response, rx_frame.payload, rx_frame.length);
                        if (rx_frame.relevance == 0)
                            printf("Received: %s\n", response);
                    }
                    recv_events++;
                }
                state = 0;
            }
        }
    }
    printf("End of loop...\n");

    close(fd);
    return 0;
}
