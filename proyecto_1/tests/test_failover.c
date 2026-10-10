#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "host/failover.h"

/*
 * Construye una tarea para las pruebas.
 */
static Task task(
    uint32_t id,
    TaskType type,
    uint32_t work,
    uint32_t period,
    int32_t criticality
)
{
    return (Task){
        .id = id,
        .type = type,
        .work = work,
        .period = period,
        .deadline = period,
        .criticality = criticality
    };
}

/*
 * Asigna una tarea directamente a un nodo.
 *
 * Se utiliza para preparar escenarios
 * de prueba con asignaciones preexistentes.
 */
static void seed(
    Planner *planner,
    size_t node_index,
    Task t
)
{
    Node *node = &planner->nodes[node_index];

    AdmissionResult admission = admission_check(
        node,
        &t,
        planner->algorithm
    );

    assert(admission.accepted);

    assert(
        node_add_task(
            node,
            &t,
            admission.cost_ticks
        )
    );
}

/*
 * Busca el resultado correspondiente a una tarea.
 */
static const FailoverEntry *find_entry(
    const FailoverResult *result,
    uint32_t task_id
)
{
    for (size_t i = 0; i < result->count; ++i) {

        if (result->entries[i].task.id == task_id) {

            return &result->entries[i];
        }
    }

    return NULL;
}

/*
 * PRUEBA 1
 *
 * Verifica:
 * - Reubicación de una tarea periódica.
 * - Rechazo de una esporádica.
 * - Conservación de tareas sobrevivientes.
 * - Actualización del estado del nodo fallido.
 */
static void test_reallocation(void)
{
    Planner planner;

    FailoverResult result;

    assert(planner_init(
        &planner,
        SCHED_EDF,
        PLACEMENT_FIRST_FIT
    ));

    assert(planner_add_node(
        &planner, 1u, 100u
    ));

    assert(planner_add_node(
        &planner, 2u, 50u
    ));

    /* Tareas del nodo que fallará */
    seed(
        &planner,
        0,
        task(1u, TASK_PERIODIC, 100u, 20u, 3)
    );

    seed(
        &planner,
        0,
        task(2u, TASK_SPORADIC, 20u, 50u, 5)
    );

    /* Tarea que ya ejecuta el nodo sobreviviente */
    seed(
        &planner,
        1,
        task(3u, TASK_PERIODIC, 50u, 20u, 4)
    );

    double original_utilization =
        planner.nodes[1].utilization;

    /* Simular la caída del nodo 1 */
    assert(failover_handle_node_failure(
        &planner,
        1u,
        &result
    ));

    /* Comprobar estado del nodo fallido */
    assert(result.failed_node_id == 1u);

    assert(result.count == 2u);

    assert(
        planner.nodes[0].status == NODE_FAILED
    );

    assert(
        planner.nodes[0].task_count == 0u
    );

    assert(
        planner.nodes[0].utilization == 0.0
    );

    /* El nodo sobreviviente mantiene su tarea */
    assert(planner.nodes[1].task_count == 2u);

    assert(
        planner.nodes[1].tasks[0].task.id == 3u
    );

    assert(
        planner.nodes[1].utilization >
        original_utilization
    );

    /* La tarea periódica se reubicó */
    assert(
        find_entry(&result, 1u)->decision.accepted
    );

    assert(
        find_entry(&result, 1u)->decision.node_id == 2u
    );

    /* La tarea esporádica fue rechazada */
    assert(
        !find_entry(&result, 2u)->decision.accepted
    );

    assert(
        find_entry(&result, 2u)->decision.reason ==
        REJECT_SPORADIC_NODE_FAILED
    );

    /* No se permite procesar la misma caída dos veces */
    assert(
        !failover_handle_node_failure(
            &planner,
            1u,
            &result
        )
    );

    /* Tampoco se permite utilizar un nodo inexistente */
    assert(
        !failover_handle_node_failure(
            &planner,
            99u,
            &result
        )
    );
}

/*
 * PRUEBA 2
 *
 * Comprueba que las tareas más críticas
 * tengan prioridad ante capacidad insuficiente.
 */
