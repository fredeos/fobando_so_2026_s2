/* Prueba autónoma de RMS/EDF: no requiere host ni protocolo serial. */
#include <inttypes.h>
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/sem.h"
#include "hardware/sync.h"
#include "local_test.h"
#include "calibrator.h"
#include "gpio/gpio.h"

#ifndef LOCAL_TICK_MS
#define LOCAL_TICK_MS 10u
#endif
#ifndef LOCAL_USE_EDF
#define LOCAL_USE_EDF 0
#endif

#ifndef LOCAL_MISS_LED_PIN
#define LOCAL_MISS_LED_PIN 25
#endif

#define BUTTON_PIN 18u
/* GP10 es el bit menos significativo; 0000=idle, 0001=tarea 1. */
static const uint led_pins[] = {10u, 11u, 12u, 13u};
static semaphore_t tick_sem;
static volatile uint32_t pending_ticks;
static volatile uint64_t button_edge_us;
static volatile uint32_t pending_presses;
static volatile bool button_error;
static alarm_id_t debounce_alarm_id;
static LocalButton button; /* Solo la rutina de antirrebote modifica su estado. */
static Scheduler scheduler;

static void display_task(uint32_t id)
{
    uint32_t code = id == UINT32_MAX ? 0u : id;
    for (unsigned i=0; i<4; ++i)
        board_gpio_write(led_pins[i], ((code >> i) & 1u) ? GPIO_HIGH : GPIO_LOW);
}

/* El calendarizador llama este observador después de comprobar plazos y antes
 * de ejecutar las UT: GP25 refleja el tick actual, sin borrar el historial. */
static void display_scheduled_task(uint32_t id)
{
    static uint32_t previous_misses;
    display_task(id);
#if LOCAL_MISS_LED_PIN >= 0
    board_gpio_write(LOCAL_MISS_LED_PIN,
        scheduler.deadline_miss_count != previous_misses ? GPIO_HIGH : GPIO_LOW);
#endif
    previous_misses = scheduler.deadline_miss_count;
}

/* El antirrebote tiene su propia alarma: no depende del tick de 1 segundo.
 * Las IRQ registran pulsaciones; solo el bucle principal libera trabajos. */
static int64_t debounce_irq(alarm_id_t id, void *user_data)
{
    (void)id;
    (void)user_data;
    debounce_alarm_id = 0;
    gpio_level_t level = GPIO_LOW;
    if (board_gpio_read(BUTTON_PIN, &level) != 0) {
        button_error = true;
        return 0;
    }
    if (local_button_update(&button, level == GPIO_HIGH,
                            button_edge_us, time_us_64())) {
        if (pending_presses != UINT32_MAX) ++pending_presses;
        else button_error = true;
    }
    return 0;
}

static void button_irq(uint gpio, uint32_t events)
{
    (void)events;
    if (gpio != BUTTON_PIN) return;
    button_edge_us = time_us_64();
    if (debounce_alarm_id > 0) cancel_alarm(debounce_alarm_id);
    debounce_alarm_id = add_alarm_in_us(LOCAL_BUTTON_DEBOUNCE_US,
                                        debounce_irq, NULL, true);
    if (debounce_alarm_id <= 0) button_error = true;
}

static bool tick_irq(repeating_timer_t *timer)
{
    (void)timer;
    if (pending_ticks != UINT32_MAX) ++pending_ticks;
    sem_release(&tick_sem);
    return true;
}

static void wait_forever(void)
{
    /* El procesador duerme entre interrupciones; no hay espera ocupada. */
    while (true) __wfi();
}

