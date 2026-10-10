#include "host/config.h"
#include "host/task_parser.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Convierte una velocidad de demostración
 * a un entero positivo de 32 bits.
 */
static bool parse_speed(
    const char *str,
    uint32_t *speed
)
{
    if (*str < '0' || *str > '9')
        return false;

    errno = 0;

    char *end;

    uintmax_t n = strtoumax(
        str,
        &end,
        10
    );

    if (errno != 0 ||
        *end != '\0' ||
        n == 0u ||
        n > UINT32_MAX) {

        return false;
    }

    *speed = (uint32_t)n;

    return true;
}

/*
 * Convierte un motivo de rechazo
 * en una cadena descriptiva.
 */
static const char *rejection_name(
    RejectReason reason
)
{
    switch (reason) {

        case REJECT_NONE:
            return "NINGUNO";

        case REJECT_INVALID_TASK:
            return "TAREA_INVALIDA";

        case REJECT_DUPLICATE_TASK:
            return "ID_DUPLICADO";

        case REJECT_INSUFFICIENT_CAPACITY:
            return "SIN_CAPACIDAD";

        case REJECT_SPORADIC_LIMIT:
            return "LIMITE_ESPORADICAS";

        case REJECT_NO_AVAILABLE_NODE:
            return "SIN_NODO";

        case REJECT_SPORADIC_NODE_FAILED:
            return "ESPORADICA_NO_REUBICABLE";

        default:
            return "DESCONOCIDO";
    }
}

/*
 * Punto de entrada del Host.
 *
 * Carga configuración y tareas.
 * En modo demo ejecuta la planificación
 * utilizando capacidades simuladas.
 */
int main(int argc, char **argv)
{
    bool demo = false;

    const char *config_path = "config/system.conf";

    uint32_t speed_esp32 = 0u;
    uint32_t speed_pico = 0u;

    /*
     * Procesar argumentos.
     *
     * Uso normal:
     * ./host [config.conf]
     *
     * Demostración:
     * ./host --demo config.conf velocidad1 velocidad2
     */
    if (argc == 2) {

        config_path = argv[1];

    } else if (
        argc == 5 &&
        strcmp(argv[1], "--demo") == 0
    ) {

        demo = true;

        config_path = argv[2];

        if (!parse_speed(argv[3], &speed_esp32) ||
            !parse_speed(argv[4], &speed_pico)) {

            fprintf(
                stderr,
                "Error: velocidades de demo invalidas\n"
            );

            return EXIT_FAILURE;
        }

    } else if (argc != 1) {

        fprintf(
            stderr,
            "Uso: %s [config.conf]\n",
            argv[0]
        );

        fprintf(
            stderr,
            "     %s --demo config.conf "
            "ut_esp32 ut_pico\n",
            argv[0]
        );

        return EXIT_FAILURE;
    }

    /* Estructuras principales */
    HostConfig config;

    TaskList tasks = {0};

    char error[256];

    /* Cargar configuración */
    if (!config_load(
            config_path,
            &config,
            error,
            sizeof(error))) {

        fprintf(
            stderr,
            "Configuracion: %s\n",
            error
        );

        return EXIT_FAILURE;
    }

    /* Cargar tareas */
    if (!task_parser_load(
            config.tasks_file,
            &tasks,
            error,
            sizeof(error))) {

        fprintf(
            stderr,
            "Tareas: %s\n",
            error
        );

        return EXIT_FAILURE;
    }

    /* Mostrar configuración */
    printf(
        "Algoritmo: %s | Heuristica: %s | "
        "Tick: %" PRIu32 " ms\n",

        config.algorithm == SCHED_EDF
            ? "EDF"
            : "RMS",

        config.heuristic == PLACEMENT_FIRST_FIT
            ? "FIRST_FIT"
            : "WORST_FIT",

        config.tick_ms
    );

    printf(
        "Tareas cargadas: %zu\n",
        tasks.count
    );

    /*
     * En modo normal todavía no se planifica.
     * Se necesitan velocidades calibradas.
     */
    if (!demo) {

        puts(
            "Planificacion pendiente de "
            "calibracion real de los nodos."
        );

        task_list_free(&tasks);

        return EXIT_SUCCESS;
    }

    /*
     * Modo demostración.
     * Las velocidades se reciben por argumentos.
     */
    puts(
        "[DEMO] Las velocidades son simuladas; "
        "no se usan en modo real."
    );

    Planner planner;

    /* Inicializar planificador y nodos */
    if (!planner_init(
            &planner,
            config.algorithm,
            config.heuristic) ||

        !planner_add_node(
            &planner,
            1u,
            speed_esp32) ||

        !planner_add_node(
            &planner,
            2u,
            speed_pico)) {

        fprintf(
            stderr,
            "Error inicializando nodos simulados\n"
        );

        task_list_free(&tasks);

        return EXIT_FAILURE;
    }

    /* Reservar arreglo de decisiones */
    PlannerDecision *decisions = NULL;

    if (tasks.count > 0u) {

        if (tasks.count >
            SIZE_MAX / sizeof(*decisions)) {

            fprintf(
                stderr,
                "Demasiadas tareas\n"
            );

            task_list_free(&tasks);

            return EXIT_FAILURE;
        }

        decisions = calloc(
            tasks.count,
            sizeof(*decisions)
        );

        if (decisions == NULL) {

            fprintf(
                stderr,
                "Sin memoria para decisiones\n"
            );

            task_list_free(&tasks);

            return EXIT_FAILURE;
        }
    }

    /* Ejecutar planificación */
    if (!planner_plan_batch(
            &planner,
            tasks.items,
            tasks.count,
            decisions)) {

        fprintf(
            stderr,
            "Fallo la planificacion\n"
        );

        free(decisions);
        task_list_free(&tasks);

        return EXIT_FAILURE;
    }

    /*
     * Mostrar decisiones de asignación.
     */
    for (size_t i = 0u; i < tasks.count; ++i) {

        const PlannerDecision *decision =
            &decisions[i];

        if (decision->accepted) {

            printf(
                "Tarea %" PRIu32
                " -> Nodo %" PRIu32
                " [ACEPTADA]\n",

                decision->task_id,
                decision->node_id
            );

        } else {

            printf(
                "Tarea %" PRIu32
                " -> [RECHAZADA: %s]\n",

                decision->task_id,
                rejection_name(decision->reason)
            );
        }
    }

    /*
     * Mostrar utilización de cada nodo.
     */
    for (size_t i = 0u; i < planner.node_count; ++i) {

        printf(
            "Nodo %" PRIu32
            ": utilizacion %.4f (%zu tareas)\n",

            planner.nodes[i].id,
            planner.nodes[i].utilization,
            planner.nodes[i].task_count
        );
    }

    /* Liberar recursos */
    free(decisions);

    task_list_free(&tasks);

    return EXIT_SUCCESS;
}