#ifndef ESP32_LOCAL_TEST_H
#define ESP32_LOCAL_TEST_H

#include <stdint.h>
#include <stdbool.h>
#include "scheduler.h"

#define LOCAL_BUTTON_DEBOUNCE_US 30000u

/* El pulldown externo deja el botón en 0; una pulsación lo lleva a 3V3. */
typedef struct {
    bool stable_high;
} LocalButton;

/* edge_us es la hora del último cambio, registrada por la IRQ en ambos flancos.
 * Solo se acepta una pulsación después de 30 ms estables; una pulsación larga
 * no se repite. También se filtra la liberación antes de rearmar el botón. */
static inline bool local_button_update(LocalButton *button, bool high,
                                       uint64_t edge_us, uint64_t now_us)
{
    if (now_us < edge_us || now_us - edge_us < LOCAL_BUTTON_DEBOUNCE_US ||
        high == button->stable_high) return false;
    button->stable_high = high;
    return high;
}

/* Cuatro tareas periódicas y una esporádica: W se calcula con el sn medido.
 * Utilización periódica = 2/7 + 1/8 + 3/10 + 3/14 = 0.925.
 * Incluyendo S (1/20) = 0.975. EDF cumple; RMS pierde algunos plazos.
 * Se omite deliberadamente la admisión del host para comparar algoritmos.
 */
static inline bool local_tasks_init(Scheduler *sc, uint32_t sn, SchedulerType type)
{
    if (sn == 0 || sn > UINT32_MAX / 3u) return false;
    SchedulerTask tasks[] = {
        {.id=1, .type='P', .W=2u*sn, .T=7,  .D=7,  .criticality=2, .C=2},
        {.id=2, .type='P', .W=sn,    .T=8,  .D=8,  .criticality=2, .C=1},
        {.id=3, .type='P', .W=3u*sn, .T=10, .D=10, .criticality=2, .C=3},
        {.id=4, .type='P', .W=3u*sn, .T=14, .D=14, .criticality=2, .C=3},
        {.id=5, .type='S', .W=sn,    .T=20, .D=20, .criticality=3, .C=1}
    };
    init(sc, sn, type);
    for (unsigned i=0; i < sizeof(tasks)/sizeof(tasks[0]); ++i) {
        if (!add_task(&tasks[i], sc)) return false;
    }
    return true;
}
#endif
