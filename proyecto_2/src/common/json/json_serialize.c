// > Archivo de implementacion de <json.h>
#include "common/json.h"

// + C libraries
#include <string.h>
#include <stdio.h>

// =================================================================================================

/* Contar los digitos de un entero (usando la maxima capacidad posible) */
static size_t check_int_digits(const void* value, size_t bytes){
    int64_t number = 0;
    
    switch (bytes){
        case 1:{
            int8_t* ptr = (int8_t*) value;
            number = (int64_t) (*ptr);
            break;
        }
        case 2:{
            int16_t* ptr = (int16_t*) value;
            number = (int64_t) (*ptr);
            break;
        }
        case 4:{
            int32_t* ptr = (int32_t*) value;
            number = (int64_t) (*ptr);
            break;
        }
        case 8:{
            int64_t* ptr = (int64_t*) value;
            number = *ptr;
            break;
        }
    }

    return snprintf(NULL, 0, "%lld", number);
}

/* Contar los digitos de un entero sin signo (usando la maxima capacidad posible) */
static size_t check_uint_digits(const void* value, size_t bytes){
    uint64_t number = 0;
    
    switch (bytes){
        case 1:{
            uint8_t* ptr = (uint8_t*) value;
            number = (uint64_t) (*ptr);
            break;
        }
        case 2:{
            uint16_t* ptr = (uint16_t*) value;
            number = (uint64_t) (*ptr);
            break;
        }
        case 4:{
            uint32_t* ptr = (uint32_t*) value;
            number = (uint64_t) (*ptr);
            break;
        }
        case 8:{
            uint64_t* ptr = (uint64_t*) value;
            number = *ptr;
            break;
        }
    }
    
    return snprintf(NULL, 0, "%llu", number);
}

/* Contar los digitos de un flotante (usando la maxima capacidad posible) */
static size_t check_float_digits(const void* value, size_t bytes){
    size_t digits = 0; // contabilizar parte fraccionario

    switch (bytes){
        case 4: {
            float* ptr = (float*) value;
            digits = snprintf(NULL, 0, "%.*f", JSON_FLOATS, *ptr);
            break;
        }
        case 8: {
            double* ptr = (double*) value;
            digits = snprintf(NULL, 0, "%.*f", JSON_FLOATS, *ptr);
            break;
        }
        default: break;
    }

    return digits;
}

/* 
 * Revisar la cantidad de caracteres que puede consumir un par JSON 
 * Aplica recursividad para explorar a profundidad estructuras anidadas
*/
static size_t pair_max_chars (const JsonPair* pair){
    // << Llave >>
    size_t key_len = strlen(pair->name);
    size_t size = (key_len == 0) ? 0 : key_len + 2; // contabilizar longitud de la llave + comillas
    // << Valor >>
    switch (pair->type){
        case (JSON_STRING): {
            JsonString* str = (JsonString*) pair->value;
            size += str->length + 2; // contabilizar comillas
            break;
        }
        case (JSON_INTEGER): {
            size += check_int_digits(pair->value, pair->size);
            break;
        }
        case (JSON_UNSIGNED): {
            size += check_uint_digits(pair->value, pair->size);
            break;
        }
        case (JSON_FLOAT): {
            size += check_float_digits(pair->value, pair->size);
            break;
        }
        case (JSON_BOOL): {
            bool* ptr = (bool*) pair->value;
            if (*ptr) size += 4; // true
            else size += 5; // false
            break;
        }
        case (JSON_NULL): {
            size += 4;
            break;
        }
        case (JSON_OBJECT): {
            size += 2; // contabilizar corchetes {}
            JsonObject* obj = (JsonObject*) pair->value;
            for (size_t i = 0; i < obj->count; i++){
                size += pair_max_chars(&obj->pairs[i]);
                if (i < obj->count-1) size++; // contabilizar ','
            }
            break;
        }
        case (JSON_ARRAY): {
            size += 2; // contabilizar parentesis cuadrados []
            JsonArray* arr = (JsonArray*) pair->value;
            for (size_t i = 0; i < arr->count; i += arr->size){
                JsonPair pair = {.name = "", .value = (arr->data + i), .size = arr->size, .type = arr->type};
                size += pair_max_chars(&pair);
                if (i < arr->count - arr->size) size++; // contabilizar ','
            }
            break;
        }
    }
    // << Retorno >>
    if (key_len > 0) size++; // contabilizar separador ':'
    return size;
}

