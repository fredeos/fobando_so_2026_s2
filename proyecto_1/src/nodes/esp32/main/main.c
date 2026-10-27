#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include "common/protocol.h"
#include "common/message_parser.h"
#include "common/messages.h"
#include "common/task_store.h"
#include "calibrator.h"
#include "scheduler_runtime.h"
#include "serial/esp32.h"
#include "serial/serial.h"
#include "gpio/gpio.h"

#define LED0 18
#define LED1 19
#define LED2 21
#define LED3 22

#define UART_PORT   UART_NUM_0

#define NODE_ID 1u
#define MESSAGE_LEN (PROTOCOL_PAYLOAD_BYTES + 1u)

static uint32_t configured_tick_ms;
static MessageAlgorithm configured_algorithm = MESSAGE_SCHED_RMS;
static uint32_t configured_ut_per_tick;
static uint64_t next_tick_us;
/* Tabla temporal: los despliegues aceptados desaparecen al reiniciar el MCU. */
static TaskStore deployed_tasks;
static SchedulerRuntime scheduler_runtime;

/**
 * @brief Agrega un evento a la cola de reportes del nodo.
 *
 * Inserta el evento al final de la cola circular si hay espacio disponible.
 *
 * @param event Evento que se desea almacenar.
 * @return true si el evento se agrega correctamente; false si la cola está llena.
 */
static bool enqueue_node_report(const Event *event)
{
    if (report_count == NODE_REPORT_QUEUE_CAPACITY)
        return false;
    node_report_queue[report_tail] = *event;
    report_tail = (report_tail + 1u) % NODE_REPORT_QUEUE_CAPACITY;
    ++report_count;
    return true;
}

/**
 * @brief Extrae el evento más antiguo de la cola de reportes.
 *
 * Copia el primer evento disponible al parámetro de salida y actualiza
 * los índices y el contador de la cola circular.
 *
 * @param event Dirección donde se almacenará el evento extraído.
 * @return true si se obtiene un evento; false si la cola está vacía.
 */
static bool dequeue_node_report(Event *event)
{
    if (report_count == 0u)
        return false;
    *event = node_report_queue[report_head];
    report_head = (report_head + 1u) % NODE_REPORT_QUEUE_CAPACITY;
    --report_count;
    return true;
}

/**
 * @brief Intenta agregar a la cola el intervalo de ejecución pendiente.
 *
 * Si no existe un intervalo pendiente, no realiza ninguna operación.
 * Si la cola tiene espacio, agrega el intervalo y limpia su indicador
 * de validez.
 *
 * @return true si no hay intervalos pendientes o si se agrega correctamente;
 *         false si la cola está llena.
 */
static bool flush_pending_slice(void)
{
    if (!pending_slice_valid)
        return true;
    if (!enqueue_node_report(&pending_slice))
        return false;
    pending_slice_valid = false;
    return true;
}

/**
 * @brief Registra un evento de liberación de una tarea.
 *
 * Crea un evento con los identificadores de la tarea y del trabajo,
 * junto con el tick en que se liberó, e intenta agregarlo a la cola.
 *
 * @param task_id Identificador de la tarea liberada.
 * @param job_id  Identificador del trabajo liberado.
 * @param tick    Tick en el que ocurrió la liberación.
 */
static void report_task_released(uint32_t task_id, uint32_t job_id, uint32_t tick)
{
    Event event = {.type = EVENT_TASK_RELEASED, .source = EVENT_SOURCE_NODE,
        .node_id = NODE_ID, .task_id = task_id, .job_id = job_id, .tick = tick};
    (void)enqueue_node_report(&event);
}

/**
 * @brief Registra un evento de finalización de una tarea.
 *
 * Crea un evento con los identificadores de la tarea y del trabajo,
 * junto con el tick de finalización, e intenta agregarlo a la cola.
 *
 * @param task_id Identificador de la tarea completada.
 * @param job_id  Identificador del trabajo completado.
 * @param tick    Tick en el que terminó la ejecución.
 */
static void report_task_completed(uint32_t task_id, uint32_t job_id, uint32_t tick)
{
    Event event = {.type = EVENT_TASK_COMPLETED, .source = EVENT_SOURCE_NODE,
        .node_id = NODE_ID, .task_id = task_id, .job_id = job_id, .tick = tick};
    (void)enqueue_node_report(&event);
}

