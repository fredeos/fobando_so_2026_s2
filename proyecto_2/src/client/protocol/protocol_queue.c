// Archivo de implementacion de <client/protocol/worker.h>
#include "client/protocol/worker.h"

#include "string.h"

int pqueue_append(const void *src, size_t length, uint8_t priority, protocol_queue_t* queue){
    // Negar peticiones con valores nulos
    if (src == NULL || queue == NULL || length == 0u)
        return -1;
    // Negar peticiones si la cola esta llena
    if (queue->last == PROTOCOL_QUEUE_SIZE-1)
        return 1;
    
    pthread_mutex_lock(&queue->mutex);   // blockear hasta recibir control

    int16_t last = queue->last + 1;

    memcpy(queue->messages[last], src, length);
    queue->priority[last] = priority;

    queue->last = last;
    queue->available = (last >= 0);

    pthread_mutex_unlock(&queue->mutex); // devolver control

    return 0;
}

int pqueue_consume(void *dst, size_t length, uint8_t *priority, protocol_queue_t* queue){
    // Negar peticiones con valores nulos o buffers de menor capacidad al requerido
    if (queue == NULL || (dst != NULL && length < PROTOCOL_MESSAGE_LEN))
        return -1;
    // Si no hay mensajes disponibles retorna inmediatamente
    if (queue->last < 0)
        return 0;

    pthread_mutex_lock(&queue->mutex);   // blockear hasta recibir control

    if (dst != NULL) memcpy(dst, queue->messages[0], strlen(queue->messages[0]) + 1);
    if (priority != NULL) *priority = queue->priority[0];
    for (unsigned i = 0; i < PROTOCOL_QUEUE_SIZE; i++){
        if (i < PROTOCOL_QUEUE_SIZE-1){
            memcpy(queue->messages[i], queue->messages[i+1], PROTOCOL_MESSAGE_LEN);
            queue->priority[i] = queue->priority[i+1];
        } else {
            memset(queue->messages[i], 0, PROTOCOL_MESSAGE_LEN);
            queue->priority[i] = 0u;
        }
    }
    queue->last--;
    queue->available = (queue->last >= 0);

    pthread_mutex_unlock(&queue->mutex); // devolver control

    return 1;
}