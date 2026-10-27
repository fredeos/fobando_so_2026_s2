#ifndef HOST_WORKERS_NODE_WORKER_H
#define HOST_WORKERS_NODE_WORKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comms/comms.h"
#include "host/config.h"
#include "host/event_queue.h"
#include "host/planner.h"

#define EXPECTED_DEVICES 2
#define HANDSHAKE_TIMEOUT_SECONDS 5
#define CALIBRATION_REQUEST_ID 2u
#define FIRST_CONTROL_REQUEST_ID 0x80000000u

typedef struct {
    HostConfig config;
    const char *devices[EXPECTED_DEVICES];
    uint32_t node_ids[EXPECTED_DEVICES];
    comms_handler_t handlers[EXPECTED_DEVICES];
    char sent[EXPECTED_DEVICES][COMMS_MESSAGE_CAPACITY];
    bool initialized[EXPECTED_DEVICES];
    bool handler_started[EXPECTED_DEVICES];
    bool running[EXPECTED_DEVICES];
    Planner planner;
    uint32_t next_deploy_request_id;
    size_t initialized_count;
} SerialReportSession;

typedef enum {
    NODE_COMMAND_DEPLOY = 1,
    NODE_COMMAND_START,
    NODE_COMMAND_STOP
} NodeCommandType;

typedef struct {
    NodeCommandType type;
    Message message;
} NodeCommand;

typedef struct {
    bool ok;
    bool accepted;
    uint32_t ut_per_tick;
    RejectReason reason;
} NodeReply;

typedef struct {
    SerialReportSession serial;
    size_t index;
    int command_read_fd;
    int reply_write_fd;
} NodeWorkerContext;

/* El fork de cada nodo crea su propia copia de estos datos de ejecución. */
extern uint64_t latest_node_ticks[EXPECTED_DEVICES];
extern uint64_t node_stop_heartbeat_baseline[EXPECTED_DEVICES];
extern bool node_started_once[EXPECTED_DEVICES];
extern bool node_reports_drained[EXPECTED_DEVICES];
extern bool node_stop_pending[EXPECTED_DEVICES];

/* Funciones compartidas con el coordinador para transporte y reportes. */
bool publish_report_event(EventQueue *queue, const Event *event);
uint64_t host_time_ns(void);
uint64_t monotonic_time_ms(void);
bool send_message(comms_handler_t *handler, char *buffer,
                  const Message *message, uint32_t node_id,
                  const char *message_name);
bool wait_for_ack(comms_handler_t *handler, uint32_t node_id,
                  uint32_t request_id, const char *device);
bool wait_for_calibration(comms_handler_t *handler, uint32_t node_id,
                          uint32_t request_id, const char *device,
                          uint32_t *ut_per_tick);
bool wait_for_task_deploy(EventQueue *queue, comms_handler_t *handler,
                          uint32_t node_id, uint32_t request_id,
                          const char *device, bool *accepted,
                          RejectReason *reason);
bool wait_for_control_reply(EventQueue *queue, comms_handler_t *handler,
                            uint32_t node_id, uint32_t request_id,
                            MessageType expected_type, const char *device,
                            unsigned int timeout_seconds);
bool publish_bad_frame_reports(
    EventQueue *queue, const uint32_t node_ids[EXPECTED_DEVICES],
    comms_handler_t handlers[EXPECTED_DEVICES],
    const bool enabled[EXPECTED_DEVICES]);
bool pump_node_reports(EventQueue *queue,
                       const uint32_t node_ids[EXPECTED_DEVICES],
                       comms_handler_t handlers[EXPECTED_DEVICES],
                       const bool enabled[EXPECTED_DEVICES]);
bool transfer_process_message(int descriptor, void *data, size_t size,
                              bool writing, int timeout_ms);
void stop_handlers(comms_handler_t *handlers, const bool *handler_started,
                   size_t count);

int node_worker_run(EventQueue *queue, void *context);
int node_worker_stop(EventQueue *queue, void *context);

#endif
