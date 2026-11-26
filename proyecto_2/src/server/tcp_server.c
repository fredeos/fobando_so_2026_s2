#include "server/header.h"
#include "common/tcp.h"
#include "common/image_job.h"
#include "common/image_codec.h"
#include "distributed/distributed.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>

static volatile sig_atomic_t stopping;
static void stop_signal(int signal_number) {
    (void)signal_number;
    stopping = 1; // La senal solo marca la salida; el flujo normal cierra los recursos.
}
static int parse_port(const char *text, uint16_t *port) {
    unsigned value = 0;
    if (!*text)
        return -1;
    for (const char *p = text; *p; ++p) {
        if (*p < '0' || *p > '9')
            return -1;
        value = value * 10 + (unsigned)(*p - '0');
        if (value > 65535)
            return -1;
    }
    if (!value)
        return -1;
    *port = (uint16_t)value;
    return 0;
}
static int send_error(int fd, const uint8_t *key, const char *text) {
    if (tcp_send_message(fd, key, TCP_MESSAGE_ERROR, text, strlen(text)) < 0)
        perror("Respuesta de error TCP");
    return 0;
}
/* -1 is a fatal MPI error; 0 is an isolated client error; 1 is success. */
static int handle_client(int fd, const uint8_t *key, distributed_cluster *cluster, int verify) {
    tcp_message message = {0};
    uint8_t *input = NULL, *output = NULL, *encoded = NULL;
    uint32_t sw = 0, sh = 0;
    size_t encoded_size = 0;
    image_job job = {0};
    int result = 0;
    if (tcp_recv_message(fd, key, &message) < 0) { // No procesa solicitudes que no superen la autenticacion GCM.
        perror("Recepcion/autenticacion TCP");
        return 0;
    }
    if (message.type == TCP_MESSAGE_IMAGE && message.payload_size) { // Compatibilidad con el modo antiguo --ack-only.
        const uint8_t ack[] = {0x4f, 0x4b};
        printf("Imagen autenticada recibida: %u bytes.\n", message.payload_size);
        fflush(stdout);
        result = tcp_send_message(fd, key, TCP_MESSAGE_ACK, ack, sizeof(ack)) == 0;
        goto cleanup;
    }
    if (message.type != TCP_MESSAGE_PROCESS ||
        image_job_decode(message.payload, message.payload_size, &job) < 0) {
        send_error(fd, key, "Solicitud de procesamiento invalida");
        goto cleanup;
    }
    tcp_message_free(&message);
    if (tcp_recv_message(fd, key, &message) < 0) { // No procesa solicitudes que no superen la autenticacion GCM.
        perror("Imagen TCP incompleta/no autentica");
        goto cleanup;
    }
    if (message.type != TCP_MESSAGE_IMAGE || !message.payload_size) {
        send_error(fd, key, "Se esperaba IMAGE");
        goto cleanup;
    }
    if (!cluster || !cluster->workers) {
        send_error(fd, key, "Inicia el servidor con mpirun -np 4 o mas");
        goto cleanup;
    }
    if (image_decode(message.payload, message.payload_size, &input, &sw, &sh) < 0) {
        send_error(fd, key, "PNG/JPEG invalido o imagen superior a 32 MiB BGR");
        goto cleanup;
    }
    tcp_message_free(&message);
    if (!job.width && !job.height) {
        job.width = sw; // Si el cliente pide original, usa las dimensiones decodificadas.
        job.height = sh;
    }
    if (image_job_valid(&job, 0) < 0) {
        send_error(fd, key, "Dimensiones, filtro o bloques invalidos");
        goto cleanup;
    }
    if (distributed_cluster_process(cluster, &job, input, sw, sh, &output, verify) < 0) { // Entrega los pixeles al planificador MPI de resize y filtro.
        result = -1; // Un error MPI es fatal para todos los ranks, no solo para el cliente.
        goto cleanup;
    }
    if (image_encode_png(output, job.width, job.height, filters_channels(job.filter), &encoded,
                         &encoded_size) < 0) {
        send_error(fd, key, "No se pudo codificar resultado PNG (limite TCP: 8 MiB)");
        goto cleanup;
    }
    if (tcp_send_message(fd, key, TCP_MESSAGE_RESULT, encoded, encoded_size) < 0) { // Devuelve al cliente el PNG procesado con cifrado y autenticacion.
        perror("Envio del resultado");
        goto cleanup;
    }
    printf("Resultado PNG enviado: %ux%u, %zu bytes.\n", job.width, job.height, encoded_size);
    fflush(stdout);
    result = 1;
cleanup: {
    int saved = errno;
    tcp_message_free(&message);
    crypto_clear(input, (size_t)sw * sh * 3);
    crypto_clear(output, (size_t)job.width * job.height * filters_channels(job.filter));
    free(input);
    free(output);
    free(encoded);
    errno = saved;
}
    return result;
}
static int listen_clients(uint16_t port, const uint8_t *key, distributed_cluster *cluster, int once,
                          int verify) {
    int listener = tcp_server_create(port, 16); // Solo el coordinador abre el puerto de servicio TCP.
    if (listener < 0) {
        perror("Servidor TCP");
        return -1;
    }
    struct sigaction action = {0};
    action.sa_handler = stop_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 || sigaction(SIGTERM, &action, NULL) < 0) {
        tcp_close(&listener);
        return -1;
    }
    stopping = 0;
    printf("Servidor TCP IPv4 escuchando en el puerto %u.\n", (unsigned)port);
    fflush(stdout);
    int status = 0;
    while (!stopping) {
        struct pollfd event = {listener, POLLIN, 0};
        int ready = poll(&event, 1, 500); // La espera limitada permite atender la solicitud de cierre.
        if (ready < 0) {
            if (errno == EINTR)
                continue;
            status = -1;
            break;
        }
        if (!ready)
            continue;
        if (!(event.revents & POLLIN)) {
            errno = EIO;
            status = -1;
            break;
        }
        int fd = tcp_accept(listener); // Cada cliente se atiende secuencialmente con su propio socket.
        if (fd < 0) {
            if (errno == ECONNABORTED)
                continue;
            status = -1;
            break;
        }
        struct timeval timeout = {120, 0};
        if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
            tcp_close(&fd);
            status = -1;
            break;
        }
        int handled = handle_client(fd, key, cluster, verify); // Los errores de entrada del cliente no reinician los trabajadores.
        tcp_close(&fd);
        if (handled < 0) {
            status = -1;
            break;
        }
        if (once) { // El modo demo termina el servicio despues de una conexion.
            status = handled == 1 ? 0 : 1;
            break;
        }
    }
    tcp_close(&listener);
    return status;
}
int tcp_server_run(int argc, char **argv) {
    if (MPI_Init(&argc, &argv) != MPI_SUCCESS) // Todos los procesos inicializan MPI, aunque solo rank 0 atienda TCP.
        return EXIT_FAILURE;
    int rank = 0, size = 0;
    if (MPI_Comm_set_errhandler(MPI_COMM_WORLD, MPI_ERRORS_RETURN) != MPI_SUCCESS ||
        MPI_Comm_rank(MPI_COMM_WORLD, &rank) != MPI_SUCCESS ||
        MPI_Comm_size(MPI_COMM_WORLD, &size) != MPI_SUCCESS) {
        MPI_Abort(MPI_COMM_WORLD, 1);
        return EXIT_FAILURE;
    }
    uint16_t port = 0;
    int once = 0, verify = 0;
    int valid = argc >= 2 && parse_port(argv[1], &port) == 0 && (size == 1 || size >= 4); // El cluster requiere coordinador y al menos tres trabajadores.
    for (int i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--once") == 0)
            once = 1;
        else if (strcmp(argv[i], "--verify") == 0)
            verify = 1;
        else
            valid = 0;
    }
    if (!valid && rank == 0)
        fprintf(stderr, "Uso: %s <PUERTO> [--once] [--verify]; cluster: mpirun -np 4 o mas\n",
                argv[0]);
    mpi_transport transport = {0};
    distributed_cluster cluster = {0};
    uint8_t key[CRYPTO_KEY_SIZE] = {0};
    if (valid && crypto_load_key(key) < 0) {
        fprintf(stderr, "Rank %d: DIPS_AES_KEY invalida/ausente\n", rank);
        valid = 0;
    }
    if (valid && size >= 4 && mpi_transport_init(&transport, MPI_COMM_WORLD) < 0)
        valid = 0;
    int all_valid = 0;
    if (MPI_Allreduce(&valid, &all_valid, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD) != MPI_SUCCESS) {
        MPI_Abort(MPI_COMM_WORLD, 1);
        return EXIT_FAILURE;
    }
    int result = 1;
    if (all_valid) {
        if (rank == 0) { // Separa al coordinador de los procesos dedicados al calculo.
            if (size >= 4 && distributed_cluster_init(&cluster, &transport) < 0)
                result = -1; // Un error MPI es fatal para todos los ranks, no solo para el cliente.
            else
                result = listen_clients(port, key, size >= 4 ? &cluster : NULL, once, verify);
            if (result >= 0 && size >= 4 && distributed_cluster_finish(&cluster) < 0) // Envia STOP y espera confirmaciones antes de finalizar MPI.
                result = -1; // Un error MPI es fatal para todos los ranks, no solo para el cliente.
        } else {
            char name[MPI_MAX_PROCESSOR_NAME + 1];
            int length = 0;
            if (MPI_Get_processor_name(name, &length) != MPI_SUCCESS || length <= 0 ||
                length > MPI_MAX_PROCESSOR_NAME)
                result = -1; // Un error MPI es fatal para todos los ranks, no solo para el cliente.
            else {
                name[length] = 0;
                result = distributed_worker_serve(&transport, name); // El trabajador permanece disponible para sucesivas solicitudes.
            }
        }
    }
    if (result < 0) {
        perror("Error irrecuperable del cluster");
        MPI_Abort(MPI_COMM_WORLD, 1);
        return EXIT_FAILURE;
    }
    distributed_cluster_destroy(&cluster);
    mpi_transport_destroy(&transport);
    crypto_clear(key, sizeof(key));
    int global = 1;
    if (MPI_Allreduce(&result, &global, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD) != MPI_SUCCESS) {
        MPI_Abort(MPI_COMM_WORLD, 1);
        return EXIT_FAILURE;
    }
    if (MPI_Finalize() != MPI_SUCCESS) // Cierra MPI cuando ya terminaron el coordinador y los trabajadores.
        return EXIT_FAILURE;
    return global == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
