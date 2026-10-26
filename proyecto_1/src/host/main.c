#define _DEFAULT_SOURCE

#include <stdio.h>

#include "serial/serial.h"
#include "serial/posix.h"
#include "common/protocol.h"
#include "comms/comms.h"
#include "timing/timing.h"

#define BUFFER_LEN 80      // message buffer len
#define EXPECTED_DEVICES 2 // expected devices

#define TICK_SIZE   10  // 10 ms
#define TICK_LIMIT  10  // 10 ticks

#define LIFETIME   30u // program lifetime: 30 s
#define MSG_TICK   1u  // send message every 1 s
#define PRIORITY    1  // priority level 1

int main(int argc, char *argv[])
{
    if (argc != 1 + EXPECTED_DEVICES) {
        fprintf(stderr, "Usage: %s /dev/ttyUSB0\n", argv[0]);
        return 1;
    }
    // << Program variables >>
    const char* marco = "MARCO";
    
    int turn = -1;
    bool warning = true;
    uint64_t start = get_time_ms() / 1000;
    // << Configure message buffers >>
    char response[EXPECTED_DEVICES][BUFFER_LEN];
    char message [EXPECTED_DEVICES][BUFFER_LEN];

    // << Configure communication interface >>
    int online_count = EXPECTED_DEVICES; // assume all as online
    bool online[EXPECTED_DEVICES]; // handler devices online

    comms_handler_t handler[EXPECTED_DEVICES]; // handler objects
    uint64_t last[EXPECTED_DEVICES];    // last tick for devices
    for (int i = 0; i < EXPECTED_DEVICES; i++){
        last[i] = start;

        comms_init_handler(
            i,
            &handler[i],
            argv[1+i],
            PRIORITY,
            TICK_SIZE,
            TICK_LIMIT
        );

        handler[i].sent.content = message[i];
        handler[i].recv.content = response[i];

        online[i] = true;

        comms_setup_handler(&handler[i]);
    }
    // << Loop procedure >>
    printf("Start of procedure...\n");
    printf("Program lifetime: %u s\n", LIFETIME);
    while (1){
        // [Change turn]
        turn++;
        if (turn == EXPECTED_DEVICES) turn = 0;
        // [Check progam lifetime]
        uint64_t now = get_time_ms() / 1000; // convert to seconds
        bool end_of_program = (now - start >= LIFETIME);
        if (end_of_program) {
            printf("Lifetime completed!\n");
            break;
        }
        // [Get device status]
        pthread_mutex_lock(&handler[turn].mutex);   // lock
        // + Check if handler is running
        bool is_running  = handler[turn].running;
        bool is_finished = handler[turn].finished;
        // + Check handler activity
        bool is_online    = handler[turn].online;
        bool went_online  = !online[turn] && is_online;
        bool went_offline = online[turn] && !is_online;
        pthread_mutex_unlock(&handler[turn].mutex);  // unlock

        // [Update program information
        if (is_running || is_finished){
            online[turn] = is_online;
            // [Update counter]
            if (went_online)  {
                online_count++;
                warning = true;
            }
            if (went_offline) online_count--;
            // [Check counter]
            if (online_count == 0 && warning){
                printf("All devices disconnected!\n");
                warning = false;
            }
        } else continue;

        // [Check on handler]
        if(online[turn]){
            // (Configure a mesage)
            bool on_tick = (now - last[turn] >= MSG_TICK);
            if(on_tick){
                comms_request_send(
                    &handler[turn],
                    marco,
                    1
                );
                last[turn] += MSG_TICK;
            }
            // (Message sent)
            if (handler[turn].sent.rdy){
                printf("Sent: %s [to device(%d) with %u priority]\n", 
                    handler[turn].sent.content,
                    turn,
                    handler[turn].sent.priority
                );
                comms_acquire_send(&handler[turn]);
            }
            // (Message received)
            if (handler[turn].recv.rdy){
                printf("Received: %s [from device(%d) with %u priority]\n",
                    handler[turn].recv.content,
                    turn,
                    handler[turn].recv.priority
                );
                comms_acquire_receive(&handler[turn]);
            }
        }
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