/**
 * @brief Registra un incumplimiento del plazo de una tarea.
 *
 * Primero intenta agregar a la cola el intervalo de ejecución pendiente.
 * Después, crea y encola el evento que identifica la tarea, el trabajo
 * y el tick en que se detectó el incumplimiento.
 *
 * @param task_id Identificador de la tarea que incumplió el plazo.
 * @param job_id  Identificador del trabajo afectado.
 * @param tick    Tick en el que se detectó el incumplimiento.
 */
static void report_deadline_miss(uint32_t task_id, uint32_t job_id, uint32_t tick)
{
    (void)flush_pending_slice();
    Event event = {.type = EVENT_DEADLINE_MISSED, .source = EVENT_SOURCE_NODE,
        .node_id = NODE_ID, .task_id = task_id, .job_id = job_id, .tick = tick};
    (void)enqueue_node_report(&event);
}

/* Aporte de hardware: LEDs del scheduler y botón con pull-down externo.
 * El procesamiento de mensajes y el runtime de comunicación siguen debajo. */
#define HARDWARE_BUTTON_PIN 27u
#define HARDWARE_MISS_PIN 15u
static const uint32_t hardware_task_pins[] = {LED0, LED1, LED2, LED3};
static uint32_t hardware_last_misses;
static bool hardware_button_stable_high;
static volatile uint64_t hardware_button_edge_us;
static volatile uint32_t hardware_button_events;

static void hardware_show_task(uint32_t id)
{
    uint32_t code = id == UINT32_MAX ? 0u : id;
    for (unsigned bit = 0; bit < 4; ++bit)
        board_gpio_write(hardware_task_pins[bit],
                         code & (1u << bit) ? GPIO_HIGH : GPIO_LOW);
}

/* El scheduler llama este observador después de revisar plazos y antes de UT. */
static void hardware_show_selection(uint32_t task_id)
{
    hardware_show_task(task_id);
    uint32_t misses = scheduler_runtime.scheduler.deadline_miss_count;
    board_gpio_write(HARDWARE_MISS_PIN,
                     misses != hardware_last_misses ? GPIO_HIGH : GPIO_LOW);
    hardware_last_misses = misses;
}

static portMUX_TYPE hardware_button_lock = portMUX_INITIALIZER_UNLOCKED;
static gptimer_handle_t hardware_debounce_timer;
static TaskHandle_t hardware_main_task;

static void hardware_clear_button(void)
{
    portENTER_CRITICAL(&hardware_button_lock);
    hardware_button_events = 0;
    portEXIT_CRITICAL(&hardware_button_lock);
}

/* La IRQ GPIO solo registra el último flanco. */
static void hardware_button_irq(void *arg)
{
    (void)arg;
    portENTER_CRITICAL_ISR(&hardware_button_lock);
    hardware_button_edge_us = (uint64_t)esp_timer_get_time();
    portEXIT_CRITICAL_ISR(&hardware_button_lock);
}

/* Alarma auxiliar: filtra pulsación y liberación durante 30 ms estables.
 * No ejecuta trabajos, no imprime ni cambia la calendarización. */
static bool hardware_debounce_irq(gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *event, void *arg)
{
    (void)timer; (void)event; (void)arg;
    gpio_level_t level = GPIO_LOW;
    portENTER_CRITICAL_ISR(&hardware_button_lock);
    uint64_t now = (uint64_t)esp_timer_get_time();
    if (board_gpio_read(HARDWARE_BUTTON_PIN, &level) == 0 &&
        now - hardware_button_edge_us >= 30000u &&
        (level == GPIO_HIGH) != hardware_button_stable_high) {
        hardware_button_stable_high = level == GPIO_HIGH;
        if (hardware_button_stable_high && hardware_button_events < UINT32_MAX)
            ++hardware_button_events;
    }
    portEXIT_CRITICAL_ISR(&hardware_button_lock);
    /* Despertar el bucle desde la alarma ya existente permite dormir entre
     * polls. FreeRTOS no selecciona los trabajos de RMS/EDF. */
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(hardware_main_task, &woken);
    return woken == pdTRUE;
}

