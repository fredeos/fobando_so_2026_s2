#ifndef HOST_CONFIG_H
#define HOST_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "host/planner.h"

/* Tamaños máximos de las rutas */
#define CONFIG_PORT_SIZE 128u
#define CONFIG_PATH_SIZE 512u

/*
 * Configuración general del Host.
 *
 * Contiene el algoritmo, la heurística,
 * los parámetros temporales y las rutas.
 */
typedef struct {

    SchedulingAlgorithm algorithm;
    PlacementHeuristic heuristic;

    uint32_t tick_ms;
    uint32_t report_every_ticks;
    uint32_t missed_reports_limit;

    char port_esp32[CONFIG_PORT_SIZE];
    char port_pico[CONFIG_PORT_SIZE];
    char tasks_file[CONFIG_PATH_SIZE];

} HostConfig;

/*
 * Lee y valida un archivo de configuración.
 *
 * path: ruta del archivo.
 * out: estructura donde se guardan los valores.
 * error: buffer para describir errores.
 * error_size: tamaño del buffer.
 *
 * Retorna true si la carga fue correcta.
 */
bool config_load(
    const char *path,
    HostConfig *out,
    char *error,
    size_t error_size
);

#endif