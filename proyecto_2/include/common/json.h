#ifndef COMMON_JSON_H
#define COMMON_JSON_H
/*
 * <json.h>: este archivo empaqueta funcionalidades para manipular objetos JSON
 * usando la librería coreJSON sin tener que utilizar FreeRTOS, llamadas de sistema
 * o operaciones en el heap; esto permite compilar estas funcionalidades tanto en
 * sistemas POSIX como en sistemas embebidos.
 *
 * Implementacion: 'src/common/json'
*/
// ===========================================================================
// Includes
// ===========================================================================
// + C libraries
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
// + Third Party libraries
#include "coreJSON/core_json.h"

// ===========================================================================
// Definiciones
// ===========================================================================

#define JSON_FLOATS 6       // cantidad de valores de punto flotante

#define COUNT(array) (sizeof(array) / sizeof((array)[0]))

// ===========================================================================
// Declaraciones y tipos
// ===========================================================================
#ifdef __cplusplus
extern "C" {
#endif
// ---------------------------------------------------------------------------
// + JSON Misc

/* Tipos de datos json permitidos */
typedef enum {
    JSON_STRING,
    JSON_INTEGER,
    JSON_UNSIGNED,
    JSON_FLOAT,
    JSON_BOOL,
    JSON_NULL,
    JSON_OBJECT,
    JSON_ARRAY
} JsonType;

// << Serializacion de objetos >>

/* Par llave-valor en un json */
typedef struct {
    const char *name; // llave del par
    JsonType type; // tipo de dato del valor
    const void *value; // referencia al valor
    size_t size; // cantidad de bytes del valor
} JsonPair;

/* Object json */
typedef struct {
    const JsonPair *pairs; // arreglo de pares
    size_t count; // cantidad de pares
} JsonObject;

/* Arreglo json (solo permite datos del mismo tipo) */
typedef struct {
    const void *data; // arreglo de datos
    JsonType type;// tipo de dato
    size_t count; // cantidad bytes totales
    size_t size;  // bytes por elemento
} JsonArray;

typedef struct {
    char* string;   // puntero al buffer de caracteres
    size_t length;  // largo del buffer de caracteres
} JsonString;

// << Parsing de objetos >>

/* Campo esperado y vista temporal de su valor dentro del string original */
typedef struct {
    const char *name; // llave del par
    JSONPair_t pair;  // par
} JsonField;

// ---------------------------------------------------------------------------
// + JSON Parse

/* Resultados de parsing JSON */
typedef enum {
    JSON_PARSE_OK = 0,
    JSON_PARSE_INVALID_ARGUMENT,
    JSON_PARSE_TOO_LARGE,
    JSON_PARSE_INVALID_JSON,
    JSON_PARSE_INVALID_SCHEMA,
    JSON_PARSE_OUT_OF_RANGE,
    JSON_PARSE_UNKNOWN_TYPE
} JsonParseResult;

/* Propagar el primer error sin publicar un mensaje parcialmente interpretado */
#define PARSE_TRY(operation) do { \
    JsonParseResult result_ = (operation); \
    if (result_ != JSON_PARSE_OK) return result_; \
} while (0)

/*
 * Lee los campos de un objeto representado como string.
 * Todos son obligatorios; rechaza nombres desconocidos o repetidos.
 * Los nombres se comparan literalmente, sin decodificar escapes JSON.
*/
JsonParseResult read_object(
    const char *text,
    size_t length,
    JsonField *fields,
    size_t count
);

/*
 * Lee un campo que corresponde a un arreglo y extrae sus elementos
 * a un buffer. Todos los elementos se interpretan como un mismo tipo.
 * 
*/
JsonParseResult read_array(
    const char *text,
    size_t length,
    void* values,
    size_t count,
    size_t size,
    JsonType type
);

// ---------------------------------------------------------------------------
// + JSON Parse Utils

/* Leer un numero entero con signo */
JsonParseResult read_int(
    const JsonField *field,
    void *value,
    size_t bytes
);

/* Leer un numero entero sin signo */
JsonParseResult read_uint(
    const JsonField *field,
    void *value,
    size_t bytes
);

/* Leer un numero flotante con precision simple o precision doble */
JsonParseResult read_float(
    const JsonField *field,
    void *value,
    size_t bytes
);

JsonParseResult read_bool(
    const JsonField *field,
    bool *value
);

JsonParseResult read_string(
    const JsonField *field,
    char *string,
    size_t capacity
);

// ---------------------------------------------------------------------------
// + JSON Serialize

/* Resulatdos de serializado JSON */
typedef enum {
    JSON_SERIALIZE_OK = 0,
    JSON_SERIALIZE_INVALID_ARGUMENT,
    JSON_SERIALIZE_INVALID_MESSAGE,
    JSON_SERIALIZE_INVALID_JSON,
    JSON_SERIALIZE_INVALID_SCHEMA,
    JSON_SERIALIZE_TOO_LARGE
} JsonSerializeResult;

/* Propagar el primer error sin publicar un objeto parcialmente interpretado */
#define SERIALIZE_TRY(operation) do { \
    JsonSerializeResult result_ = (operation); \
    if (result_ != JSON_SERIALIZE_OK) return result_; \
} while (0)

/* Campo deseado para serializar hacia un string (objeto json) */
typedef struct {
    const char *name;   // nombre/llave
    JsonType type;      // tipo de dato
    const void *value;  // valor
    const int size;     // bytes
    const int length;   // elementos
} JsonSerializeField;

/* Escibrir un objeto JSON con todos sus campos */
JsonSerializeResult write_object(
    const JsonObject *object,
    char *text,
    size_t capacity,
    size_t *written,
    bool entry_point
);

/* Escribir un arreglo JSON con todos sus elementos */
JsonSerializeResult write_array(
    const JsonArray *array,
    char *text,
    size_t capacity,
    size_t* written
);

/* Escribir un par JSON (llave-valor) */
JsonSerializeResult write_pair(
    const JsonPair *pair,
    char *text,
    size_t capacity,
    size_t *written
);

// ---------------------------------------------------------------------------
// + JSON Misc

/* Obtener informacion de debug del error de parsing */
const char* parse_error(JsonParseResult result);

/* Obtener informacion de debug del error de serializacion */
const char* serialize_error(JsonSerializeResult result);

// ---------------------------------------------------------------------------
#ifdef __cplusplus
}
#endif
// ===========================================================================
#endif // COMMON_JSON_H