/**
 * @brief Inicializa el hardware para detectar pulsaciones del botón.
 *
 * Configura los pines GPIO, registra la interrupción del botón y prepara
 * el temporizador utilizado para el antirrebote. También guarda el handle
 * de la tarea principal para la comunicación con las interrupciones.
 *
 * @return true si todos los componentes se inicializan correctamente;
 *         false si ocurre un error durante la configuración.
 */
static bool hardware_init(void)
{
    hardware_main_task = xTaskGetCurrentTaskHandle();
    if (board_gpio_init(HARDWARE_MISS_PIN, GPIO_OUTPUT) != 0 ||
        board_gpio_write(HARDWARE_MISS_PIN, GPIO_LOW) != 0 ||
        board_gpio_init(HARDWARE_BUTTON_PIN, GPIO_INPUT) != 0)
        return false;
    hardware_button_edge_us = (uint64_t)esp_timer_get_time();
    if (gpio_set_intr_type(HARDWARE_BUTTON_PIN, GPIO_INTR_ANYEDGE) != ESP_OK ||
        gpio_install_isr_service(0) != ESP_OK ||
        gpio_isr_handler_add(HARDWARE_BUTTON_PIN, hardware_button_irq, NULL) != ESP_OK)
        return false;
    gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000u
    };
    gptimer_event_callbacks_t callbacks = {.on_alarm = hardware_debounce_irq};
    gptimer_alarm_config_t alarm = {
        .alarm_count = 1000u, .flags.auto_reload_on_alarm = true
    };
    return gptimer_new_timer(&config, &hardware_debounce_timer) == ESP_OK &&
        gptimer_register_event_callbacks(hardware_debounce_timer, &callbacks, NULL) == ESP_OK &&
        gptimer_set_alarm_action(hardware_debounce_timer, &alarm) == ESP_OK &&
        gptimer_enable(hardware_debounce_timer) == ESP_OK &&
        gptimer_start(hardware_debounce_timer) == ESP_OK;
}

/**
 * @brief Obtiene y limpia la cantidad de eventos pendientes del botón.
 *
 * Accede al contador de eventos dentro de una sección crítica para evitar
 * conflictos con las interrupciones que registran nuevas pulsaciones.
 *
 * @return Cantidad de eventos del botón pendientes de procesar.
 */
static uint32_t hardware_take_button_events(void)
{
    portENTER_CRITICAL(&hardware_button_lock);
    uint32_t events = hardware_button_events;
    hardware_button_events = 0;
    portEXIT_CRITICAL(&hardware_button_lock);
    return events;
}

/**
 * @brief Procesa los eventos pendientes del botón y activa la tarea esporádica.
 *
 * Consume los eventos registrados por las interrupciones y verifica que el
 * scheduler esté preparado y en ejecución. Si existe una tarea esporádica
 * válida, intenta activarla una vez por cada evento pendiente.
 */
static void hardware_button_poll(void)
{
    uint32_t events = hardware_take_button_events();
    if (!scheduler_runtime.running || !scheduler_runtime.prepared)
        return;
    Scheduler *scheduler = &scheduler_runtime.scheduler;
    uint32_t index = scheduler->sporadic_task_index;
    if (index == UINT32_MAX || index >= scheduler->task_count)
        return;
    while (events-- > 0u) {
        RejectReason reason;
        (void)scheduler_runtime_trigger_sporadic(&scheduler_runtime,
            scheduler->tasks[index].id, &reason);
    }
}

/**
 * @brief Serializa una respuesta correlacionada con una solicitud recibida.
 *
 * Construye un ACK, NACK o CALIBRATE_RESPONSE para este nodo, conservando el
 * request_id del mensaje original. La respuesta queda en formato JSON dentro
 * del buffer proporcionado.
 *
 * @param request           Solicitud que se está respondiendo.
 * @param type              Tipo de respuesta que se generará.
 * @param reason            Motivo de rechazo si la respuesta es NACK.
 * @param ut_per_tick       Capacidad calibrada para CALIBRATE_RESPONSE.
 * @param response          Buffer que recibirá el JSON serializado.
 * @param response_capacity Capacidad total del buffer.
 * @param response_length   Dirección donde se escribirá la longitud JSON.
 *
 * @return true si la respuesta se serializa; false si no cabe o no es válida.
 */
