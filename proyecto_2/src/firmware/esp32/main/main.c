// =================================================================================
// Informacion relevante de la implementacion
// =================================================================================

/* Sobre el controlador de pantalla OLED (128x160): ST7735S
 *
 *
 *
*/

/* Sobre el controlador de pantalla LCD (20x4): HD44780U + PCF8574
 *
 *
 *
*/

// =================================================================================
// Drivers, librerias y modulos
// =================================================================================

// + Drivers
#include "driver/uart.h" // UART driver
#include "gpio/gpio.h"   // GPIO driver

#include "serial/esp32.h"    // interfaz UART
#include "serial/serial.h"   // interfaz serial

// + Librerias de C
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// + Modulos
#include "common/protocol.h" // protocolo CRC-16
#include "common/json.h"     // json parser & serializer

// + Librerias
#include "freertos/FreeRTOS.h" // FreeRTOS
#include "freertos/task.h"     // tasks/tareas FreeRTOS
#include "freertos/semphr.h"

// =================================================================================
// Configuracion de pines
// =================================================================================

// + LEDs
#define LED0 33 // protocol 0 (RX)
#define LED1 26 // protocol 1 (TX)
#define LED2 14 // debug 0
#define LED3 13 // debug 1

// + Botones
/* NOTA: en ESP32 los botones manejan logica invertida/negada */
#define BTN0 32 // alternar
#define BTN1 25 // anterior
#define BTN2 27 // siguiente
#define BTN3 12 // confirmar

// + I2C pantalla LCD
#define SDA 21 // I2C wire
#define SCL 22 // I2C wire
#define OE  4  // habilitar

// + SPI pantalla OLED
#define SCK  18 // VPSI
#define MOSI 23 // VSPI
#define MISO 19 // VSPI
#define SS   5  // VSPI

#define RST  16 // reset
#define DC   17 // control

// =================================================================================
// Constantes
// =================================================================================

// + Buffers
#define PROTOCOL_BUFFER PROTOCOL_FRAME_BYTES 
#define MESSAGE_LENGTH  128u
#define QUEUE_SIZE      5u

// + Puerto UART
#define UART_PORT UART_NUM_0

// + Criterios de aceptacion (protocolo)
#define ACCEPT_CRITERIA  1u

// =================================================================================
// Scope global
// =================================================================================

// + Setup de dispositivos
bool SETUP_FAILURE = false;

// + Inputs de botones
bool buttons[4] = { 0 };
gpio_level_t btn_in[4] = { 0 };

// + Objeto de UART serial
esp32_uart_t uart;
esp32_uart_configs_t uart_config;

// + Objetos del protocolo
// [TX:output]
protocol_frame_t tx_frame;
protocol_tx_t tx;
serial_packet_t tx_packet;
// [RX:input]
protocol_frame_t rx_frame;
protocol_rx_t rx;
serial_packet_t rx_packet;

// + Objetos de comunicacion

/* Cola de mensajes para protocolo */
typedef struct {
    SemaphoreHandle_t mutex; // primitiva de sincronizacion
    char messages[QUEUE_SIZE][MESSAGE_LENGTH];  // mensajes
    uint8_t priority[QUEUE_SIZE];                // prioridad
    int16_t last;   // ultimo revisado
    bool available; // mensajes disponibles
} queue_t;

queue_t in;  // cola de mensajes recibidos
queue_t out; // cola de mensajes por enviar

// + Mensajes genericos
char* unknown_msg = "ESP32 didn't understand message";
char* pong = "PONG from ESP32";

// + Buffers de loop()
// [Mensajes JSON]
char input [MESSAGE_LENGTH] = { 0 };
char output[MESSAGE_LENGTH] = { 0 };
// [Campos JSON recibidos]
uint8_t action = 0u;
char command[24] = { 0 };

// =================================================================================
// Definiciones
// =================================================================================

