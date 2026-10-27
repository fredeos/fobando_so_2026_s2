#ifndef HOST_WORKERS_NODE_WORKER_H
#define HOST_WORKERS_NODE_WORKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comms/comms.h"
#include "host/config.h"
#include "host/event_queue.h"

#define EXPECTED_DEVICES 2
#define HANDSHAKE_TIMEOUT_SECONDS 5
#define CALIBRATION_REQUEST_ID 2u
#define FIRST_CONTROL_REQUEST_ID 0x80000000u

/* Recursos seriales que cada proceso worker conserva en su propia copia. */
typedef struct {
    HostConfig config;
    /* Solo el índice propio está poblado; se conserva el índice para helpers por arreglo. */
    const char *devices[EXPECTED_DEVICES];
    uint32_t node_ids[EXPECTED_DEVICES];
    comms_handler_t handlers[EXPECTED_DEVICES];
    char sent[EXPECTED_DEVICES][COMMS_MESSAGE_CAPACITY];
    bool initialized[EXPECTED_DEVICES];
    bool handler_started[EXPECTED_DEVICES];
    bool running[EXPECTED_DEVICES];
    uint32_t ut_per_tick[EXPECTED_DEVICES];
    uint64_t latest_ticks[EXPECTED_DEVICES];
    uint64_t tick_offset[EXPECTED_DEVICES];
    uint64_t stop_heartbeat_baseline[EXPECTED_DEVICES];
    bool started_once[EXPECTED_DEVICES];
    bool reports_drained[EXPECTED_DEVICES];
    bool stop_pending[EXPECTED_DEVICES];
} NodeSerialContext;

typedef enum {
    NODE_COMMAND_DEPLOY = 1,
    NODE_COMMAND_START,
    NODE_COMMAND_STOP,
    NODE_COMMAND_RECONFIGURE
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
    NodeSerialContext serial;
    size_t index;
    int command_read_fd;
    int reply_write_fd;
    int link_status_write_fd;
    bool link_status_known;
    bool last_link_online;
} NodeWorkerContext;

typedef struct {
    uint32_t node_id;
    uint64_t tick;
    bool online;
} NodeLinkStatus;

/* Funciones compartidas con el coordinador para transporte y reportes. */
bool publish_report_event(EventQueue *queue, const Event *event);
uint64_t host_time_ns(void);
uint64_t monotonic_time_ms(void);
bool send_message(comms_handler_t *handler, char *buffer,
                  const Message *message, uint32_t node_id,
                  const char *message_name);
bool wait_for_ack(comms_handler_t *handler, uint32_t node_id,
                  uint32_t request_id, const char *device);
bool wait_for_config_reply(EventQueue *queue, NodeSerialContext *serial,
                           comms_handler_t *handler, uint32_t node_id,
                           uint32_t request_id, const char *device,
                           bool *accepted, RejectReason *reason);
bool wait_for_calibration(comms_handler_t *handler, uint32_t node_id,
                          uint32_t request_id, const char *device,
                          uint32_t *ut_per_tick);
bool wait_for_task_deploy(EventQueue *queue, NodeSerialContext *serial,
                          comms_handler_t *handler,
                          uint32_t node_id, uint32_t request_id,
                          const char *device, bool *accepted,
                          RejectReason *reason);
bool wait_for_control_reply(EventQueue *queue, NodeSerialContext *serial,
                            comms_handler_t *handler,
                            uint32_t node_id, uint32_t request_id,
                            MessageType expected_type, const char *device,
                            unsigned int timeout_seconds);
bool publish_bad_frame_reports(
    EventQueue *queue, NodeSerialContext *serial,
    const uint32_t node_ids[EXPECTED_DEVICES],
    comms_handler_t handlers[EXPECTED_DEVICES],
    const bool enabled[EXPECTED_DEVICES]);
bool pump_node_reports(EventQueue *queue, NodeSerialContext *serial,
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