static bool serialize_reply(
    const Message *request,
    MessageType type,
    RejectReason reason,
    uint32_t ut_per_tick,
    char *response,
    size_t response_capacity,
    uint32_t *response_length
)
{
    Message reply = {
        .type = type,
        .node_id = NODE_ID,
        .request_id = request->request_id
    };
    if (type == MSG_ACK) {
        reply.data.reply.type = request->type;
    } else {
        reply.data.reply.type = request->type;
        reply.data.reply.reason = reason;
    }
    if (type == MSG_CALIBRATE_RESPONSE)
        reply.data.ut_per_tick = ut_per_tick;

    size_t json_length = 0u;
    if (message_serialize(&reply, response, response_capacity,
                          &json_length) != MESSAGE_SERIALIZE_OK) {
        return false;
    }
    *response_length = (uint32_t)json_length;
    return true;
}

/**
 * @brief Mide el número promedio de unidades de trabajo por tick.
 *
 * Ejecuta la calibración solicitada usando la duración de tick recibida en
 * CONFIG y una reserva del 20 por ciento. El resultado es el promedio entero
 * de las muestras.
 *
 * @param sample_ticks Cantidad de muestras solicitadas.
 * @param ut_per_tick  Dirección donde se guardará el promedio medido.
 *
 * @return true si todas las muestras y el promedio son válidos; false en caso
 *         contrario.
 */
static bool calibrate(
    uint32_t sample_ticks,
    uint32_t *ut_per_tick
)
{
    if (configured_tick_ms == 0u ||
        configured_tick_ms > UINT32_MAX / 1000u ||
        sample_ticks == 0u || sample_ticks > CALIBRATION_MAX_SAMPLES) {
        return false;
    }

    uint64_t total_units = 0u;
    for (uint32_t sample = 0u; sample < sample_ticks; ++sample) {
        CalibrationResult result;
        if (!calibrator_run(configured_tick_ms * 1000u, 20u, &result))
            return false;
        total_units += result.units_per_tick;
    }

    uint64_t average = total_units / sample_ticks;
    if (average == 0u || average > UINT32_MAX)
        return false;
    *ut_per_tick = (uint32_t)average;
    return true;
}

/**
 * @brief Procesa una trama recibida y prepara su respuesta.
 *
 * Interpreta CONFIG, CALIBRATE_REQUEST, DEPLOY_TASK, START, STOP y
 * TRIGGER_SPORADIC. Comprueba que el mensaje esté dirigido a este nodo,
 * actualiza su configuración o estado y produce el ACK/NACK correspondiente.
 * También conserva la compatibilidad con los mensajes de prueba PING/MARCO.
 *
 * @param received          Trama de protocolo recibida por el ESP32.
 * @param response          Buffer donde se almacenará la respuesta.
 * @param response_capacity Capacidad del buffer de respuesta.
 * @param response_length   Dirección donde se guardará la longitud generada.
 *
 * @return true si se preparó una respuesta válida; false si la trama no puede
 *         procesarse o la respuesta no cabe en el buffer.
 */
