#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "host/manual_tasks.h"

/*
 * Construye una tarea periódica para pruebas.
 */
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

/*
 * PRUEBA 1:
 * Validar la interpretación de tareas manuales.
 */
static void test_parser(void)
{
    char error[128];

    Task task = {0};

    assert(manual_task_parse(
        " 7 , S , 20 , 50 , 50 , -2 ",
        &task,
        error,
        sizeof(error)
    ));

    assert(task.id == 7u);
    assert(task.type == TASK_SPORADIC);
    assert(task.criticality == -2);

    /* Período inválido */
    assert(!manual_task_parse(
        "1,P,10,0,10,1",
        &task,
        error,
        sizeof(error)
    ));

    /* Tipo inválido */
    assert(!manual_task_parse(
        "1,X,10,20,20,1",
        &task,
        error,
        sizeof(error)
    ));

    /* Campo faltante */
    assert(!manual_task_parse(
        "1,P,10,20,20",
        &task,
        error,
        sizeof(error)
    ));

    /* Campo adicional */
    assert(!manual_task_parse(
        "1,P,10,20,20,1,extra",
        &task,
        error,
        sizeof(error)
    ));

    /* Trabajo fuera de rango */
    assert(!manual_task_parse(
        "1,P,4294967296,20,20,1",
        &task,
        error,
        sizeof(error)
    ));

    /* Criticidad fuera de rango */
    assert(!manual_task_parse(
        "1,P,10,20,20,2147483648",
        &task,
        error,
        sizeof(error)
    ));
}

/*
 * PRUEBA 2:
 * Incorporación, confirmación y duplicados.
 */
static void test_confirm_and_duplicate(void)
{
    Planner planner;

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

    Task task = periodic(11u, 20u, 10u);

    ManualTaskRequest request = {0};
    ManualTaskRequest duplicated = {0};

    /* Incorporar tarea */
    assert(manual_task_submit(
        &planner,
        &task,
        &request
    ));

    assert(request.status == MANUAL_PENDING);
    assert(request.decision.accepted);
    assert(request.decision.node_id == 1u);

    assert(planner.nodes[0].task_count == 1u);

    /* No reutilizar una solicitud pendiente */
    assert(!manual_task_submit(
        &planner,
        &task,
        &request
    ));

    /* Rechazar una tarea con ID duplicado */
    assert(manual_task_submit(
        &planner,
        &task,
        &duplicated
    ));

    assert(
        duplicated.status == MANUAL_REJECTED
    );

    assert(
        duplicated.decision.reason ==
        REJECT_DUPLICATE_TASK
    );

    assert(planner.nodes[0].task_count == 1u);

    /* Simular confirmación satisfactoria */
    assert(manual_task_confirm(&request));

    assert(
        request.status == MANUAL_CONFIRMED
    );

    /* Una tarea confirmada no puede cancelarse */
    assert(!manual_task_cancel(
        &planner,
        &request
    ));

    /* Tampoco puede confirmarse dos veces */
    assert(!manual_task_confirm(&request));
}

/*
 * PRUEBA 3:
 * Cancelación y recuperación de capacidad.
 */
static void test_cancel_and_sporadic(void)
{
    Planner planner;

    assert(planner_init(
        &planner,
        SCHED_EDF,
        PLACEMENT_FIRST_FIT
    ));

    assert(planner_add_node(
        &planner, 1u, 100u
    ));

    /* Tarea preexistente */
    Task initial = periodic(1u, 20u, 10u);

    assert(
        planner_assign_task(
            &planner,
            &initial
        ).accepted
    );

    double before =
        planner.nodes[0].utilization;

    /* Nueva tarea manual */
    Task next = periodic(2u, 40u, 20u);

    ManualTaskRequest request = {0};

    assert(manual_task_submit(
        &planner,
        &next,
        &request
    ));

    assert(request.status == MANUAL_PENDING);

    assert(planner.nodes[0].task_count == 2u);

    /* Simular fallo del despliegue */
    assert(manual_task_cancel(
        &planner,
        &request
    ));

    assert(request.status == MANUAL_CANCELLED);

    /* Debe conservarse la tarea anterior */
    assert(planner.nodes[0].task_count == 1u);

    assert(
        planner.nodes[0].tasks[0].task.id ==
        initial.id
    );

    /* La utilización debe recuperarse */
    assert(
        fabs(
            planner.nodes[0].utilization - before
        ) < 1e-12
    );

    /* La solicitud cancelada no puede repetirse */
    assert(!manual_task_cancel(
        &planner,
        &request
    ));

    assert(!manual_task_confirm(&request));

    /*
     * Verificar que cancelar una esporádica
     * también libere su cupo.
     */
    Task sporadic = periodic(3u, 10u, 20u);

    sporadic.type = TASK_SPORADIC;

    ManualTaskRequest request2 = {0};

    assert(manual_task_submit(
        &planner,
        &sporadic,
        &request2
    ));

    assert(planner.nodes[0].sporadic_count == 1u);

    assert(manual_task_cancel(
        &planner,
        &request2
    ));

    assert(planner.nodes[0].sporadic_count == 0u);

    /* Debe poder admitirse nuevamente */
    assert(
        planner_assign_task(
            &planner,
            &sporadic
        ).accepted
    );
}

/*
 * PRUEBA 4:
 * Rechazo de tareas que no pasan admisión.
 */
static void test_rejection(void)
{
    Planner planner;

    assert(planner_init(
        &planner,
        SCHED_EDF,
        PLACEMENT_FIRST_FIT
    ));

    assert(planner_add_node(
        &planner, 1u, 10u
    ));

    ManualTaskRequest request = {0};

    /* Tarea que supera la capacidad */
    Task too_large = periodic(
        9u,
        500u,
        1u
    );

    assert(manual_task_submit(
        &planner,
        &too_large,
        &request
    ));

    assert(request.status == MANUAL_REJECTED);

    assert(
        request.decision.reason ==
        REJECT_INSUFFICIENT_CAPACITY
    );

    assert(planner.nodes[0].task_count == 0u);

    /* No se puede confirmar una tarea rechazada */
    assert(!manual_task_confirm(&request));

    assert(!manual_task_cancel(
        &planner,
        &request
    ));

    /* Rechazar atributos inválidos */
    Task invalid = periodic(
        10u,
        0u,
        10u
    );

    assert(manual_task_submit(
        &planner,
        &invalid,
        &request
    ));

    assert(request.status == MANUAL_REJECTED);

    assert(
        request.decision.reason ==
        REJECT_INVALID_TASK
    );

    assert(planner.nodes[0].task_count == 0u);
}

/*
 * Ejecutar las pruebas del módulo.
 */
int main(void)
{
    test_parser();

    test_confirm_and_duplicate();

    test_cancel_and_sporadic();

    test_rejection();

    puts("OK: manual tasks tests");

    return 0;
}