// =================================================================================================

/* Escribir un numero entero con signo segun su cantidad de bytes*/
static int write_int(const void* value, size_t bytes, char* buffer, size_t n){
    int64_t number = 0;
    
    switch (bytes){
        case 1:{
            int8_t* ptr = (int8_t*) value;
            number = (int64_t) (*ptr);
            break;
        }
        case 2:{
            int16_t* ptr = (int16_t*) value;
            number = (int64_t) (*ptr);
            break;
        }
        case 4:{
            int32_t* ptr = (int32_t*) value;
            number = (int64_t) (*ptr);
            break;
        }
        case 8:{
            int64_t* ptr = (int64_t*) value;
            number = *ptr;
            break;
        }
    }

    return snprintf(buffer, n, "%lld", number);
}

/* Escribir un numero entero sin signo segun su cantidad de bytes*/
static int write_uint(const void* value, size_t bytes, char* buffer, size_t n){
    uint64_t number = 0;

    switch (bytes){
        case 1:{
            uint8_t* ptr = (uint8_t*) value;
            number = (uint64_t) (*ptr);
            break;
        }
        case 2:{
            uint16_t* ptr = (uint16_t*) value;
            number = (uint64_t) (*ptr);
            break;
        }
        case 4:{
            uint32_t* ptr = (uint32_t*) value;
            number = (uint64_t) (*ptr);
            break;
        }
        case 8:{
            uint64_t* ptr = (uint64_t*) value;
            number = *ptr;
            break;
        }
    }

    return snprintf(buffer, n, "%llu", number);
}

/* Escribir un numero flotante segun su cantidad de bytes para precision simple y precision doble */
static int write_float(const void* value, size_t bytes, char* buffer, size_t n){
    int wd = 0;
    switch (bytes){
        case 4: { // float => 4 bytes (precision simple)
            float* ptr = (float*) value;
            wd = snprintf(buffer, n, "%.*f", JSON_FLOATS, *ptr);
            break;
        }
        case 8: { // double => 8 bytes (precision doble)
            double* ptr = (double*) value;
            wd = snprintf(buffer, n, "%.*f", JSON_FLOATS, *ptr);
            break;
        }
    }
    return wd;
}

// =================================================================================================

JsonSerializeResult write_object(
    const JsonObject *object,
    char *text,
    size_t capacity,
    size_t *written,
    bool entry_point
){
    if (text == NULL || object == NULL || capacity == 0u || written == NULL)
        return JSON_SERIALIZE_INVALID_ARGUMENT;
    if (object->pairs == NULL && object->count != 0u)
        return JSON_SERIALIZE_INVALID_SCHEMA;

    size_t expected = 0;
    if (entry_point) {
        JsonPair parent = {"", JSON_OBJECT, object, sizeof(*object)};
        expected = pair_max_chars(&parent);
        if (expected > capacity - 1)
            return JSON_SERIALIZE_TOO_LARGE;
    };

    text[(*written)++] = '{';
    for (size_t i = 0; i < object->count; i++){
        SERIALIZE_TRY( write_pair(&object->pairs[i], text, capacity, written) );
        if (i < object->count-1) text[(*written)++] = ',';
    }
    text[(*written)++] = '}';

    if (entry_point && *written != expected)
        return JSON_SERIALIZE_INVALID_JSON;

    return JSON_SERIALIZE_OK;
}

