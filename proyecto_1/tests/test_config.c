#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "host/config.h"

/*
 * Crea un archivo temporal con el
 * contenido indicado.
 */
static void save(
    char path[],
    const char *text
)
{
    int fd = mkstemp(path);

    assert(fd >= 0);

    FILE *f = fdopen(fd, "w");

    assert(f != NULL);
    assert(fputs(text, f) >= 0);
    assert(fclose(f) == 0);
}

/* Configuración válida para las pruebas */
#define VALID_CONFIG \
    "algorithm=EDF\n" \
    "heuristic=FIRST_FIT\n" \
    "tick_ms=10\n" \
    "port_esp32=/dev/ttyUSB0\n" \
    "port_pico=/dev/ttyACM0\n" \
    "report_every_ticks=10\n" \
    "connection_timeout_ms=6000\n" \
    "tasks_file=config/tasks.csv\n"

int main(void)
{
    char error[256];

    HostConfig config = {0};

    /*
     * Prueba 1:
     * Configuración correcta.
     */
    char path1[] = "/tmp/so_config_a_XXXXXX";

    save(path1, "# Comentario\n" VALID_CONFIG);

    assert(config_load(
        path1,
        &config,
        error,
        sizeof(error)
    ));

    assert(config.algorithm == SCHED_EDF);

    assert(
        config.heuristic == PLACEMENT_FIRST_FIT
    );

    assert(config.tick_ms == 10u);
    assert(config.connection_timeout_ms == 6000u);

    assert(
        strcmp(
            config.tasks_file,
            "config/tasks.csv"
        ) == 0
    );

    unlink(path1);

    /*
     * Prueba 2:
     * Parámetro duplicado.
     */
    char path2[] = "/tmp/so_config_b_XXXXXX";

    save(
        path2,
        VALID_CONFIG "tick_ms=20\n"
    );

    assert(!config_load(
        path2,
        &config,
        error,
        sizeof(error)
    ));

    assert(strstr(error, "duplicada") != NULL);

    unlink(path2);

    /*
     * Prueba 3:
     * Parámetros obligatorios ausentes.
     */
    char path3[] = "/tmp/so_config_c_XXXXXX";

    save(path3, "algorithm=EDF\n");

    assert(!config_load(
        path3,
        &config,
        error,
        sizeof(error)
    ));

    assert(strstr(error, "Falta") != NULL);

    unlink(path3);

    /*
     * Prueba 4:
     * Clave desconocida.
     */
    char path4[] = "/tmp/so_config_d_XXXXXX";

    save(
        path4,
        VALID_CONFIG "desconocida=12\n"
    );

    assert(!config_load(
        path4,
        &config,
        error,
        sizeof(error)
    ));

    unlink(path4);

    /*
     * Prueba 5:
     * Duración del tick inválida.
     */
    char path5[] = "/tmp/so_config_e_XXXXXX";

    save(
        path5,
        "algorithm=RMS\n"
        "heuristic=WORST_FIT\n"
        "tick_ms=0\n"
        "port_esp32=/dev/ttyUSB0\n"
        "port_pico=/dev/ttyACM0\n"
        "report_every_ticks=10\n"
        "connection_timeout_ms=6000\n"
        "tasks_file=config/tasks.csv\n"
    );

    assert(!config_load(
        path5,
        &config,
        error,
        sizeof(error)
    ));

    assert(strstr(error, "tick_ms") != NULL);

    unlink(path5);

    /*
     * Prueba 6:
     * Configuración RMS y Worst-Fit.
     */
    char path6[] = "/tmp/so_config_f_XXXXXX";

    save(
        path6,
        "algorithm=RMS\n"
        "heuristic=WORST_FIT\n"
        "tick_ms=10\n"
        "port_esp32=/dev/ttyUSB0\n"
        "port_pico=/dev/ttyACM0\n"
        "report_every_ticks=10\n"
        "connection_timeout_ms=6000\n"
        "tasks_file=config/tasks.csv\n"
    );

    assert(config_load(
        path6,
        &config,
        error,
        sizeof(error)
    ));

    assert(config.algorithm == SCHED_RMS);

    assert(
        config.heuristic == PLACEMENT_WORST_FIT
    );

    unlink(path6);

    puts("OK: config tests");

    return 0;
}