static bool prepare_response(
    const protocol_frame_t *received,
    char *response,
    size_t response_capacity,
    uint32_t *response_length
)
{
    char request[MESSAGE_LEN];
    size_t request_length = received->length;

    if (request_length > PROTOCOL_PAYLOAD_BYTES)
        return false;
    /* El transporte histórico incluye NUL; el parser recibe la longitud JSON exacta. */
    memcpy(request, received->payload, request_length);
    request[request_length] = '\0';
    if (request_length > 0u && request[request_length - 1u] == '\0')
        --request_length;

    Message incoming;
    if (message_parse(request, request_length, &incoming) == MESSAGE_PARSE_OK) {
        if (incoming.node_id != NODE_ID)
            return false;

        if (incoming.type == MSG_CONFIG) {
            if (scheduler_runtime.prepared) {
                return serialize_reply(&incoming, MSG_NACK,
                    REJECT_INVALID_STATE, 0u, response, response_capacity,
                    response_length);
            }
            configured_tick_ms = incoming.data.config.tick_ms;
            report_every_ticks = incoming.data.config.report_every_ticks;
            configured_algorithm = incoming.data.config.algorithm;
            return serialize_reply(&incoming, MSG_ACK, REJECT_NONE, 0u,
                response, response_capacity, response_length);
        }

        if (incoming.type == MSG_CALIBRATE_REQUEST) {
            if (scheduler_runtime.prepared) {
                return serialize_reply(&incoming, MSG_NACK,
                    REJECT_INVALID_STATE, 0u, response, response_capacity,
                    response_length);
            }
            uint32_t ut_per_tick = 0u;
            if (calibrate(incoming.data.sample_ticks, &ut_per_tick)) {
                configured_ut_per_tick = ut_per_tick;
                return serialize_reply(&incoming, MSG_CALIBRATE_RESPONSE,
                    REJECT_NONE, ut_per_tick, response, response_capacity,
                    response_length);
            }
            return serialize_reply(&incoming, MSG_NACK,
                REJECT_CALIBRATION_FAILED, 0u, response, response_capacity,
                response_length);
        }

        if (incoming.type == MSG_DEPLOY_TASK) {
            if (scheduler_runtime.prepared) {
                return serialize_reply(&incoming, MSG_NACK,
                    REJECT_INVALID_STATE, 0u, response, response_capacity,
                    response_length);
            }
            /*
             * El Host ya hizo la prueba de admisión. Este paso valida límites
             * locales, guarda la tarea y confirma el almacenamiento con ACK.
             */
            RejectReason reason = task_store_add(
                &deployed_tasks,
                &incoming.data.deploy_task.task,
                incoming.data.deploy_task.cost_ticks
            );
            MessageType reply_type = reason == REJECT_NONE ? MSG_ACK : MSG_NACK;
            return serialize_reply(&incoming, reply_type, reason, 0u,
                response, response_capacity, response_length);
        }

        if (incoming.type == MSG_START) {
            bool was_running = scheduler_runtime.running;
            RejectReason reason = REJECT_INVALID_STATE;
            SchedulerType scheduler_type =
                configured_algorithm == MESSAGE_SCHED_EDF ? EDF : RMS;
            if (!scheduler_runtime_start(&scheduler_runtime, &deployed_tasks,
                                         configured_ut_per_tick,
                                         scheduler_type, &reason)) {
                return serialize_reply(&incoming, MSG_NACK, reason, 0u,
                    response, response_capacity, response_length);
            }
            scheduler_runtime.scheduler.on_selection = hardware_show_selection;
            scheduler_runtime.scheduler.on_task_released = report_task_released;
            scheduler_runtime.scheduler.on_task_completed = report_task_completed;
            scheduler_runtime.scheduler.on_deadline_miss = report_deadline_miss;
            if (!was_running) {
                hardware_last_misses = scheduler_runtime.scheduler.deadline_miss_count;
                hardware_clear_button();
                previous_selected_task = UINT32_MAX;
            }
            if (!was_running)
                next_tick_us = (uint64_t)esp_timer_get_time() +
                    (uint64_t)configured_tick_ms * 1000u;
            return serialize_reply(&incoming, MSG_ACK, REJECT_NONE, 0u,
                response, response_capacity, response_length);
        }

        if (incoming.type == MSG_STOP) {
            scheduler_runtime_stop(&scheduler_runtime);
            (void)flush_pending_slice();
            hardware_show_task(UINT32_MAX);
            board_gpio_write(HARDWARE_MISS_PIN, GPIO_LOW);
            hardware_clear_button();
            return serialize_reply(&incoming, MSG_ACK, REJECT_NONE, 0u,
                response, response_capacity, response_length);
        }

        if (incoming.type == MSG_TRIGGER_SPORADIC) {
            RejectReason reason = REJECT_INVALID_STATE;
            if (!scheduler_runtime_trigger_sporadic(
                    &scheduler_runtime, incoming.data.task_id, &reason)) {
                return serialize_reply(&incoming, MSG_NACK, reason, 0u,
                    response, response_capacity, response_length);
            }
            return serialize_reply(&incoming, MSG_ACK, REJECT_NONE, 0u,
                response, response_capacity, response_length);
        }

        return false;
    }

    if (request_length == 4u && memcmp(request, "PING", 4u) == 0) {
        Event event;
        if (dequeue_node_report(&event)) {
            Message report = {.type = MSG_EVENT, .node_id = NODE_ID,
                .request_id = 0u, .data.event = event};
            size_t json_length = 0u;
            if (message_serialize(&report, response, response_capacity,
                                  &json_length) != MESSAGE_SERIALIZE_OK)
                return false;
            *response_length = (uint32_t)json_length;
            return true;
        }
        const char *legacy = "PONG from ESP32";
        size_t legacy_length = strlen(legacy) + 1u;
        if (legacy_length > response_capacity)
            return false;
        memcpy(response, legacy, legacy_length);
        *response_length = (uint32_t)legacy_length;
        return true;
    }

    const char *legacy = "ESP32 didn't understand message";
    if (request_length == 5u && memcmp(request, "MARCO", 5u) == 0 &&
             received->relevance > 0u)
        legacy = "POLO from ESP32";

    size_t legacy_length = strlen(legacy) + 1u;
    if (legacy_length > response_capacity)
        return false;
    memcpy(response, legacy, legacy_length);
    *response_length = (uint32_t)legacy_length;
    return true;
}