// =================================================================================================

JsonSerializeResult write_array(
    const JsonArray *array,
    char *text,
    size_t capacity,
    size_t* written
){
    if (text == NULL || array == NULL || capacity == 0u || written == NULL)
        return JSON_SERIALIZE_INVALID_ARGUMENT;
    if (array->data == NULL && array->count != 0u)
        return JSON_SERIALIZE_INVALID_SCHEMA;
    if (array->count != 0u && array->size == 0u)
        return JSON_SERIALIZE_INVALID_SCHEMA;

    text[(*written)++] = '[';
    for (size_t i = 0; i < array->count; i += array->size){
        JsonPair pair = {.name = "", .value = (array->data + i), .size = array->size, .type = array->type};
        SERIALIZE_TRY( write_pair(&pair, text, capacity, written) );
        if (i < array->count - array->size) text[(*written)++] = ',';
    }
    text[(*written)++] = ']';
    
    return JSON_SERIALIZE_OK;
}

// =================================================================================================

JsonSerializeResult write_pair(
    const JsonPair *pair,
    char *text,
    size_t capacity,
    size_t *written
){
    if (text == NULL || pair == NULL || written == NULL)
        return JSON_SERIALIZE_INVALID_ARGUMENT;
    if (pair->name == NULL || pair->value == NULL || pair->size == 0u)
        return JSON_SERIALIZE_INVALID_SCHEMA;
    
    // << Llave >>
    size_t key_len = strlen(pair->name);
    if (key_len > 0){
        int wd = snprintf(text + (*written), capacity - (*written), "\"%s\":", pair->name);
        if (wd < 0) return JSON_SERIALIZE_INVALID_SCHEMA;
        (*written) += wd;
    }
    // << Valor >>
    switch (pair->type){
        case (JSON_STRING): {
            JsonString* str = (JsonString*) pair->value;
            int wd = snprintf(text + (*written), capacity - (*written), "\"%s\"", str->string);
            if (wd < 0) return JSON_SERIALIZE_INVALID_SCHEMA;
            (*written) += wd;
            break;
        }
        case (JSON_INTEGER): {
            int wd = write_int(pair->value, pair->size, text + (*written), capacity - (*written));
            if (wd < 0) return JSON_SERIALIZE_INVALID_SCHEMA;
            (*written) += wd;
            break;
        }
        case (JSON_UNSIGNED): {
            int wd = write_uint(pair->value, pair->size, text + (*written), capacity - (*written));
            if (wd < 0) return JSON_SERIALIZE_INVALID_SCHEMA;
            (*written) += wd;
            break;
        }
        case (JSON_FLOAT): {
            int wd = write_float(pair->value, pair->size, text + (*written), capacity - (*written));
            if (wd < 0) return JSON_SERIALIZE_INVALID_SCHEMA;
            (*written) += wd;
            break;
        }
        case (JSON_BOOL): {
            bool* flag = (bool*) pair->value;
            int wd = 0;
            if (*flag) wd = snprintf(text + (*written), capacity - (*written), "true");
            else       wd = snprintf(text + (*written), capacity - (*written), "false");
            if (wd < 0) return JSON_SERIALIZE_INVALID_SCHEMA;
            (*written) += wd;
            break;
        }
        case (JSON_NULL): {
            int wd = snprintf(text + *written, capacity, "null");
            if (wd < 0) return JSON_SERIALIZE_INVALID_SCHEMA;
            (*written) += wd;
            break;
        }
        case (JSON_OBJECT): {
            JsonObject* object = (JsonObject*) pair->value;
            SERIALIZE_TRY( write_object(object, text, capacity, written, false) );
            break;
        }
        case (JSON_ARRAY): {
            JsonArray* array = (JsonArray*) pair->value;
            SERIALIZE_TRY( write_array(array, text, capacity, written) );
            break;
        }
    }

    return JSON_SERIALIZE_OK;
}