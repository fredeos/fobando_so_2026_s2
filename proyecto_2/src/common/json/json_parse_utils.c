// > Archivo de implementacion de <json.h>
#include "common/json.h"

#include <stdio.h>
#include <string.h>
// =================================================================================================

/* Represencion de un numero entero */
typedef struct {
    bool negative;
    uint64_t integer;
    uint64_t fraction;
} real_t;

/* Leer un numero real */
static JsonParseResult read_real(
    const char *text,
    size_t length,
    real_t* number 
){
    uint64_t integer  = 0u;
    uint64_t fraction = 0u;

    if (text == NULL || number == NULL)
        return JSON_PARSE_INVALID_ARGUMENT;

    if (length == 0u)
        return JSON_PARSE_INVALID_SCHEMA;

    bool on_frac = false;
    bool negative = false;
    for (size_t i = 0u; i < length; i++){
        char c = text[i];

        if (c == '-'){
            negative = true;
            continue;
        }

        if (c == '.'){
            on_frac = true;
            continue;
        }

        if (c < '0' || c > '9')
            return JSON_PARSE_INVALID_SCHEMA;

        unsigned digit = (unsigned)(c - '0');
        if (on_frac) fraction = fraction * 10u + digit;
        else integer = integer * 10u + digit;
    }

    number->negative = negative;
    number->integer = integer;
    number->fraction =fraction;

    return JSON_PARSE_OK;
}

// =================================================================================================

JsonParseResult read_int(
    const JsonField *field,
    void *value,
    size_t bytes
){
    if (field == NULL || value == NULL)
        return JSON_PARSE_INVALID_ARGUMENT;
    if (field->pair.jsonType != JSONNumber)
        return JSON_PARSE_INVALID_SCHEMA;

    real_t number;
    PARSE_TRY( read_real(field->pair.value, field->pair.valueLength, &number) );
    switch (bytes){
        case 1: {
            if (number.integer > INT8_MAX)
                return JSON_PARSE_INVALID_SCHEMA;

            int8_t* ptr = (int8_t*) value;
            int64_t integer = (int64_t) number.integer;
            *ptr = (int8_t) (number.negative ? -integer : integer);
            break;
        }
        case 2: {
            if (number.integer > INT16_MAX)
                return JSON_PARSE_INVALID_SCHEMA;

            int16_t* ptr = (int16_t*) value;
            int64_t integer = (int64_t) number.integer;
            *ptr = (int16_t) (number.negative ? -integer : integer);
            break;
        }
        case 4: {
            if (number.integer > INT32_MAX)
                return JSON_PARSE_INVALID_SCHEMA;

            int32_t* ptr = (int32_t*) value;
            int64_t integer = (int64_t) number.integer;
            *ptr = (int32_t) (number.negative ? -integer : integer);
            break;
        }
        case 8: {
            if (number.integer > INT64_MAX)
                return JSON_PARSE_INVALID_SCHEMA;

            int64_t* ptr = (int64_t*) value;
            int64_t integer = (int64_t) number.integer;
            *ptr = number.negative ? -integer : integer;
            break;
        }
        default: break;
    }
    return JSON_PARSE_OK;
}

JsonParseResult read_uint(
    const JsonField *field,
    void *value,
    size_t bytes
){
    if (field == NULL || value == NULL)
        return JSON_PARSE_INVALID_ARGUMENT;
    if (field->pair.jsonType != JSONNumber)
        return JSON_PARSE_INVALID_SCHEMA;

    real_t number;
    PARSE_TRY( read_real(field->pair.value, field->pair.valueLength, &number) );
    switch (bytes){
        case 1: {
            if (number.integer > UINT8_MAX)
                return JSON_PARSE_INVALID_SCHEMA;

            uint8_t* ptr = (uint8_t*) value;
            int64_t integer = (int64_t) number.integer;
            *ptr = (uint8_t) (number.negative ? -integer : integer);
            break;
        }
        case 2: {
            if (number.integer > UINT16_MAX)
                return JSON_PARSE_INVALID_SCHEMA;

            uint16_t* ptr = (uint16_t*) value;
            int64_t integer = (int64_t) number.integer;
            *ptr = (uint16_t) (number.negative ? -integer : integer);
            break;
        }
        case 4: {
            if (number.integer > UINT32_MAX)
                return JSON_PARSE_INVALID_SCHEMA;

            uint32_t* ptr = (uint32_t*) value;
            int64_t integer = (int64_t) number.integer;
            *ptr = (uint32_t) (number.negative ? -integer : integer);
            break;
        }
        case 8: {
            if (number.integer > UINT64_MAX)
                return JSON_PARSE_INVALID_SCHEMA;

            uint64_t* ptr = (uint64_t*) value;
            int64_t integer = (int64_t) number.integer;
            *ptr = (uint64_t) (number.negative ? -integer : integer);
            break;
        }
        default: break;
    }
    return JSON_PARSE_OK;
}

JsonParseResult read_float(
    const JsonField *field,
    void *value,
    size_t bytes
){
    if (field == NULL || value == NULL)
        return JSON_PARSE_INVALID_ARGUMENT;
    if (field->pair.jsonType != JSONNumber)
        return JSON_PARSE_INVALID_SCHEMA;

    real_t number;
    PARSE_TRY( read_real(field->pair.value, field->pair.valueLength, &number) );

    double fp = (double) number.fraction;
    while (fp >= 1.0f) fp /= 10;
    fp = (double) number.integer + fp;

    switch(bytes){
        case 4: { // float => 4 bytes (simple precision)
            float* ptr = (float*) value;
            *ptr = (float) fp;
            break;
        }
        case 8: { // double => 8 bytes (double precision)
            double* ptr = (double*) value;
            *ptr = fp;
            break;
        }
        default: break;
    }

    return JSON_PARSE_OK;
}

JsonParseResult read_bool(
    const JsonField *field,
    bool *value
){
    if (field == NULL || value == NULL)
        return JSON_PARSE_INVALID_ARGUMENT;

    if (field->pair.jsonType == JSONTrue) *value = true;
    else if (field->pair.jsonType == JSONFalse) *value = false;
    else {
        char temp[8] = {0};
        memcpy(temp, field->pair.value, field->pair.valueLength);
        if      (strcmp(temp, "true")  == 0) return *value = true;
        else if (strcmp(temp, "false") == 0) return *value = false;
        else return JSON_PARSE_INVALID_SCHEMA;
    }

    return JSON_PARSE_OK;
}

JsonParseResult read_string(
    const JsonField *field,
    char *string,
    size_t capacity
){
    if (field == NULL || string == NULL || capacity == 0u)
        return JSON_PARSE_INVALID_ARGUMENT;

    const char* text = field->pair.value;
    size_t length = field->pair.valueLength;

    if (text == NULL || length == 0u)
        return JSON_PARSE_INVALID_SCHEMA;

    size_t start = 0u;

    while (start < capacity &&
           (text[start] == ' ' || text[start] == '\t' ||
            text[start] == '\r' || text[start] == '\n')) {
        ++start;
    }

    if (start == length)
        return JSON_PARSE_INVALID_SCHEMA;

    for (size_t i = 0; i < capacity; i++){
        char c = text[start++];
        if (start <= length)
            string[i] = c;
        else
            break;
    }

    return JSON_PARSE_OK;
}