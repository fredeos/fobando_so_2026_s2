#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "host/task_parser.h"

/*
 * Crea un archivo temporal para las pruebas.
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

/*
 * Comprueba que un CSV inválido
 * sea rechazado correctamente.
 */
static void invalid_csv(const char *text)
{
    char path[] = "/tmp/so_bad_tasks_XXXXXX";

    save(path, text);

    TaskList tasks = {0};

    char error[256];

    assert(!task_parser_load(
        path,
        &tasks,
        error,
        sizeof(error)
    ));

    assert(error[0] != '\0');

    assert(
        tasks.items == NULL &&
        tasks.count == 0u
    );

    unlink(path);
}

int main(void)
{
    /*
     * Prueba 1:
     * Cargar tareas válidas.
     */
    char path[] = "/tmp/so_good_tasks_XXXXXX";

    save(
        path,
        "# Ejemplo\n"
        "1,P,40,20,20,2\n"
        "2,S,20,50,50,-1\n"
    );

    TaskList tasks = {0};

    char error[256];

    assert(task_parser_load(
        path,
        &tasks,
        error,
        sizeof(error)
    ));

    assert(tasks.count == 2u);

    assert(tasks.items[0].id == 1u);

    assert(
        tasks.items[1].type == TASK_SPORADIC
    );

    assert(tasks.items[1].criticality == -1);

    /* Liberación de memoria */
    task_list_free(&tasks);

    assert(
        tasks.items == NULL &&
        tasks.count == 0u
    );

    unlink(path);

    /*
     * Prueba 2:
     * Período igual a cero.
     */
    invalid_csv(
        "1,P,40,0,20,2\n"
    );

    /*
     * Prueba 3:
     * Falta un campo.
     */
    invalid_csv(
        "1,P,40,20,20\n"
    );

    /*
     * Prueba 4:
     * Campo adicional.
     */
    invalid_csv(
        "1,P,40,20,20,2,EXTRA\n"
    );

    /*
     * Prueba 5:
     * Identificadores duplicados.
     */
    invalid_csv(
        "1,P,40,20,20,2\n"
        "1,S,20,50,50,3\n"
    );

    /*
     * Prueba 6:
     * Número fuera del rango uint32_t.
     */
    invalid_csv(
        "1,P,4294967296,20,20,2\n"
    );

    /*
     * Prueba 7:
     * Campo de trabajo vacío.
     */
    invalid_csv(
        "1,P,,20,20,2\n"
    );

    puts("OK: task parser tests");

    return 0;
}