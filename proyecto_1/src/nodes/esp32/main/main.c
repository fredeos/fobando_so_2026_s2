/* Prueba local: cuatro tareas periódicas y una esporádica, sin host. */
#include <inttypes.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "gpio/gpio.h"
#include "calibrator.h"
#include "local_test.h"

#ifndef LOCAL_TICK_MS
#define LOCAL_TICK_MS 1000u
#endif
#ifndef LOCAL_USE_EDF
#define LOCAL_USE_EDF 0
#endif

#define BUTTON_PIN 27u
#define MISS_LED_PIN 15u
static const uint32_t task_leds[] = {18u, 19u, 21u, 22u};
static const char *TAG = "local_test";
static Scheduler scheduler;
static TaskHandle_t scheduler_task;
static portMUX_TYPE input_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint64_t button_edge_us;
static volatile uint32_t pending_presses;
static volatile bool input_error;
static LocalButton button;

static void display_task(uint32_t id)
{
    uint32_t code = id == UINT32_MAX ? 0u : id;
    for (unsigned i = 0; i < 4; ++i)
        board_gpio_write(task_leds[i], (code & (1u << i)) ? GPIO_HIGH : GPIO_LOW);
}

/* Llamado por el calendarizador antes de ejecutar las UT del tick. */
static void display_selection(uint32_t id)
{
    static uint32_t previous_misses;
    display_task(id);
    board_gpio_write(MISS_LED_PIN,
        scheduler.deadline_miss_count != previous_misses ? GPIO_HIGH : GPIO_LOW);
    previous_misses = scheduler.deadline_miss_count;
}

static void stop_test(const char *reason)
{
    display_task(15u);
    ESP_LOGE(TAG, "%s; prueba detenida (LEDs=1111)", reason);
    /* Espera bloqueada; el RTOS no selecciona ni ejecuta nuestros trabajos. */
    for (;;) vTaskSuspend(NULL);
}

static void check(esp_err_t result, const char *operation)
{
    if (result != ESP_OK) {
        ESP_LOGE(TAG, "%s: %s", operation, esp_err_to_name(result));
        stop_test("Error de inicializacion");
    }
}

/* IRQ GPIO: solo registra el último flanco para aplicar antirrebote. */
static void button_irq(void *arg)
{
    (void)arg;
    portENTER_CRITICAL_ISR(&input_lock);
    button_edge_us = (uint64_t)esp_timer_get_time();
    portEXIT_CRITICAL_ISR(&input_lock);
}

/* Alarma auxiliar de hardware de 1 ms, independiente del tick de tareas.
 * Detecta 30 ms estables y conserva pulsaciones que suceden entre dos ticks. */
static bool debounce_irq(gptimer_handle_t timer,
                         const gptimer_alarm_event_data_t *event, void *arg)
{
    (void)timer;
    (void)event;
    (void)arg;
    gpio_level_t level = GPIO_LOW;
    portENTER_CRITICAL_ISR(&input_lock);
    if (board_gpio_read(BUTTON_PIN, &level) != 0) {
        input_error = true;
    } else if (local_button_update(&button, level == GPIO_HIGH, button_edge_us,
                                   (uint64_t)esp_timer_get_time())) {
        if (pending_presses != UINT32_MAX) ++pending_presses;
        else input_error = true;
    }
    portEXIT_CRITICAL_ISR(&input_lock);
    return false;
}

/* El temporizador genera los ticks; el RTOS solo despierta este único bucle. */
static bool tick_irq(gptimer_handle_t timer,
                     const gptimer_alarm_event_data_t *event, void *arg)
{
    (void)timer;
    (void)event;
    (void)arg;
    BaseType_t woken = pdFALSE;
    vTaskNotifyGiveFromISR(scheduler_task, &woken);
    return woken == pdTRUE;
}

static gptimer_handle_t create_timer(uint64_t period_us,
    bool (*callback)(gptimer_handle_t, const gptimer_alarm_event_data_t *, void *))
{
    gptimer_handle_t timer = NULL;
    gptimer_config_t config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000u
    };
    check(gptimer_new_timer(&config, &timer), "crear temporizador");
    gptimer_event_callbacks_t callbacks = {.on_alarm = callback};
    check(gptimer_register_event_callbacks(timer, &callbacks, NULL), "registrar IRQ");
    gptimer_alarm_config_t alarm = {
        .alarm_count = period_us,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true
    };
    check(gptimer_set_alarm_action(timer, &alarm), "configurar alarma");
    check(gptimer_enable(timer), "habilitar temporizador");
    return timer;
}