int main(void)
{
    stdio_init_all(); /* La ejecución no espera a que se conecte una terminal. */
    for (unsigned i=0; i<4; ++i) {
        board_gpio_init(led_pins[i], GPIO_OUTPUT);
        board_gpio_write(led_pins[i], GPIO_LOW);
    }

    board_gpio_init(LOCAL_MISS_LED_PIN, GPIO_OUTPUT);
    board_gpio_write(LOCAL_MISS_LED_PIN, GPIO_LOW);

    board_gpio_init(BUTTON_PIN, GPIO_INPUT); /* Pulldown externo configurado en gpio.c. */

    CalibrationResult calibration;
    if (!calibrator_run(LOCAL_TICK_MS * 1000u, 30u, &calibration) ||
        !local_tasks_init(&scheduler, calibration.units_per_tick,
                          LOCAL_USE_EDF ? EDF : RMS)) {
        display_task(15u);
        puts("ERROR de calibracion o carga local (LEDs=1111)");
        wait_forever();
    }
    printf("LOCAL %s tick=%u ms sn=%" PRIu32 " UT/tick; boton GP18 activo alto\n",
           LOCAL_USE_EDF ? "EDF" : "RMS", (unsigned)LOCAL_TICK_MS, scheduler.sn);

    scheduler.on_selection = display_scheduled_task;
    button_edge_us = time_us_64();
    /* La interfaz compartida solo ofrece init/read/write; la IRQ usa el SDK. */
    gpio_set_irq_enabled_with_callback(BUTTON_PIN,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, button_irq);
    sem_init(&tick_sem, 0, 1);
    repeating_timer_t timer;
    /* Periodo negativo: las alarmas se programan entre inicios de ticks. */
    if (!add_repeating_timer_us(-((int64_t)LOCAL_TICK_MS * 1000),
                                tick_irq, NULL, &timer)) {
        display_task(15u);
        puts("ERROR creando temporizador (LEDs=1111)");
        wait_forever();
    }

    while (true) {
        sem_acquire_blocking(&tick_sem);
        uint32_t irq_state = save_and_disable_interrupts();
        uint32_t due = pending_ticks;
        pending_ticks = 0;
        uint32_t presses = pending_presses;
        pending_presses = 0;
        bool input_error = button_error;
        restore_interrupts(irq_state);

        if (input_error) {
            cancel_repeating_timer(&timer);
            display_task(15u);
            puts("ERROR leyendo boton GP18 (1111)");
            wait_forever();
        }
        if (due > 1u) {
            /* No comprimir ticks atrasados: invalidaría la prueba temporal. */
            cancel_repeating_timer(&timer);
            display_task(15u);
            puts("OVERRUN: mas de un tick pendiente; prueba detenida (1111)");
            wait_forever();
        }
        if (due == 0u) continue;
        while (presses-- > 0u) {
            bool accepted = scheduler_register_sporadic_event(&scheduler);
            printf("boton tick=%" PRIu32 " %s; ignorados=%" PRIu32 "\n",
                   scheduler.current_tick, accepted ? "aceptado" : "ignorado por T minimo",
                   scheduler.sporadic_rejected_count);
        }
        uint32_t misses = scheduler.deadline_miss_count;
        uint32_t selected;
        if (!scheduler_run_tick(&scheduler, &selected)) {
            cancel_repeating_timer(&timer);
            display_task(15u);
            puts("ERROR del calendarizador (1111)");
            wait_forever();
        }
        /* El observador actualiza LEDs antes de ejecutar las UT de cada tick. */
        if (scheduler.deadline_miss_count != misses) {
            printf("MISS tick=%" PRIu32 " tarea=%" PRIu32 " total=%" PRIu32 "\n",
                   scheduler.current_tick-1u, scheduler.last_missed_task_id,
                   scheduler.deadline_miss_count);
        }
        /* USB es diagnóstico: timeout=0 en CMake, nunca esperar al host.
         * Puede perder texto si la terminal no consume; no es reporte final. */
        if (scheduler.current_tick <= 20u || scheduler.current_tick % 10u == 0u) {
            printf("tick=%" PRIu32 " tarea=%" PRIu32 " misses=%" PRIu32
                   " S_completadas=%" PRIu32 " S_ignoradas=%" PRIu32 "\n",
                   scheduler.current_tick-1u, selected == UINT32_MAX ? 0u : selected,
                   scheduler.deadline_miss_count, scheduler.sporadic_completed_count,
                   scheduler.sporadic_rejected_count);
        }
    }
}