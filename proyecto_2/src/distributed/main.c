#include "distributed/distributed.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *program)
{
    fprintf(stderr, "Uso: %s [--filter blur|sharpen|sobel] [--width N] [--height N]\n"
            "       [--block-rows N | --blocks N] [--radius 1..4] [--intensity 0..16]\n"
            "Requiere al menos 4 ranks y la misma DIPS_AES_KEY en todos.\n", program);
}

static int number(const char *text, uint32_t *out)
{
    uint32_t value = 0;
    if (!text || !*text) return -1;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9') return -1;
        unsigned digit = (unsigned)(*p - '0');
        if (value > (UINT32_MAX - digit) / 10) return -1;
        value = value * 10 + digit;
    }
    *out = value; return 0;
}

static int parse(int argc, char **argv, distributed_config *c)
{
    *c = (distributed_config){FILTER_BLUR, 193, 129, 8, 0, 1, 2}; // Configura la prueba determinista sin depender de TCP ni de archivos.
    int partition = 0;
    for (int i = 1; i < argc; i += 2) {
        if (i + 1 >= argc) return -1;
        const char *option = argv[i], *value = argv[i + 1];
        if (strcmp(option, "--filter") == 0) {
            if (strcmp(value, "blur") == 0) c->filter = FILTER_BLUR;
            else if (strcmp(value, "sharpen") == 0) c->filter = FILTER_SHARPEN;
            else if (strcmp(value, "sobel") == 0) c->filter = FILTER_SOBEL;
            else return -1;
        } else {
            uint32_t n;
            if (number(value, &n) < 0) return -1;
            if (strcmp(option, "--width") == 0) c->width = n;
            else if (strcmp(option, "--height") == 0) c->height = n;
            else if (strcmp(option, "--radius") == 0) c->radius = n;
            else if (strcmp(option, "--intensity") == 0) c->intensity = n;
            else if (strcmp(option, "--blocks") == 0) {
                if (partition || n == 0) return -1; // Acepta una sola forma de particionar y exige bloques no vacios.
                c->blocks = n; c->block_rows = 0; partition = 1;
            } else if (strcmp(option, "--block-rows") == 0) {
                if (partition || n == 0) return -1; // Acepta una sola forma de particionar y exige bloques no vacios.
                c->block_rows = n; partition = 1;
            } else return -1;
        }
    }
    return distributed_config_valid(c);
}

static void abort_mpi(int rank, const char *reason)
{
    fprintf(stderr, "Rank %d: %s (errno=%d)\n", rank, reason, errno);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE); // Detiene todos los ranks ante un fallo irrecuperable.
}

int main(int argc, char **argv)
{
    if (MPI_Init(&argc, &argv) != MPI_SUCCESS) { // Inicializa el entorno MPI en cada proceso lanzado por mpirun.
        fprintf(stderr, "MPI_Init fallo.\n"); return EXIT_FAILURE;
    }
    int rank = -1, size = 0;
    if (MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN) != MPI_SUCCESS ||
        MPI_Comm_rank(MPI_COMM_WORLD, &rank) != MPI_SUCCESS ||
        MPI_Comm_size(MPI_COMM_WORLD, &size) != MPI_SUCCESS) {
        abort_mpi(rank, "No se pudo inicializar MPI"); return EXIT_FAILURE;
    }
    char hostname[MPI_MAX_PROCESSOR_NAME + 1];
    int hostname_size = 0;
    if (MPI_Get_processor_name(hostname, &hostname_size) != MPI_SUCCESS ||
        hostname_size <= 0 || hostname_size > MPI_MAX_PROCESSOR_NAME) {
        abort_mpi(rank, "Hostname MPI invalido"); return EXIT_FAILURE;
    }
    hostname[hostname_size] = '\0'; // Termina el nombre local antes de registrarlo o mostrarlo.
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        if (rank == 0) usage(argv[0]);
        return MPI_Finalize() == MPI_SUCCESS ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    distributed_config config;
    mpi_transport transport = {0};
    int valid = size >= 4 && parse(argc, argv, &config) == 0; // Exige un coordinador y al menos tres trabajadores, sin fijar un maximo.
    if (!valid && rank == 0) usage(argv[0]);
    if (valid && mpi_transport_init(&transport, MPI_COMM_WORLD) < 0) { // Carga y valida DIPS_AES_KEY individualmente en cada rank.
        fprintf(stderr, "Rank %d: DIPS_AES_KEY invalida/ausente o inicializacion fallida.\n", rank);
        valid = 0;
    }
    int all_valid = 0;
    if (MPI_Allreduce(&valid, &all_valid, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD) != MPI_SUCCESS) { // Todos deben estar listos antes de comenzar los envios entre ranks.
        mpi_transport_destroy(&transport);
        abort_mpi(rank, "MPI_Allreduce fallo"); return EXIT_FAILURE;
    }
    if (!all_valid) {
        mpi_transport_destroy(&transport);
        if (MPI_Finalize() != MPI_SUCCESS) return EXIT_FAILURE; // Libera el entorno MPI una vez detenidos los trabajadores.
        return EXIT_FAILURE;
    }
    int result = distributed_run(&config, &transport, hostname); // Rank 0 coordina la prueba; los demas procesan los bloques.
    if (result < 0) {
        int saved = errno; mpi_transport_destroy(&transport); errno = saved;
        abort_mpi(rank, "Error irrecuperable de transporte o procesamiento");
        return EXIT_FAILURE;
    }
    mpi_transport_destroy(&transport);
    int global_result;
    if (MPI_Allreduce(&result, &global_result, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD) != MPI_SUCCESS) { // Propaga el resultado final para que todos terminen coherentemente.
        abort_mpi(rank, "MPI_Allreduce final fallo"); return EXIT_FAILURE;
    }
    if (MPI_Finalize() != MPI_SUCCESS) return EXIT_FAILURE; // Libera el entorno MPI una vez detenidos los trabajadores.
    return global_result == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