// + Helper para setup
#define SETUP(operation) do {\
    SETUP_FAILURE = (operation); \
    if (SETUP_FAILURE) return; \
} while (0)

// =================================================================================
// Funciones auxiliares
// =================================================================================

/* Agregar elemento a la cola */
static int append(const char* src, size_t length, uint8_t priority, queue_t* queue){
    // Negar peticiones con valores nulos o largos mayores al
    if (src == NULL || queue == NULL || length > MESSAGE_LENGTH) 
        return -1;
    // Negar peticiones si la cola esta llena
    if (queue->last == QUEUE_SIZE-1)
        return 1;

    while (xSemaphoreTake(queue->mutex, 100) != pdTRUE); // blockear hasta recibir control

    int16_t last = queue->last + 1;

    memcpy(queue->messages[last], src, length);
    queue->priority[last] = priority;

    queue->last = last;
    queue->available = true;

    xSemaphoreGive(queue->mutex); // devolver control
    return 0;
}

/* Consumir elemento de la cola */
static int consume(char* dst, size_t length, uint8_t* priority, queue_t* queue){
    // Negar peticiones con valores nulos o largos mayores al
    if (queue == NULL || length > MESSAGE_LENGTH) 
        return -1;
    // Si no hay mensajes disponibles retorna de inmediato
    if (queue->last < 0)
        return 0;
    
    while (xSemaphoreTake(queue->mutex, 100) != pdTRUE); // blockear hasta recibir control

    if (dst != NULL) memcpy(dst, queue->messages[0], length);
    if (priority != NULL) *priority = queue->priority[0];
    for (int i = 0; i < QUEUE_SIZE; i++){
        if (i < QUEUE_SIZE-1){
            memcpy(queue->messages[i], queue->messages[i+1], MESSAGE_LENGTH);
            queue->priority[i] = queue->priority[i+1];
        } else {
            memset(queue->messages[i], 0, MESSAGE_LENGTH);
            queue->priority[i] = 0u;
        }
    }
    queue->last--;
    queue->available = (queue->last >= 0);

    xSemaphoreGive(queue->mutex); // devolver control
    return 1;
}

/* Obtener la accion del boton presionado */
static char* get_key_action(int button){
    switch (button){
        case 0: return "switch";
        case 1: return "prev";
        case 2: return "next";
        case 3: return "confirm";
    }
    return "not_binded";
}

// =================================================================================
// Tareas
// =================================================================================