/**
 * @brief Ejecuta un tick cuando el scheduler está activo y vence su intervalo.
 *
 * Ejecuta como máximo un tick por llamada. Si el scheduler informa un error,
 * enciende LED0 y detiene la ejecución. Después del tick, calcula el siguiente
 * vencimiento desde el tiempo actual para no ejecutar ticks atrasados en
 * ráfaga y permitir que continúe el polling serial.
 */
static void run_scheduler_if_due(void)
{
    hardware_button_poll();
    if (!scheduler_runtime.running || configured_tick_ms == 0u)
        return;
    if (report_count > NODE_REPORT_QUEUE_CAPACITY - MAX_JOBS -
                       TASK_MAX_PER_NODE - 4u)
        return;

    uint64_t now_us = (uint64_t)esp_timer_get_time();
    if (now_us < next_tick_us)
        return;

    uint32_t selected_task_id;
    if (!scheduler_runtime_run_tick(&scheduler_runtime, &selected_task_id)) {
        hardware_show_task(15u);
        scheduler_runtime_stop(&scheduler_runtime);
        return;
    }

    Scheduler *scheduler = &scheduler_runtime.scheduler;
    uint64_t tick = scheduler->current_tick - 1u;
    uint32_t report_task_id = selected_task_id == UINT32_MAX ? 0u : selected_task_id;
    if (selected_task_id != previous_selected_task &&
        previous_selected_task != UINT32_MAX &&
        selected_task_id != UINT32_MAX) {
        Event change = {.type = EVENT_TASK_SWITCH, .source = EVENT_SOURCE_NODE,
            .node_id = NODE_ID, .task_id = selected_task_id,
            .job_id = scheduler->last_selected_job_id, .tick = tick};
        (void)enqueue_node_report(&change);
    }
    if (pending_slice_valid &&
        (pending_slice.type != (selected_task_id == UINT32_MAX
            ? EVENT_IDLE_SLICE : EVENT_EXECUTION_SLICE) ||
        pending_slice.task_id != report_task_id ||
         pending_slice.job_id != scheduler_runtime.scheduler.last_selected_job_id ||
         pending_slice.tick + pending_slice.duration_ticks != tick)) {
        (void)flush_pending_slice();
    }
    if (!pending_slice_valid) {
        pending_slice = (Event){
            .type = selected_task_id == UINT32_MAX
                ? EVENT_IDLE_SLICE : EVENT_EXECUTION_SLICE,
            .source = EVENT_SOURCE_NODE,
            .node_id = NODE_ID,
            .task_id = report_task_id,
            .job_id = selected_task_id == UINT32_MAX
                ? 0u : scheduler->last_selected_job_id,
            .tick = tick,
            .duration_ticks = 1u
        };
        pending_slice_valid = true;
    } else if (pending_slice.duration_ticks < UINT32_MAX) {
        ++pending_slice.duration_ticks;
    }
    previous_selected_task = selected_task_id;
    if (pending_slice.duration_ticks >= report_every_ticks)
        (void)flush_pending_slice();

    /* Evita ejecutar ticks atrasados en ráfaga y deja tiempo al polling serial. */
    next_tick_us = (uint64_t)esp_timer_get_time() +
        (uint64_t)configured_tick_ms * 1000u;
}

