#ifndef COMMON_MESSAGES_H
#define COMMON_MESSAGES_H

/*
 * Tipos de mensajes intercambiados
 * entre el Host y los microcontroladores.
 */
typedef enum {
    MSG_CONFIG = 1,
    MSG_CALIBRATE_REQUEST,
    MSG_CALIBRATE_RESPONSE,
    MSG_DEPLOY_TASK,
    MSG_START,
    MSG_EVENT,
    MSG_TICK_REPORT,
    MSG_TRIGGER_SPORADIC,
    MSG_STOP,
    MSG_ACK,
    MSG_NACK
} MessageType;

#endif // COMMON_MESSAGES_H