/* Tarea para gestion de protocolo de comunicacion */
static void protocol_task(void *arg){
    int state = 0;  // estado de FSM
    // << Loop de protocolo >>
    while (1){
        /* (0): RX state */
        /* RX se encarga de recibir mensajes */
        if (state == 0){
            // << Actualizar LEDs de protocolo >>
            board_gpio_write(LED0, GPIO_HIGH); // RX activo
            board_gpio_write(LED1, GPIO_LOW);  // TX inactivo
            // << Poll RX FSM >>
            protocol_rx_poll(&uart, &rx, &rx_packet);
            // << Check RX status >>
            if (rx.complete && rx.valid){
                // Mensaje invalido (respuesta default)
                char *response = unknown_msg;
                tx_frame.relevance = 0u;
                // Mensaje valido
                // (Cargar buffer al frame)
                protocol_set_buffer_to_frame(&rx_frame, rx.buffer, sizeof(rx.buffer));
                // (Cargar mensaje/payload)
                char *message = (char*) rx_frame.payload;
                // (Revisar mensaje y prioridad)
                if (rx_frame.relevance == 0u && strcmp(message, "PING") == 0) response = pong; // ping -> pong
                else if (rx_frame.relevance >= ACCEPT_CRITERIA) { // mensajes prioritarios
                    while (append(message, rx_frame.length, rx_frame.relevance, &in) > 0);
                }
                // Definir respuesta
                int loaded = 0;
                if (out.available){
                    char* payload = (char*) tx_frame.payload;
                    loaded = consume(payload, MESSAGE_LENGTH, &tx_frame.relevance, &out); // copiar mensaje completo
                    tx_frame.length = strlen(payload) + 1; // incluir '\0'
                }
                if (loaded < 1){
                    memcpy(tx_frame.payload, response, strlen(response)+1); // copiar hasta '\0'
                    tx_frame.length = strlen(response) + 1; // incluir '\0'
                }
                protocol_set_frame_to_buffer(&tx_frame, tx.buffer, sizeof(tx.buffer));
                // Transicion a TX
                state = 1;
                memset(rx.buffer, 0, sizeof(rx.buffer));
            }
        }
        /* (1): TX state */
        /* TX se encarga de enviar mensajes */
        if (state == 1){
            // << Actualizar LEDs de protocolo >>
            board_gpio_write(LED0, GPIO_LOW); // RX inactivo
            board_gpio_write(LED1, GPIO_HIGH);// TX activo
            // << Poll TX FSM >>
            protocol_tx_poll(&uart, &tx, &tx_packet);
            // << Check TX status >>
            if (tx.complete && tx.valid) {
                memset(tx.buffer, 0, sizeof(tx.buffer));
                state = 0;
            }
        }
        /* delay opcional para mejorar alternancia de procesos */
        /* se puede comentar en caso de ser necesario */
        // vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// =================================================================================
// Codigo fuente
// =================================================================================

// Arduino-style setup function
void setup(void){
    /* Add your setup code here */
    // -------------------------------------------------
    // Configuracion de pines
    // -------------------------------------------------
    // + Pines para LEDs de protocol
    board_gpio_init(LED0, GPIO_OUTPUT); // protocol 0
    board_gpio_init(LED1, GPIO_OUTPUT); // protocol 1
    // + Pines para LEDS de debug
    board_gpio_init(LED2, GPIO_OUTPUT); // debug 0
    board_gpio_init(LED3, GPIO_OUTPUT); // debug 1
    // + Pines de botones
    board_gpio_init(BTN0, GPIO_INPUT);  // alternar
    board_gpio_init(BTN1, GPIO_INPUT);  // anterior
    board_gpio_init(BTN2, GPIO_INPUT);  // siguiente
    board_gpio_init(BTN3, GPIO_INPUT);  // confirmar
    // + Pines de I2C
    // -------------------------------------------------
    // Configuracion de interfaces y controladores
    // -------------------------------------------------
    // + Perifericos I2C
    // + Perifericos SPI
    // + Interfaz serial UART
    uart_config = (esp32_uart_configs_t){
        .port_num = UART_PORT,
        .baudrate = SERIAL_BAUDRATE,
        .rx_buffer_size = 1024,
        .tx_buffer_size = 0,
        .tick_ms = 100
    };
    SETUP ( serial_configure(&uart, &uart_config) != SERIAL_OK ); // configurar puerto UART
    // + Interfaz de protocolo CRC-16
    SETUP ( protocol_tx_init(&tx, &tx_packet) != PROTOCOL_OK);  // configurar TX
    SETUP ( protocol_rx_init(&rx, &rx_packet) != PROTOCOL_OK);  // configurar RX
    // -------------------------------------------------
    // Inicializar variables y objetos compartidos
    // -------------------------------------------------
    // + Colas de mensajes recibidos
    in.mutex = xSemaphoreCreateMutex();
    in.available = false;
    in.last = -1;
    for (int i = 0; i < QUEUE_SIZE; i++){
        in.priority[i] = 0u;
        memset(in.messages[i], 0, MESSAGE_LENGTH);
    }
    // + Colas de mensajes para enviar
    out.mutex = xSemaphoreCreateMutex();
    out.available = false;
    out.last = -1;
    for (int i = 0; i < QUEUE_SIZE; i++){
        out.priority[i] = 0u;
        memset(out.messages[i], 0, MESSAGE_LENGTH);
    }
    // -------------------------------------------------
    // Configuracion de estimulos iniciales
    // -------------------------------------------------
    // + Estimulos para LEDs
    board_gpio_write(LED0, GPIO_LOW);
    board_gpio_write(LED1, GPIO_LOW);
    board_gpio_write(LED2, GPIO_LOW);
    board_gpio_write(LED3, GPIO_LOW);
    // + Estimulo para habilitar control I2C
    // -------------------------------------------------
    // Configuracion de tareas
    // -------------------------------------------------
    // + Gestion de protocolo
    xTaskCreate(protocol_task, "protocol_task", 1024, NULL, 2, NULL);
    // + Gestion de I2C
    // + Gestion de SPI
}

// Arduino-style loop function
void loop(void){
    /* Add your loop code here */
    // -------------------------------------------------
    // Lectura de inputs
    // -------------------------------------------------
    // + Lectura de niveles logicos
    board_gpio_read(BTN0, &btn_in[0]);
    board_gpio_read(BTN1, &btn_in[1]);
    board_gpio_read(BTN2, &btn_in[2]);
    board_gpio_read(BTN3, &btn_in[3]);
    // + Interpretacion de inputs
    for (int i = 0; i < 4; i++) buttons[i] = (btn_in[i] == GPIO_HIGH);
    // -------------------------------------------------
    // Escritura de outputs
    // -------------------------------------------------
    // + Escritura en LEDs
    board_gpio_write(LED2, GPIO_LOW);
    board_gpio_write(LED3, GPIO_LOW);
    // -------------------------------------------------
    // Lectura de mensajes recibidos
    // -------------------------------------------------
    if (in.available && consume(input, sizeof(input), NULL, &in) == 1){
        // Definir campos esperados
        JsonField fields[] = {
            {.name = "command", .pair = { 0 }},
            {.name = "action", .pair = { 0 }}
        };
        // Parsear objecto
        if ( read_object(input, strlen(input), fields, COUNT(fields)) == JSON_PARSE_OK ){
            // Leer valores
            read_string(&fields[0], command, sizeof(command));
            read_uint(&fields[1], &action, sizeof(action));
            if (strcmp(command, "led") == 0){
                if      (action == 0) board_gpio_write(LED2, GPIO_HIGH);
                else if (action == 1) board_gpio_write(LED3, GPIO_HIGH);
            }
            memset(command, 0, sizeof(command));
        }
        memset(input, 0, sizeof(input)); // limpiar buffer
    }
    // -------------------------------------------------
    // Escritura de eventos
    // -------------------------------------------------
    for (int i = 0; i < 4; i++){
        if (buttons[i]){
            // Contruir objeto
            char* btn_event  = "button_pressed";
            JsonString btn_event_str = {.string = btn_event, .length = strlen(btn_event)};

            char* btn_action = get_key_action(i);
            JsonString btn_action_str = {.string = btn_action, .length = strlen(btn_action)};

            JsonPair pairs[] = {
                {.name = "event", .value = &btn_event_str, .size = sizeof(btn_event_str), .type = JSON_STRING},
                {.name = "action", .value = &btn_action_str, .size = sizeof(btn_action_str), .type = JSON_STRING}
            };
            JsonObject object = {.pairs = pairs, .count = COUNT(pairs)};

            // Serializar objeto
            size_t written = 0;
            write_object(&object, output, sizeof(output), &written, true);
            output[written] = '\0';

            // Enviar objeto
            while (append(output, strlen(output) + 1, ACCEPT_CRITERIA+1, &out) > 0); // incluir '\0';

            // Limpiar buffer
            memset(output, 0, sizeof(output));
        }
    }
}

// ESP32 main application entry point
void app_main(void){
    // << Setup logic >>
    setup();

    // << Failure loop >>
    if (SETUP_FAILURE) while(1);

    // << Procedure loop >>
    while(1) loop();
}