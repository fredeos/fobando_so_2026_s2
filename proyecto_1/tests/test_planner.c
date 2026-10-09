#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "host/planner.h"

/* Construye una tarea periódica de prueba */
static Task periodic(
    uint32_t id,
    uint32_t work,
    uint32_t period
)
{
    return (Task){
        .id = id,
        .type = TASK_PERIODIC,
        .work = work,
        .period = period,
        .deadline = period,
        .criticality = 2
    };
}

int main(void)
{
    // ========================================
    // TEST 1: COSTOS POR NODO
    // ========================================

    Node fast;
    Node slow;

    assert(node_init(&fast, 1u, 100u));
    assert(node_init(&slow, 2u, 40u));

    Task t = periodic(1u, 250u, 20u);

    assert(calculate_cost(&t, &fast) == 3u);
    assert(calculate_cost(&t, &slow) == 7u);

    // ========================================
    // TEST 2: COTA RMS
    // ========================================

    assert(
        fabs(rms_bound(2u) - 0.828427124746)
        < 1e-9
    );

    assert(
        admission_check(&fast, &t, SCHED_EDF)
        .accepted
    );

    // ========================================
    // TEST 3: FIRST-FIT VS WORST-FIT
    // ========================================

    Planner ff;
    Planner wf;

    assert(planner_init(
        &ff, SCHED_EDF, PLACEMENT_FIRST_FIT
    ));

    assert(planner_init(
        &wf, SCHED_EDF, PLACEMENT_WORST_FIT
    ));

    assert(planner_add_node(&ff, 1u, 100u));
    assert(planner_add_node(&ff, 2u, 50u));

    assert(planner_add_node(&wf, 1u, 100u));
    assert(planner_add_node(&wf, 2u, 50u));

    Task tasks[] = {
        periodic(2u, 10u, 10u),
        periodic(1u, 900u, 10u)
    };

    PlannerDecision ff_r[2];
    PlannerDecision wf_r[2];

    assert(planner_plan_batch(
        &ff, tasks, 2u, ff_r
    ));

    assert(planner_plan_batch(
        &wf, tasks, 2u, wf_r
    ));

    assert(ff_r[0].accepted);
    assert(wf_r[0].accepted);

    assert(ff_r[1].node_id == 1u);
    assert(wf_r[1].node_id == 1u);

    // First-Fit utiliza el nodo rápido
    assert(ff_r[0].node_id == 1u);

    // Worst-Fit utiliza el nodo con más espacio
    assert(wf_r[0].node_id == 2u);

    // ========================================
    // TEST 4: RECHAZO RMS
    // ========================================

    Planner rms;

    assert(planner_init(
        &rms, SCHED_RMS, PLACEMENT_FIRST_FIT
    ));

    assert(planner_add_node(&rms, 1u, 10u));

    Task a = periodic(1u, 50u, 10u);
    Task b = periodic(2u, 40u, 10u);

    assert(planner_assign_task(
        &rms, &a
    ).accepted);

    assert(!planner_assign_task(
        &rms, &b
    ).accepted);

    // ========================================
    // TEST 5: TAREAS DUPLICADAS
    // ========================================

    assert(!planner_assign_task(
        &rms, &a
    ).accepted);

    // ========================================
    // TEST 6: ENTRADAS INVÁLIDAS
    // ========================================

    Task invalid = periodic(3u, 5u, 0u);

    assert(
        planner_assign_task(&rms, &invalid).reason
        == REJECT_INVALID_TASK
    );

    // ========================================
    // TEST 7: LÍMITE ESPORÁDICO
    // ========================================

    Planner sporadic;

    assert(planner_init(
        &sporadic,
        SCHED_EDF,
        PLACEMENT_FIRST_FIT
    ));

    assert(planner_add_node(
        &sporadic, 1u, 100u
    ));

    Task s1 = periodic(7u, 10u, 10u);
    Task s2 = periodic(8u, 10u, 10u);

    s1.type = TASK_SPORADIC;
    s2.type = TASK_SPORADIC;

    assert(planner_assign_task(
        &sporadic, &s1
    ).accepted);

    assert(
        planner_assign_task(&sporadic, &s2).reason
        == REJECT_SPORADIC_LIMIT
    );

    printf("OK: planner tests\n");

    return 0;
}