/**
 * @brief Inicializa el ESP32 y mantiene activo su ciclo de comunicación.
 *
 * Prepara el almacenamiento de tareas, el runtime del scheduler, UART,
 * protocolo y GPIO. El bucle principal alterna entre polling de recepción y
 * transmisión de respuestas; en cada iteración también comprueba si debe
 * ejecutarse un tick, sin bloquear la recepción de comandos mientras corre.
 *
 * Si falla la inicialización, indica el error con LED0 y no procesa mensajes.
 */
void app_main(void){
    bool setup_fail = false;
    /* No se conserva estado de tareas entre reinicios del ESP32. */
    task_store_init(&deployed_tasks);
    scheduler_runtime_init(&scheduler_runtime);
    // << Setup ESP32 serial interface >>
    esp32_uart_t uart;
    esp32_uart_configs_t uart_conf = {
        .port_num = UART_PORT,
        .baudrate = SERIAL_BAUDRATE,
        .rx_buffer_size = 1024,
        .tx_buffer_size = 0,
        .tick_ms = 0u
    };

    serial_errno_t conf_errno = serial_configure(
        &uart, &uart_conf
    );
    if (!setup_fail) setup_fail = (conf_errno != SERIAL_OK);

    // << Setup the protocol interface >>
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
    // << Setup GPIO >>
    board_gpio_init(LED0, GPIO_OUTPUT);
    board_gpio_init(LED1, GPIO_OUTPUT);
    board_gpio_init(LED2, GPIO_OUTPUT);
    board_gpio_init(LED3, GPIO_OUTPUT);
    
    board_gpio_write(LED0, GPIO_LOW);
    board_gpio_write(LED1, GPIO_LOW);
    board_gpio_write(LED2, GPIO_LOW);
    board_gpio_write(LED3, GPIO_LOW);
    if (!hardware_init()) setup_fail = true;
    // << Setup basic messages >>
    char response[MESSAGE_LEN];
    // << Procedure loop >>
    int state = 0;

    if (setup_fail) hardware_show_task(15u);
    while (1) {
        if (setup_fail) {
            /* Los LEDs ya indican el fallo; no ocupar CPU girando aquí. */
            vTaskSuspend(NULL);
            continue;
        }
        // [RX state]
        if (state == 0){
            // [Poll RX state machine]
            protocol_errno_t rx_errno = protocol_rx_poll(
                &uart,
                &rx,
                &rx_packet
            );
            // [Check RX]
            if (rx.complete){
                uint32_t response_length = 0u;
                tx_frame.relevance = 0;
                if (rx.valid){
                    protocol_errno_t rx_frame_errno = protocol_set_buffer_to_frame(
                        &rx_frame,
                        rx.buffer,
                        sizeof(rx.buffer)
                    );
                    if (rx_frame_errno == PROTOCOL_OK)
                        (void)prepare_response(&rx_frame, response,
                            sizeof(response), &response_length);
                }
                if (response_length == 0u) {
                    /* Mantiene una respuesta legible para mensajes no soportados. */
                    const char *fallback = "ESP32 didn't understand message";
                    response_length = (uint32_t)strlen(fallback) + 1u;
                    memcpy(response, fallback, response_length);
                }

                tx_frame.relevance = 0;
                tx_frame.length = response_length;
                memcpy(tx_frame.payload, response, response_length);

                protocol_errno_t tx_frame_errno = protocol_set_frame_to_buffer(
                    &tx_frame,
                    tx.buffer,
                    sizeof(tx.buffer)
                );
                // [Move to TX]
                state = 1;
            }
        }
        // [TX state]
        if (state == 1){
            // [Poll TX state machine]
            protocol_errno_t tx_errno = protocol_tx_poll(
                &uart,
                &tx,
                &tx_packet
            );
            // [Check TX]
            if (tx.complete && tx.valid){
                // [Move to RX]
                state = 0;
            }
        }
        run_scheduler_if_due();
        /* La alarma auxiliar de 1 ms despierta este bucle. Esta espera deja
         * correr IDLE0 sin usar delay como temporización del scheduler. */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }
}