static void test_criticality(void)
{
    Planner planner;

    FailoverResult result;

    assert(planner_init(
        &planner,
        SCHED_EDF,
        PLACEMENT_FIRST_FIT
    ));

    assert(planner_add_node(
        &planner, 1u, 100u
    ));

    assert(planner_add_node(
        &planner, 2u, 10u
    ));

    /* Tarea existente en el nodo sobreviviente */
    seed(
        &planner,
        1,
        task(3u, TASK_PERIODIC, 8u, 10u, 7)
    );

    /*
     * Agregamos primero la tarea menos crítica
     * para comprobar que el orden original
     * no determina la prioridad.
     */
    seed(
        &planner,
        0,
        task(1u, TASK_PERIODIC, 40u, 10u, 1)
    );

    seed(
        &planner,
        0,
        task(2u, TASK_PERIODIC, 60u, 10u, 9)
    );

    /* Simular falla */
    assert(failover_handle_node_failure(
        &planner,
        1u,
        &result
    ));

    /* La tarea más crítica debe admitirse */
    assert(
        find_entry(&result, 2u)->decision.accepted
    );

    assert(
        find_entry(&result, 2u)->decision.node_id == 2u
    );

    /* La menos crítica debe rechazarse */
    assert(
        !find_entry(&result, 1u)->decision.accepted
    );

    assert(
        find_entry(&result, 1u)->decision.reason ==
        REJECT_INSUFFICIENT_CAPACITY
    );

    /* Verificar asignaciones finales */
    assert(planner.nodes[1].task_count == 2u);

    assert(
        planner.nodes[1].tasks[0].task.id == 3u
    );

    assert(
        planner.nodes[1].tasks[1].task.id == 2u
    );

    /* Utilización final: 0.1 + 0.6 = 0.7 */
    assert(
        fabs(planner.nodes[1].utilization - 0.7)
        < 1e-12
    );
}

/*
 * PRUEBA 3
 *
 * Verifica el rechazo cuando no existe
 * ningún nodo sobreviviente.
 */
static void test_no_survivor(void)
{
    Planner planner;

    FailoverResult result;

    assert(planner_init(
        &planner,
        SCHED_RMS,
        PLACEMENT_WORST_FIT
    ));

    assert(planner_add_node(
        &planner, 1u, 20u
    ));

    seed(
        &planner,
        0,
        task(4u, TASK_PERIODIC, 20u, 10u, 1)
    );

    assert(failover_handle_node_failure(
        &planner,
        1u,
        &result
    ));

    assert(result.count == 1u);

    assert(!result.entries[0].decision.accepted);

    assert(
        result.entries[0].decision.reason ==
        REJECT_NO_AVAILABLE_NODE
    );
}

/*
 * PRUEBA 4
 *
 * Verifica que Worst-Fit conserve su
 * comportamiento durante la replanificación.
 */
static void test_worst_fit(void)
{
    Planner planner;

    FailoverResult result;

    assert(planner_init(
        &planner,
        SCHED_EDF,
        PLACEMENT_WORST_FIT
    ));

    assert(planner_add_node(
        &planner, 1u, 100u
    ));

    assert(planner_add_node(
        &planner, 2u, 50u
    ));

    assert(planner_add_node(
        &planner, 3u, 50u
    ));

    /* Tarea del nodo que fallará */
    seed(
        &planner,
        0,
        task(1u, TASK_PERIODIC, 50u, 10u, 4)
    );

    /* Nodo 2: utilización 0.5 */
    seed(
        &planner,
        1,
        task(2u, TASK_PERIODIC, 50u, 2u, 3)
    );

    /* Nodo 3: utilización 0.1 */
    seed(
        &planner,
        2,
        task(3u, TASK_PERIODIC, 50u, 10u, 3)
    );

    /* Simular falla del nodo 1 */
    assert(failover_handle_node_failure(
        &planner,
        1u,
        &result
    ));

    assert(result.count == 1u);

    assert(result.entries[0].decision.accepted);

    /*
     * Worst-Fit debe seleccionar el nodo 3
     * porque tiene más capacidad disponible.
     */
    assert(
        result.entries[0].decision.node_id == 3u
    );

    /* El nodo 2 conserva sus asignaciones */
    assert(planner.nodes[1].task_count == 1u);

    /* El nodo 3 recibe la tarea reubicada */
    assert(planner.nodes[2].task_count == 2u);
}

/*
 * Ejecutar todas las pruebas.
 */
int main(void)
{
    test_reallocation();

    test_criticality();

    test_no_survivor();

    test_worst_fit();

    puts("OK: failover tests");

    return 0;
}