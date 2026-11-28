// Archivo de implementacion de <client/protocol/worker.h>
#include "client/protocol/worker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "client/timing.h"

#define SETUP(operation) do { \
    if (operation){ \
        printf("protocol_worker(): error during setup \n"); \
        return -1; \
    } \
} while (0);

#define TRY(operation, verbose, msg, id) do { \
    if ((operation) && verbose) printf("protocol_worker(%u): %s \n", id, msg); \
} while (0);

/* Inicializa las propiedades del worker */
int init_pworker(
    uint8_t id, 
    const char* device,
    bool debug,
    uint64_t timeout_ms,
    uint8_t criteria,
    protocol_worker_t* worker
){
    // Excepciones de casos nulos
    if (device == NULL || worker == NULL)
        return -1;
    // Configurar campos del worker
    worker->id = id;
    worker->device = device;

    worker->debug = debug;
    worker->running = false;
    worker->online = false;
    worker->kill = false;

    worker->timeout_ms = timeout_ms;
    worker->criteria = criteria;

    pthread_mutex_init(&worker->mutex, NULL);
    // Configurar puerto serial
    worker->fd = -1;
    worker->config = (posix_serial_config_t){
        .device = worker->device,
        .baudrate = SERIAL_BAUDRATE
    };
    SETUP ( serial_configure(&worker->fd, &worker->config) != SERIAL_OK );
    // Inicializar estructuras de protocolo
    SETUP( protocol_tx_init(&worker->tx, &worker->tx_packet) != PROTOCOL_OK );
    SETUP( protocol_rx_init(&worker->rx, &worker->rx_packet) != PROTOCOL_OK );
    // Inicializar colas de mensajes recibidos
    pthread_mutex_init(&worker->in.mutex, NULL);
    worker->in.available = false;
    worker->in.last = -1;
    for (unsigned i = 0; i < PROTOCOL_QUEUE_SIZE; i++){
        worker->in.priority[i] = 0u;
        memset(worker->in.messages[i], 0, PROTOCOL_MESSAGE_LEN);
    }
    // Inicializar colas de mensajes para enviar
    pthread_mutex_init(&worker->out.mutex, NULL);
    worker->out.available = false;
    worker->out.last = -1;
    for (unsigned i = 0; i < PROTOCOL_QUEUE_SIZE; i++){
        worker->out.priority[i] = 0u;
        memset(worker->out.messages[i], 0, PROTOCOL_MESSAGE_LEN);
    }
    return 0;
}

/* Inicia el hilo de comunicacion del worker */
int start_pworker(protocol_worker_t* worker){
    if (worker == NULL)
        return -1;

    // Crear el thread
    pthread_t protocol_thread;
    int code = pthread_create(
        &protocol_thread,
        NULL,
        protocol_worker,
        worker
    );
    if (code < 0) return 1;
    pthread_detach(protocol_thread);

    // Activar bandera
    pthread_mutex_lock(&worker->mutex); // blockear hasta recibir control
    worker->running = true;
    pthread_mutex_unlock(&worker->mutex); // devolver control
    
    return 0;
}

/* Terminar el hilo de comunicacion del worker */
int kill_pworker(protocol_worker_t* worker){
    if (worker == NULL)
        return -1;

    pthread_mutex_lock(&worker->mutex); // blockear hasta recibir control
    worker->kill = true;
    pthread_mutex_unlock(&worker->mutex); // devolver control
    
    return 0;
}

