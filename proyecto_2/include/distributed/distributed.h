#ifndef DIPS_DISTRIBUTED_H
#define DIPS_DISTRIBUTED_H

#include "distributed/mpi_transport.h"
#include "filters/filters.h"
#include "common/image_job.h"

#define DISTRIBUTED_MAX_IMAGE (32u * 1024u * 1024u)
#define DISTRIBUTED_MAX_DIMENSION 32768u

enum worker_state { AVAILABLE, BUSY, ERROR, FINISHED }; // Estados que el coordinador mantiene para cada trabajador.
typedef image_job distributed_config;

typedef struct {
    enum worker_state state;
    char hostname[MPI_MAX_PROCESSOR_NAME + 1]; // Identifica el equipo anfitrion o la VM donde corre el rank.
    uint32_t active_task, completed; // Relaciona el resultado esperado con la tarea asignada y sus estadisticas.
    uint64_t processing_ns; // Tiempo acumulado de los kernels, medido en nanosegundos.
    int registered, ready, result_received, stop_sent; // Controla las transiciones de registro, trabajo y cierre.
} distributed_worker;

typedef struct {
    mpi_transport *transport;
    distributed_worker *workers; // Registro del coordinador; se conserva entre solicitudes TCP.
} distributed_cluster;

/* Persistent cluster API. Rank 0 initializes, processes sequential requests,
 * then finishes. Other ranks call worker_serve exactly once. Transport errors
 * are fatal: caller must MPI_Abort, never retry a partly dispatched phase. */
int distributed_cluster_init(distributed_cluster *cluster, mpi_transport *transport);
int distributed_worker_serve(mpi_transport *transport, const char *hostname);
int distributed_cluster_process(distributed_cluster *cluster, const image_job *job,
                                const uint8_t *input, uint32_t source_width, uint32_t source_height,
                                uint8_t **output, int verify); // Entrega la imagen reconstruida; verify compara contra el filtro secuencial.
int distributed_cluster_finish(distributed_cluster *cluster);
void distributed_cluster_destroy(distributed_cluster *cluster);

/* Validate limits and independent block count/row size. 0 or -1/errno. */
int distributed_config_valid(const distributed_config *config);
/* Rank 0 generates and reconstructs the deterministic test image and compares
 * to the sequential shared filter. Other ranks only process assigned pixels.
 * Returns 0 on exact equality, 1 on mismatch, -1 on fatal error (MPI_Abort
 * required). At least 4 ranks. All application messages use mpi_transport. */
int distributed_run(const distributed_config *config, mpi_transport *transport,
                    const char *hostname);

#endif
