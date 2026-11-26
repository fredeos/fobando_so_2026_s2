#include "distributed/distributed.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TASK_HEADER 60u // Quince campos de 32 bits describen el bloque y su imagen fuente.
#define RESULT_HEADER 44u // Incluye identificador, dimensiones, tiempo y etapa del resultado.
enum phase { RESIZE = 1, FILTER = 2 }; // Separa el redimensionamiento del filtrado distribuido.
typedef struct {
    uint32_t id, start, rows; // Identificador y rango de filas de salida que pertenecen a una tarea.
} row_task;

int distributed_config_valid(const distributed_config *c) { return image_job_valid(c, 0); }
static uint32_t task_count(const image_job *c) {
    return c->blocks ? c->blocks : 1 + (c->height - 1) / c->block_rows; // La cantidad de tareas es independiente del numero de trabajadores.
}
static row_task task_at(const image_job *c, uint32_t id) {
    row_task t = {id, 0, 0};
    if (c->blocks) {
        uint32_t base = c->height / c->blocks, rem = c->height % c->blocks; // Reparte las filas sobrantes entre los primeros bloques.
        t.start = id * base + (id < rem ? id : rem);
        t.rows = base + (id < rem ? 1u : 0u);
    } else {
        t.start = id * c->block_rows;
        t.rows = c->height - t.start;
        if (t.rows > c->block_rows) // Recorta el ultimo bloque si la altura no es divisible exactamente.
            t.rows = c->block_rows;
    }
    return t;
}
static void source_rows(const image_job *c, row_task task, unsigned phase, uint32_t sh,
                        uint32_t *top, uint32_t *rows) {
    if (phase == RESIZE) {
        *top = (uint32_t)((uint64_t)task.start * sh / c->height); // Mapea la primera fila de salida a su coordenada global de origen.
        uint32_t last = (uint32_t)((uint64_t)(task.start + task.rows - 1) * sh / c->height);
        *rows = last - *top + 1; // Envia solo el intervalo fuente que necesita este bloque de resize.
    } else {
        *top = task.start > c->radius ? task.start - c->radius : 0; // Incluye el halo superior sin salir de la imagen.
        uint32_t end = task.start + task.rows;
        end += c->height - end < c->radius ? c->height - end : c->radius; // Incluye el halo inferior y respeta el borde global.
        *rows = end - *top;
    }
}
static int register_worker(mpi_transport *t, const char *hostname) {
    size_t n = strlen(hostname);
    if (!n || n > MPI_MAX_PROCESSOR_NAME) {
        errno = EINVAL;
        return -1;
    }
    uint8_t data[12 + MPI_MAX_PROCESSOR_NAME];
    mpi_put_u32(data, (uint32_t)t->rank); // El registro declara el rank que el transporte debe autenticar.
    mpi_put_u32(data + 4, AVAILABLE); // Anuncia que el trabajador esta disponible para recibir tareas.
    mpi_put_u32(data + 8, (uint32_t)n);
    memcpy(data + 12, hostname, n);
    printf("Trabajador rank=%d hostname=%s AVAILABLE\n", t->rank, hostname);
    fflush(stdout);
    if (mpi_transport_send(t, 0, MPI_MSG_REGISTER, data, 12 + n) < 0)
        return -1;
    return mpi_transport_send(t, 0, MPI_MSG_READY, NULL, 0); // Confirma que el trabajador ya puede entrar en su bucle de recepcion.
}
int distributed_cluster_init(distributed_cluster *cluster, mpi_transport *t) {
    if (!cluster || !t || t->rank != 0 || t->size < 4) {
        errno = EINVAL;
        return -1;
    }
    *cluster = (distributed_cluster){t, NULL};
    cluster->workers = calloc((size_t)t->size, sizeof(*cluster->workers)); // Reserva un registro por rank; los workers empiezan en rank 1.
    if (!cluster->workers) {
        errno = ENOMEM;
        return -1;
    }
    int ready = 0;
    while (ready < t->size - 1) { // No distribuye trabajo hasta completar el registro de todos.
        mpi_message m = {0};
        if (mpi_transport_recv(t, MPI_ANY_SOURCE, &m) < 0) // Acepta al siguiente trabajador disponible sin imponer un orden de ranks.
            return -1;
        distributed_worker *w = &cluster->workers[m.source];
        int valid = 0;
        if (m.type == MPI_MSG_REGISTER && !w->registered && m.size >= 12) {
            uint32_t n = mpi_get_u32(m.data + 8);
            valid = mpi_get_u32(m.data) == (uint32_t)m.source &&
                    mpi_get_u32(m.data + 4) == AVAILABLE && n && n <= MPI_MAX_PROCESSOR_NAME &&
                    m.size == 12u + n;
            if (valid)
                for (uint32_t i = 0; i < n; ++i)
                    if (m.data[12 + i] < 32 || m.data[12 + i] > 126)
                        valid = 0;
            if (valid) {
                memcpy(w->hostname, m.data + 12, n);
                w->hostname[n] = 0;
                w->registered = 1; // Impide aceptar dos registros del mismo trabajador.
                w->state = AVAILABLE;
                w->active_task = UINT32_MAX; // Este valor indica que el trabajador no tiene una tarea asignada.
                printf("Registro aceptado: rank=%d hostname=%s AVAILABLE\n", m.source, w->hostname);
            }
        } else if (m.type == MPI_MSG_READY && m.size == 0 && w->registered && !w->ready) {
            w->ready = 1;
            ++ready;
            valid = 1;
        }
        mpi_message_free(&m);
        if (!valid) {
            errno = EPROTO;
            return -1;
        }
    }
    fflush(stdout);
    return 0;
}
static int assign(const image_job *c, distributed_cluster *cluster, int rank, row_task task,
                  unsigned phase, const uint8_t *input, uint32_t sw, uint32_t sh) {
    uint32_t top, rows;
    source_rows(c, task, phase, sh, &top, &rows); // Calcula filas fuente o halos segun la etapa a ejecutar.
    size_t bytes = (size_t)sw * rows * 3;
    uint8_t *data = malloc(TASK_HEADER + bytes);
    if (!data) {
        errno = ENOMEM;
        return -1;
    }
    const uint32_t fields[] = {task.id,   c->filter, c->width, c->height,     task.start,
                               task.rows, top,       rows,     c->radius,     c->intensity,
                               phase,     sw,        sh,       c->block_rows, c->blocks};
    for (unsigned i = 0; i < 15; ++i)
        mpi_put_u32(data + 4 * i, fields[i]); // Serializa cada campo de la tarea en orden de red.
    memcpy(data + TASK_HEADER, input + (size_t)top * sw * 3, bytes); // Adjunta pixeles BGR; los workers no leen archivos compartidos.
    int result =
        mpi_transport_send(cluster->transport, rank, MPI_MSG_TASK, data, TASK_HEADER + bytes);
    int saved = errno;
    crypto_clear(data, TASK_HEADER + bytes);
    free(data);
    if (result == 0) {
        distributed_worker *w = &cluster->workers[rank];
        w->state = BUSY; // Marca al trabajador como ocupado antes de esperar su resultado.
        w->ready = 0;
        w->result_received = 0;
        w->active_task = task.id; // Permite asociar una respuesta exclusivamente con su tarea activa.
        printf("%s TASK %u -> rank=%d filas=[%u,%u) fuente=[%u,%u)\n",
               phase == RESIZE ? "RESIZE" : "FILTER", task.id, rank, task.start,
               task.start + task.rows, top, top + rows);
    }
    errno = saved;
    return result;
}
static int process_task(mpi_transport *t, const mpi_message *m) {
    if (m->size < TASK_HEADER) {
        errno = EPROTO;
        return -1;
    }
    uint32_t f[15];
    for (unsigned i = 0; i < 15; ++i)
        f[i] = mpi_get_u32(m->data + 4 * i);
    image_job c = {f[1], f[2], f[3], f[13], f[14], f[8], f[9]}; // Reconstruye la configuracion desde enteros, no desde punteros recibidos.
    unsigned phase = f[10];
    uint32_t sw = f[11], sh = f[12];
    if (image_job_valid(&c, 0) < 0 || !image_dimensions_valid(sw, sh) ||
        (phase != RESIZE && phase != FILTER) ||
        (phase == FILTER && (sw != c.width || sh != c.height)) || f[0] >= task_count(&c)) {
        errno = EPROTO;
        return -1;
    }
    row_task task = task_at(&c, f[0]); // Recalcula el rango esperado para validar los metadatos de la tarea.
    uint32_t top, rows;
    source_rows(&c, task, phase, sh, &top, &rows);
    if (f[4] != task.start || f[5] != task.rows || f[6] != top || f[7] != rows ||
        m->size != TASK_HEADER + (size_t)sw * rows * 3) { // Exige que los pixeles coincidan exactamente con el bloque anunciado.
        errno = EPROTO;
        return -1;
    }
    unsigned channels = phase == RESIZE ? 3 : filters_channels(c.filter);
    size_t stride = (size_t)c.width * channels;
    size_t bytes = stride * task.rows;
    size_t out_size = phase == RESIZE ? bytes : stride * rows; // El filtrado necesita espacio para el bloque junto con sus halos.
    uint8_t *out = malloc(out_size), *payload = malloc(RESULT_HEADER + bytes);
    if (!out || !payload) {
        free(out);
        free(payload);
        errno = ENOMEM;
        return -1;
    }
    double start = MPI_Wtime();
    int result;
    if (phase == RESIZE)
        result = filters_resize_rows(m->data + TASK_HEADER, out, sw, sh, c.width, c.height, top,
                                     rows, task.start, task.rows);
    else
        result = filters_apply(c.filter, m->data + TASK_HEADER, out, c.width, rows, (size_t)sw * 3,
                               stride, c.radius, c.intensity);
    double elapsed = MPI_Wtime() - start; // Mide el tiempo de calculo del worker, separado del envio del resultado.
    if (result == 0) {
        const uint32_t fields[] = {task.id,    c.filter,  c.width,  c.height,
                                   task.start, task.rows, channels, (uint32_t)bytes};
        for (unsigned i = 0; i < 8; ++i)
            mpi_put_u32(payload + 4 * i, fields[i]);
        mpi_put_u64(payload + 32, (uint64_t)(elapsed * 1000000000.0)); // Transmite nanosegundos enteros en lugar de un double nativo.
        mpi_put_u32(payload + 40, phase);
        size_t offset = phase == RESIZE ? 0 : (size_t)(task.start - top) * stride; // Descarta los halos al seleccionar las filas de salida del bloque.
        memcpy(payload + RESULT_HEADER, out + offset, bytes);
        result = mpi_transport_send(t, 0, MPI_MSG_RESULT, payload, RESULT_HEADER + bytes); // Devuelve al coordinador solo el resultado de las filas asignadas.
    }
    int saved = errno;
    crypto_clear(out, out_size);
    crypto_clear(payload, RESULT_HEADER + bytes);
    free(out);
    free(payload);
    errno = saved;
    return result;
}
int distributed_worker_serve(mpi_transport *t, const char *hostname) {
    if (!t || t->rank == 0 || !hostname || register_worker(t, hostname) < 0)
        return -1;
    for (;;) {
        mpi_message m = {0};
        if (mpi_transport_recv(t, 0, &m) < 0) // El trabajador solo acepta instrucciones autenticadas del coordinador.
            return -1;
        if (m.type == MPI_MSG_STOP && m.size == 0) { // Reconoce la orden de terminacion del coordinador.
            mpi_message_free(&m);
            return mpi_transport_send(t, 0, MPI_MSG_STOP, NULL, 0); // Confirma el cierre antes de salir del bucle del trabajador.
        }
        int result = m.type == MPI_MSG_TASK ? process_task(t, &m) : -1;
        if (m.type != MPI_MSG_TASK)
            errno = EPROTO;
        mpi_message_free(&m);
        if (result < 0) {
            uint8_t error[4];
            mpi_put_u32(error, (uint32_t)errno);
            if (mpi_transport_send(t, 0, MPI_MSG_ERROR, error, 4) < 0)
                return -1;
            /* Root aborts after receiving ERROR. No collective while others work. */
            if (mpi_transport_recv(t, 0, &m) < 0) // El trabajador solo acepta instrucciones autenticadas del coordinador.
                return -1;
            mpi_message_free(&m);
            errno = EPROTO;
            return -1;
        }
        if (mpi_transport_send(t, 0, MPI_MSG_READY, NULL, 0) < 0) // Solo anuncia disponibilidad despues de enviar el resultado completo.
            return -1;
    }
}
static int accept_result(const image_job *c, distributed_worker *w, const mpi_message *m,
                         unsigned phase, uint8_t *output) {
    if (w->state != BUSY || w->result_received || m->size < RESULT_HEADER || // Rechaza respuestas duplicadas o de trabajadores sin tarea activa.
        w->active_task >= task_count(c)) {
        errno = EPROTO;
        return -1;
    }
    row_task task = task_at(c, w->active_task);
    unsigned channels = phase == RESIZE ? 3 : filters_channels(c->filter);
    size_t bytes = (size_t)c->width * channels * task.rows;
    const uint32_t fields[] = {task.id,    c->filter, c->width, c->height,
                               task.start, task.rows, channels, (uint32_t)bytes};
    for (unsigned i = 0; i < 8; ++i)
        if (mpi_get_u32(m->data + 4 * i) != fields[i]) {
            errno = EPROTO;
            return -1;
        }
    if (m->size != RESULT_HEADER + bytes || mpi_get_u32(m->data + 40) != phase) { // Valida la longitud exacta y evita mezclar resize con filtro.
        errno = EPROTO;
        return -1;
    }
    uint64_t elapsed = mpi_get_u64(m->data + 32);
    if (elapsed > UINT64_MAX - w->processing_ns) {
        errno = EOVERFLOW;
        return -1;
    }
    memcpy(output + (size_t)task.start * c->width * channels, m->data + RESULT_HEADER, bytes); // Reconstruye en su posicion global, sin importar el orden de llegada.
    w->result_received = 1; // El coordinador espera READY antes de volver a asignar este worker.
    ++w->completed;
    w->processing_ns += elapsed;
    printf("%s RESULT %u <- rank=%d\n", phase == RESIZE ? "RESIZE" : "FILTER", task.id, m->source);
    return 0;
}
/* FIFO pending queue [next,count); each worker owns at most one task. */
static int run_phase(distributed_cluster *cluster, const image_job *c, const uint8_t *input,
                     uint32_t sw, uint32_t sh, unsigned phase, uint8_t **output) {
    mpi_transport *t = cluster->transport;
    unsigned channels = phase == RESIZE ? 3 : filters_channels(c->filter);
    uint8_t *out = malloc((size_t)c->width * c->height * channels);
    if (!out) {
        errno = ENOMEM;
        return -1;
    }
    uint32_t count = task_count(c), next = 0, completed = 0; // La cola pendiente es el intervalo de identificadores [next, count).
    int active = 0, result = -1;
    double start = MPI_Wtime();
    for (int rank = 1; rank < t->size; ++rank) {
        distributed_worker *w = &cluster->workers[rank];
        if (w->state != AVAILABLE || !w->ready) { // Garantiza que nunca haya mas de una tarea activa por trabajador.
            errno = EPROTO;
            goto cleanup;
        }
        if (next < count) {
            if (assign(c, cluster, rank, task_at(c, next), phase, input, sw, sh) < 0)
                goto cleanup;
            ++next;
            ++active;
        }
    }
    while (active) {
        mpi_message m = {0};
        if (mpi_transport_recv(t, MPI_ANY_SOURCE, &m) < 0) // Acepta al siguiente trabajador disponible sin imponer un orden de ranks.
            goto cleanup;
        distributed_worker *w = &cluster->workers[m.source];
        int valid = 0;
        if (m.type == MPI_MSG_RESULT && accept_result(c, w, &m, phase, out) == 0) {
            ++completed;
            valid = 1;
        } else if (m.type == MPI_MSG_READY && m.size == 0 && w->state == BUSY &&
                   w->result_received) {
            w->state = AVAILABLE;
            w->ready = 1;
            w->active_task = UINT32_MAX; // Este valor indica que el trabajador no tiene una tarea asignada.
            --active; // READY confirma que la tarea termino y libera al trabajador.
            valid = 1;
            if (next < count) {
                valid = assign(c, cluster, m.source, task_at(c, next), phase, input, sw, sh) == 0; // Asigna inmediatamente otro bloque pendiente al worker que acaba de terminar.
                if (valid) {
                    ++next;
                    ++active;
                }
            }
        } else if (m.type == MPI_MSG_ERROR && m.size == 4)
            fprintf(stderr, "ERROR rank=%d codigo=%u\n", m.source, mpi_get_u32(m.data));
        mpi_message_free(&m);
        if (!valid) {
            w->state = ERROR;
            errno = EPROTO;
            goto cleanup;
        }
    }
    if (completed != count || next != count) { // No entrega la imagen hasta recibir todas las tareas esperadas.
        errno = EPROTO;
        goto cleanup;
    }
    printf("%s: %u tareas, tiempo procesamiento %.6f s\n", phase == RESIZE ? "RESIZE" : "FILTER",
           count, MPI_Wtime() - start);
    *output = out; // Transfiere la imagen reconstruida al coordinador de la siguiente etapa.
    out = NULL;
    result = 0;
cleanup: {
    int saved = errno;
    crypto_clear(out, (size_t)c->width * c->height * channels);
    free(out);
    errno = saved;
}
    return result;
}
int distributed_cluster_process(distributed_cluster *cluster, const image_job *c,
                                const uint8_t *input, uint32_t sw, uint32_t sh, uint8_t **output,
                                int verify) {
    if (!output) {
        errno = EINVAL;
        return -1;
    }
    *output = NULL;
    if (!cluster || !cluster->transport || cluster->transport->rank != 0 || !cluster->workers ||
        !input || !image_dimensions_valid(sw, sh) || image_job_valid(c, 0) < 0) {
        errno = EINVAL;
        return -1;
    }
    double start = MPI_Wtime();
    uint8_t *resized = NULL, *filtered = NULL, *serial_resize = NULL, *serial = NULL;
    unsigned channels = filters_channels(c->filter);
    size_t pixels = (size_t)c->width * c->height;
    int result = -1;
    const uint8_t *filter_input = input;
    if (sw != c->width || sh != c->height) { // Omite resize cuando el cliente conserva las dimensiones originales.
        if (run_phase(cluster, c, input, sw, sh, RESIZE, &resized) < 0)
            goto cleanup;
        filter_input = resized; // El filtro usa la imagen completa reconstruida por el resize paralelo.
    } else {
        printf("RESIZE omitido: se conservan las dimensiones %ux%u.\n", sw, sh);
    }
    if (run_phase(cluster, c, filter_input, c->width, c->height, FILTER, &filtered) < 0)
        goto cleanup;
    if (verify) { // Activa la referencia secuencial para comprobar la correccion distribuida.
        const uint8_t *serial_input = input;
        serial = malloc(pixels * channels);
        if (!serial) {
            errno = ENOMEM;
            goto cleanup;
        }
        if (resized) {
            serial_resize = malloc(pixels * 3);
            if (!serial_resize) {
                errno = ENOMEM;
                goto cleanup;
            }
            if (filters_resize_rows(input, serial_resize, sw, sh, c->width, c->height, 0, sh, 0,
                                    c->height) < 0)
                goto cleanup;
            serial_input = serial_resize;
        }
        if (filters_apply(c->filter, serial_input, serial, c->width, c->height,
                          (size_t)c->width * 3, (size_t)c->width * channels, c->radius,
                          c->intensity) < 0)
            goto cleanup;
        int same = (!resized || memcmp(resized, serial_resize, pixels * 3) == 0) &&
                   memcmp(filtered, serial, pixels * channels) == 0; // La comparacion exige igualdad exacta, byte por byte.
        printf("Comparacion secuencial/distribuida %s: %s\n", resized ? "resize+filtro" : "filtro",
               same ? "IGUALES" : "DIFERENTES");
        if (!same) {
            errno = EDOM;
            goto cleanup;
        }
    }
    printf("Imagen %ux%u -> %ux%u filtro=%u tiempo total %.6f s\n", sw, sh, c->width, c->height,
           c->filter, MPI_Wtime() - start);
    fflush(stdout);
    *output = filtered; // Devuelve los pixeles para codificarlos a PNG y enviarlos por TCP.
    filtered = NULL;
    result = 0;
cleanup: {
    int saved = errno;
    crypto_clear(resized, pixels * 3);
    crypto_clear(filtered, pixels * channels);
    crypto_clear(serial_resize, pixels * 3);
    crypto_clear(serial, pixels * channels);
    free(resized);
    free(filtered);
    free(serial_resize);
    free(serial);
    errno = saved;
}
    return result;
}
int distributed_cluster_finish(distributed_cluster *cluster) {
    if (!cluster || !cluster->workers || !cluster->transport) {
        errno = EINVAL;
        return -1;
    }
    mpi_transport *t = cluster->transport;
    for (int rank = 1; rank < t->size; ++rank) {
        distributed_worker *w = &cluster->workers[rank];
        if (w->state != AVAILABLE || !w->ready) { // Garantiza que nunca haya mas de una tarea activa por trabajador.
            errno = EPROTO;
            return -1;
        }
        if (mpi_transport_send(t, rank, MPI_MSG_STOP, NULL, 0) < 0) // Solicita cierre solo cuando todos los trabajadores estan disponibles.
            return -1;
        w->stop_sent = 1;
    }
    for (int i = 1; i < t->size; ++i) {
        mpi_message m = {0};
        if (mpi_transport_recv(t, MPI_ANY_SOURCE, &m) < 0) // Acepta al siguiente trabajador disponible sin imponer un orden de ranks.
            return -1;
        distributed_worker *w = &cluster->workers[m.source];
        int valid = m.type == MPI_MSG_STOP && m.size == 0 && w->stop_sent && w->state == AVAILABLE;
        mpi_message_free(&m);
        if (!valid) {
            errno = EPROTO;
            return -1;
        }
        w->state = FINISHED; // Registra la confirmacion de STOP recibida desde ese trabajador.
    }
    for (int rank = 1; rank < t->size; ++rank)
        printf("rank=%d hostname=%s FINISHED tareas=%u procesamiento=%.6f s\n", rank,
               cluster->workers[rank].hostname, cluster->workers[rank].completed,
               (double)cluster->workers[rank].processing_ns / 1000000000.0);
    fflush(stdout);
    return 0;
}
void distributed_cluster_destroy(distributed_cluster *c) {
    if (c) {
        free(c->workers);
        *c = (distributed_cluster){0};
    }
}
int distributed_run(const distributed_config *c, mpi_transport *t, const char *hostname) {
    if (!t || t->size < 4 || !hostname || image_job_valid(c, 0) < 0) {
        errno = EINVAL;
        return -1;
    }
    if (t->rank != 0)
        return distributed_worker_serve(t, hostname); // Solo rank 0 genera la imagen de la prueba independiente.
    distributed_cluster cluster = {0};
    uint8_t *input = NULL, *output = NULL;
    int result = -1;
    size_t pixels = (size_t)c->width * c->height;
    input = malloc(pixels * 3);
    if (!input) {
        errno = ENOMEM;
        return -1;
    }
    for (uint32_t y = 0; y < c->height; ++y)
        for (uint32_t x = 0; x < c->width; ++x)
            for (uint32_t ch = 0; ch < 3; ++ch)
                input[((size_t)y * c->width + x) * 3 + ch] =
                    (uint8_t)(x * 17u + y * 31u + ch * 71u + (x * y) % 251u); // Genera siempre los mismos pixeles para hacer reproducible la prueba.
    if (distributed_cluster_init(&cluster, t) < 0 ||
        distributed_cluster_process(&cluster, c, input, c->width, c->height, &output, 1) < 0 ||
        distributed_cluster_finish(&cluster) < 0)
        goto cleanup;
    result = 0;
cleanup: {
    int saved = errno;
    crypto_clear(input, pixels * 3);
    crypto_clear(output, pixels * filters_channels(c->filter));
    free(input);
    free(output);
    distributed_cluster_destroy(&cluster);
    errno = saved;
}
    return result;
}
