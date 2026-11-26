#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "common/json.h"
#include "client/header.h"

#define BUFFER_SIZE 1024

#define PARSE(operation) do { \
    JsonParseResult result_ = (operation); \
    if (result_ != JSON_PARSE_OK) { \
        printf("json_parse(): %s\n", parse_error(result_)); \
        return 1; \
    } \
} while (0)

int main(int argc, char *argv[]){
    if (argc > 1 && !(argc == 2 && strcmp(argv[1], "--json-demo") == 0)) {
        return tcp_client_run(argc, argv); // Integra TCP/MPI conservando la demo JSON de p2.
    }

    /* Estructura del json
     * {
     * "mensaje":"hola",
     * "x": -77,
     * "y": 3.14789190167,
     * "flag": false,
     * "obj": {
     *      "nombre": "Frederick",
     *      "edad": 21,
     *      "strings": ["xy", "xz", "xyz"]
     *  },
     *  "arr1": [3, 4, 5, 6, 7],
     *  "arr2": [2.1, 1.5, 3.4901]
     * }
    */

    // =========================================================================
    // Serializar un objeto JSON
    // =========================================================================
    /* Para crear un objecto JSON es necesario crear un los campos desde adentro hacia afuera */
    char serialized[BUFFER_SIZE];
    // 1. Crear lista de strings
    char string1[] = "xy";
    char string2[] = "xz";
    char string3[] = "xyz";

    JsonString strings_arr[] = {
        {.string = string1, .length = strlen(string1)},
        {.string = string2, .length = strlen(string2)},
        {.string = string3, .length = strlen(string3)}
    };

    JsonArray strings = {.data = strings_arr, .count = sizeof(strings_arr), .size = sizeof(strings_arr[0]), .type = JSON_STRING};
    // 2. Crear objeto anidado
    const char nombre[] = "Frederick";
    uint8_t edad = 21;

    JsonString nombre_str = {.string = nombre, .length = strlen(nombre)};
    JsonPair nested_pairs[] = {
        {.name = "nombre", .value = &nombre_str, .size = sizeof(nombre_str), .type = JSON_STRING},
        {.name = "edad", .value = &edad, .size = sizeof(edad), .type = JSON_UNSIGNED},
        {.name = "strings", .value = &strings, .size = sizeof(strings), .type = JSON_ARRAY}
    };
    JsonObject nested = {.pairs = nested_pairs, .count = COUNT(nested_pairs)};
    // 3. Crear arreglos de datos 1 y 2
    uint16_t array1[] = {3, 4, 5, 6, 7};
    JsonArray arr1 = {.data = array1, .count = sizeof(array1), .size = sizeof(array1[0]), .type = JSON_UNSIGNED};

    float array2[] = {2.1, 1.5, 3.4901};
    JsonArray arr2 = {.data = array2, .count = sizeof(array2), .size = sizeof(array2[0]), .type = JSON_FLOAT};
    // 4. Crear objecto completo
    const char mensaje[] = "hola";
    int32_t x = -77;
    double y = 3.14789190167;
    bool flag = false;

    JsonString mensaje_str = {.string = mensaje, .length = strlen(mensaje)};
    JsonPair pairs[] = {
        {.name = "mensaje", .value = &mensaje_str, .size = sizeof(mensaje_str), .type = JSON_STRING},
        {.name = "x", .value = &x, .size = sizeof(x), .type = JSON_INTEGER},
        {.name = "y", .value = &y, .size = sizeof(y), .type = JSON_FLOAT},
        {.name = "flag", .value = &flag, .size = sizeof(flag), .type = JSON_BOOL},
        {.name = "obj", .value = &nested, .size = sizeof(nested), .type = JSON_OBJECT},
        {.name = "arr1", .value = &arr1, .size = sizeof(arr1), .type = JSON_ARRAY},
        {.name = "arr2", .value = &arr2, .size = sizeof(arr2), .type = JSON_ARRAY}
    };
    JsonObject object = {.pairs = pairs, .count = COUNT(pairs)};
    // 5. Seralizar objeto y verificar la salida
    size_t written = 0;
    JsonSerializeResult serialize = write_object(&object, serialized, sizeof(serialized), &written, true);
    if (serialize == JSON_SERIALIZE_OK){
        serialized[written] = '\0';
        printf("Serializado: %s\n", serialized);
    } else {
        printf("json_serialize(): %s\n", serialize_error(serialize));
    }

    // =========================================================================
    // Parseo del objeto JSON creado
    // =========================================================================
    /* Para extraer datos de un objeto JSON se hace el parseo de afuera hacia adentro */
    printf("Parseando mensaje\n");
    // 1. Instancia todos los datos que se desean extraer
    char rd_message[10] = { 0 };
    int16_t rd_x = 0;
    float rd_y = 0.0f;
    bool rd_flag = true;

    char rd_nombre[20] = { 0 };
    uint32_t rd_edad = 0;
    char rd_string1[5] = { 0 }; char rd_string2[5] = { 0 }; char rd_string3[6] = { 0 };
    JsonString rd_strings[] = {
        {.string = rd_string1, .length = sizeof(rd_string1)},
        {.string = rd_string2, .length = sizeof(rd_string2)},
        {.string = rd_string3, .length = sizeof(rd_string3)}
    };

    int8_t rd_arr1[5] = { 0 };
    double rd_arr2[3] = { 0 };
    // 2. Definir la estructura esperada del objeto exterior
    JsonField fields[] = {
        {.name = "mensaje", .pair = { 0 }},
        {.name = "x", .pair = { 0 }},
        {.name = "y", .pair = { 0 }},
        {.name = "flag", .pair = { 0 }},
        {.name = "obj", .pair = { 0 }},
        {.name = "arr1", .pair = { 0 }},
        {.name = "arr2", .pair = { 0 }}
    };

    PARSE( read_object(serialized, strlen(serialized), fields, COUNT(fields)) );
    // 3. Obtener para estructuras inmediatas
    PARSE( read_string(&fields[0], rd_message, sizeof(rd_message)) );
    PARSE( read_int(&fields[1], &rd_x, sizeof(rd_x)) );
    PARSE( read_float(&fields[2], &rd_y, sizeof(rd_y)) );
    PARSE( read_bool(&fields[3], &rd_flag) );

    printf("mensaje = %s\n", rd_message);
    printf("x = %d\n", rd_x);
    printf("y = %.8f\n", rd_y);
    printf("flag = %d\n", rd_flag);
    // 4. Definir estructura de objecto anidado interno
    JsonField nested_fields[] = {
        {.name = "nombre", .pair = { 0 }},
        {.name = "edad", .pair = { 0 }},
        {.name = "strings", .pair = { 0 }}
    };

    PARSE ( read_object(fields[4].pair.value, fields[4].pair.valueLength, nested_fields, COUNT(nested_fields)) );
    // 5. Obtener valores de estrutura anidada
    PARSE ( read_string(&nested_fields[0], rd_nombre, sizeof(rd_nombre)));
    PARSE ( read_uint(&nested_fields[1], &rd_edad, sizeof(rd_edad)));
    PARSE ( read_array(
        nested_fields[2].pair.value, 
        nested_fields[2].pair.valueLength, 
        rd_strings, 
        sizeof(rd_strings), 
        sizeof(rd_strings[0]), 
        JSON_STRING
        ) 
    );

    printf("nombre: %s\n", rd_nombre);
    printf("edad: %u\n", rd_edad);
    printf("strings: [1]%s [2]%s [3]%s \n", rd_string1, rd_string2, rd_string3);
    // 6. Obtener valores de los arreglos
    PARSE ( read_array(fields[5].pair.value, fields[5].pair.valueLength, rd_arr1, sizeof(rd_arr1), sizeof(rd_arr1[0]), JSON_INTEGER) );
    PARSE ( read_array(fields[6].pair.value, fields[6].pair.valueLength, rd_arr2, sizeof(rd_arr2), sizeof(rd_arr2[0]), JSON_FLOAT) );

    printf("[");
    for (int i = 0; i < 5; i++){
        printf("%d", rd_arr1[i]);
        if (i < 4) printf(", ");
    }
    printf("]\n");

    printf("[");
    for (int i = 0; i < 3; i++){
        printf("%.2f", rd_arr2[i]);
        if (i < 2) printf(", ");
    }
    printf("]\n");
    // final del programa
    return 0;
}