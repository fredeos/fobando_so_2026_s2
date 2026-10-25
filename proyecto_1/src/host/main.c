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
#include "comms/comms.h"

#define BUFFER_LEN 80
#define EXPECTED_DEVICES 2
#define TICK_SIZE   10
#define TICK_LIMIT  10

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
    if (argc != 1 + EXPECTED_DEVICES) {
        fprintf(stderr, "Usage: %s /dev/ttyUSB0\n", argv[0]);
        return 1;
    }
    // << Configure communication interface >>
    bool is_online[EXPECTED_DEVICES];
    int  offline = 0;
    char response[EXPECTED_DEVICES][BUFFER_LEN];
    char message [EXPECTED_DEVICES][BUFFER_LEN];

    comms_handler_t handler[EXPECTED_DEVICES];
    for (int i = 0; i < EXPECTED_DEVICES; i++){
        comms_init_handler(
            i,
            &handler[i],
            argv[1+i],
            0,
            TICK_SIZE,
            TICK_LIMIT
        );

        handler[i].sent.content = message[i];
        handler[i].recv.content = response[i];

        is_online[i] = true;

        comms_setup_handler(&handler[i]);
    }
    // << Program variables >>
    int turn = 0;

    int count[EXPECTED_DEVICES] = {0};
    const char* marco = "MARCO";

    // << Loop procedure >>
    printf("Start of procedure...\n");
    while (1){
        pthread_mutex_lock(&handler[turn].mutex);   // lock
        bool went_online  = !is_online[turn] && handler[turn].online;
        bool went_offline = is_online[turn] && !handler[turn].online;

        if (went_online) {
            printf("Device(%d) connected!\n", turn);
            offline--;
            printf("Remaining: %d\n", offline);
        }
        if (went_offline) {
            printf("Device(%d) disconnected!\n", turn);
            offline++;
            printf("Remaining: %d\n", offline);
        }

        if (went_online || went_offline)
            is_online[turn] = handler[turn].online;
        pthread_mutex_unlock(&handler[turn].mutex);  // unlock

        if (offline == EXPECTED_DEVICES){
            printf("All devices disconnected\n");
            break;
        }
        
        if(is_online[turn]){

            if(count[turn] == 2){
                comms_request_send(
                    &handler[turn],
                    marco,
                    1
                );
            } else if (count[turn] == 7){
                printf("Stopping device(%d)...\n", turn);
                comms_stop_handler(&handler[turn]);
                printf("Device(%d) stopped\n", turn);
                count[turn]++;
            }

            if (handler[turn].sent.rdy){
                printf("Sent: %s [to device(%d) with %u priority]\n", 
                    handler[turn].sent.content,
                    turn,
                    handler[turn].sent.priority
                );
                comms_acquire_send(&handler[turn]);
            }

            if (handler[turn].recv.rdy){
                printf("Received: %s [from device(%d) with %u priority]\n",
                    handler[turn].recv.content,
                    turn,
                    handler[turn].recv.priority
                );
                comms_acquire_receive(&handler[turn]);
                count[turn]++;
            }
        }

        turn++;
        if (turn == EXPECTED_DEVICES) turn = 0;
    }
    // << End of program >>
    printf("End of procedure...\n");
    printf("Closing all handlers...\n");
    for (int i = 0; i < EXPECTED_DEVICES; i++){
        comms_stop_handler(&handler[i]);
        while (handler[i].running);
    }
    printf("All handlers closed...\n");
    return 0;
}