void app_main(void)
{
    for (unsigned i = 0; i < 4; ++i) {
        check(board_gpio_init(task_leds[i], GPIO_OUTPUT), "configurar LED de tarea");
        check(board_gpio_write(task_leds[i], GPIO_LOW), "apagar LED de tarea");
    }
    check(board_gpio_init(MISS_LED_PIN, GPIO_OUTPUT), "configurar LED de plazo");
    check(board_gpio_write(MISS_LED_PIN, GPIO_LOW), "apagar LED de plazo");
    check(board_gpio_init(BUTTON_PIN, GPIO_INPUT), "configurar boton pull-down externo");

    CalibrationResult calibration;
    if (!calibrator_run(LOCAL_TICK_MS * 1000u, 30u, &calibration) ||
        !local_tasks_init(&scheduler, calibration.units_per_tick,
                          LOCAL_USE_EDF ? EDF : RMS))
        stop_test("No se pudo calibrar o cargar las cinco tareas");
    scheduler.on_selection = display_selection;
    scheduler_task = xTaskGetCurrentTaskHandle();
    ESP_LOGI(TAG, "%s: tick=%u ms; sn=%" PRIu32 " UT/tick; 5 tareas",
             LOCAL_USE_EDF ? "EDF" : "RMS", (unsigned)LOCAL_TICK_MS, scheduler.sn);

    /* La interfaz board_gpio ofrece init/read/write; las IRQ usan el SDK. */
    button_edge_us = (uint64_t)esp_timer_get_time();
    check(gpio_set_intr_type(BUTTON_PIN, GPIO_INTR_ANYEDGE), "configurar flancos");
    check(gpio_install_isr_service(0), "instalar IRQ GPIO");
    check(gpio_isr_handler_add(BUTTON_PIN, button_irq, NULL), "registrar boton");
    gptimer_handle_t debounce_timer = create_timer(1000u, debounce_irq);
    gptimer_handle_t tick_timer = create_timer((uint64_t)LOCAL_TICK_MS * 1000u, tick_irq);
    check(gptimer_start(debounce_timer), "iniciar antirrebote");
    check(gptimer_start(tick_timer), "iniciar ticks");

    for (;;) {
        uint32_t due = ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        portENTER_CRITICAL(&input_lock);
        uint32_t presses = pending_presses;
        pending_presses = 0;
        bool error = input_error;
        portEXIT_CRITICAL(&input_lock);
        if (due > 1u || error) {
            check(gptimer_stop(tick_timer), "detener ticks");
            check(gptimer_stop(debounce_timer), "detener antirrebote");
            stop_test(error ? "Error de entrada" : "OVERRUN: ticks acumulados");
        }
        if (due == 0u) continue;
        while (presses-- > 0u) {
            bool accepted = scheduler_register_sporadic_event(&scheduler);
            ESP_LOGI(TAG, "Boton tick=%" PRIu32 " %s; ignorados=%" PRIu32,
                scheduler.current_tick, accepted ? "aceptado" : "ignorado por T minimo",
                scheduler.sporadic_rejected_count);
        }
        uint32_t previous = scheduler.deadline_miss_count;
        uint32_t selected;
        if (!scheduler_run_tick(&scheduler, &selected)) stop_test("Error de calendarizador");
        if (scheduler.deadline_miss_count != previous)
            ESP_LOGW(TAG, "MISS tick=%" PRIu32 " tarea=%" PRIu32 " total=%" PRIu32,
                scheduler.current_tick - 1u, scheduler.last_missed_task_id,
                scheduler.deadline_miss_count);
        if (scheduler.current_tick <= 20u || scheduler.current_tick % 10u == 0u)
            ESP_LOGI(TAG, "tick=%" PRIu32 " tarea=%" PRIu32 " misses=%" PRIu32,
                scheduler.current_tick - 1u, selected == UINT32_MAX ? 0u : selected,
                scheduler.deadline_miss_count);
    }
}