/* Rutina de comunicacion con dispositivo serial */
void* protocol_worker(void *arg){
    // << Extraer la referencia a esta instancia >>
    protocol_worker_t* this = (protocol_worker_t*) arg;
    // << Extraer variables del worker >>
    // [TX:output]
    protocol_tx_t *tx = &this->tx;
    protocol_frame_t *tx_frame = &this->tx_frame;
    serial_packet_t *tx_packet = &this->tx_packet;
    // [RX: input]
    protocol_rx_t *rx = &this->rx;
    protocol_frame_t *rx_frame = &this->rx_frame;
    serial_packet_t *rx_packet = &this->rx_packet;
    // [Colas de mensajes]
    protocol_queue_t* in  = &this->in; 
    protocol_queue_t* out = &this->out;
    // [Objetos de comunicacion serial]
    int *fd = &this->fd;
    posix_serial_config_t *config = &this->config;
    // << Variables del worker >>
    bool resync = false;    // bandera para sincronizacion
    bool disconnect = false;// bandera para reconexion
    char ping[] = "PING";// mensaje default
    uint64_t last = get_time_ms();
    // << Loop de comunicacion >>
    int state = 0; // estado de FSM
    while (1){
        // ----------------------------------------------------------------------------------
        /* Intento de reconexion */
        if (resync || disconnect){
            state = 0; // enviar un nuevo mensaje
            // << Reiniciar las FSM>>
            protocol_tx_init(tx, tx_packet); // reset TX FSM
            protocol_rx_init(rx, rx_packet); // reset RX FSM
            // << Revisar si es necesario reconectar >>
            if (disconnect){
                serial_errno_t ser_errno;
                TRY ( (ser_errno = serial_configure(fd, config)) == SERIAL_OK, this->debug, "serial(): dispositivo conectado", this->id );
                if (ser_errno == SERIAL_OK) disconnect = false;
            }
            // << Remover condicion de resincronizado >>
            resync = false;
        }
        // ----------------------------------------------------------------------------------
        /* Revision de estado del worker */
        pthread_mutex_lock(&this->mutex); // blockear hasta recibir control
        bool running = this->running;
        this->online = !resync && !disconnect;
        bool killed = this->kill;
        pthread_mutex_unlock(&this->mutex); // devolver control
        /* Determinar condicion del thread */
        if (killed && state == 0) break;

        if (!running) continue;

        if (disconnect || resync) continue;
        // ----------------------------------------------------------------------------------
        protocol_errno_t errno = PROTOCOL_OK;
        /* (0) Configuracion de mensajes */
        /* En esta etapa se configura el mensaje por enviar */
        if (state == 0){
            // Mensaje default
            char* message = ping;
            tx_frame->relevance = 0u;
            // Definir respuesta
            int loaded = 0;
            if (out->available)
                loaded = pqueue_consume(tx_frame->payload, sizeof(tx_frame->payload), &tx_frame->relevance, out); // consumir un mensaje
            if (loaded == 0)
                memcpy(tx_frame->payload, message, strlen(message) + 1); // copiar hasta '\0';
            tx_frame->length = strlen((char*) tx_frame->payload) + 1; // incluir '\0'
            // Cargar frame al buffer
            errno = protocol_set_frame_to_buffer(tx_frame, tx->buffer, sizeof(tx->buffer));
            // Transicion a TX
            if (errno == PROTOCOL_OK) state = 1;
        }
        /* (1) TX FSM */
        /* TX se encarga de enviar mensajes al dispositivo */
        if (state == 1){
            // << Poll TX FSM >>
            protocol_tx_state_t previous = tx->state;
            errno = protocol_tx_poll(fd, tx, tx_packet);
            if (tx->state != previous) last = get_time_ms(); // medir tiempo al detectar actividad
            // << Check TX status >>
            if (tx->complete && tx->valid){
                // Transicion a RX
                state = 2;
            }
        }
        /* (2) RX FSM */
        /* RX se encarga de recibir mensajes al dispositivi */
        if (state == 2){
            // << Poll RX FSM >>
            protocol_rx_state_t previous = rx->state;
            errno = protocol_rx_poll(fd, rx, rx_packet);
            if (rx->state != previous) last = get_time_ms(); // medir tiempo al detectar actividad
            // << Check RX status >>
            if (rx->complete && rx->valid){
                // Cargar buffer al frame
                errno = protocol_set_buffer_to_frame(rx_frame, rx->buffer, sizeof(rx->buffer));
                // Validar mensaje recibido
                if (errno == PROTOCOL_OK && rx_frame->relevance >= this->criteria){
                    // Guardar mensaje/payload
                    while (pqueue_append(rx_frame->payload, rx_frame->length, rx_frame->relevance, in) > 0); // guardar un mensaje
                }
                // Transicion a configuracion
                state = 0;
            }
        }
        // ----------------------------------------------------------------------------------
        uint64_t now = get_time_ms();
        /* Logica de desconexion */
        TRY ( errno != PROTOCOL_OK, this->debug, "serial(): dispositivo desconectado", this->id);
        if (errno != PROTOCOL_OK){ // un error de protocolo se asume como error de conexion
            disconnect = true;
            TRY ( close(*fd) == 0, this->debug, "close(): file descriptor cerrado", this->id);
            last = now;
        }
        /* Logica de desincronizacion */
        else if (now - last >= this->timeout_ms){ // timeout por inactividad
            resync = true;
            last = now;
        }
    }
    // << Cierre de comunicacion >>
    if (*fd >= 0) TRY( close(*fd) == 0, this->debug, "close(): file descriptor cerrado", this->id);
    this->running = false;
    return NULL;
}