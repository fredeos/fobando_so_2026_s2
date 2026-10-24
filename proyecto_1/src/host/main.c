#define _DEFAULT_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "serial/serial.h"
#include "serial/posix.h"

#define BUFFER_LEN  128u
#define MESSAGE_LEN 64u

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "Usage: %s /dev/ttyUSB0\n", argv[0]);
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
    setup_fail = (conf_errno != SERIAL_OK);

    // [Packets]
    uint8_t in[BUFFER_LEN];
    serial_packet_t input = {
        .mode   = SERIAL_IN,
        .buffer = in,
        .length = sizeof(in),
        .complete  = false,
        .processed = 0
    };

    uint8_t out[BUFFER_LEN];
    serial_packet_t output = {
        .mode   = SERIAL_OUT,
        .buffer = out,
        .length = sizeof(out),
        .complete  = false,
        .processed = 0
    };
    // << Message handling >>
    const char *ping  = "PING";
    const char *check = "CHECK";
    
    char response[MESSAGE_LEN];
    char message [MESSAGE_LEN];
    // << Procedure loop >>
    int recv_events = 0;
    int send_events = 0;
    int alt = false;

    if (setup_fail) printf("Something went wrong when setting serial communication!\n");
    else printf("Starting procedure...\n");
    while (!setup_fail){
        if (recv_events == 5)
            break;
        // [Read from serial]
        // read bytes
        size_t rd = 0;
        serial_errno_t rd_errno = serial_read(
            &fd, &input, &rd
        );

        if (rd_errno == SERIAL_FAILURE)
            serial_restart_packet(&input);
        else if (rd > 0)
            printf("Read %lu bytes\n", rd);
        
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
            printf("Received: %s\n", response);
            // Restart packet for next message
            serial_restart_packet(&input);
            // Count event
            recv_events++;
            // Restart send
            if (send_events == 2) send_events = 0;
        }
        // [Send with serial]
        if (send_events == 0){ // setup message
            // Select message
            const char* msg = ping;
            if (alt) msg = check;
            size_t len = strlen(msg);
            alt = !alt;
            printf("Message: %s (%lu)\n", msg, len);
            // Build message
            for (size_t i = 0; i < sizeof(message);i++){
                if (i < len)
                    message[i] = msg[i];
                else if (i == len)
                    message[i] = '\n';
                else
                    message[i] = '\0';
            }
            // Set to buffer
            const uint8_t* buffer = (uint8_t*) message;
            for (size_t i = 0; i < sizeof(out);i++){
                if (i < sizeof(message))
                    out[i] = buffer[i];
                else
                    out[i] = 0x00u;
            }
            // Move to send
            send_events = 1;
        }
        if (send_events == 1){ // send message
            // send bytes
            size_t wd = 0;
            serial_errno_t wd_errno = serial_write(
                &fd, &output, &wd
            );

            if (wd_errno == SERIAL_FAILURE)
                serial_restart_packet(&output);
            else if (wd > 0)
                printf("Sent %lu bytes\n", wd);

            if (output.complete){
                serial_restart_packet(&output);
                printf("Sent: %s", (char*)out);
                // Move to wait
                send_events = 2;
            }
        }
    }

    close(fd);
    return 0;
}