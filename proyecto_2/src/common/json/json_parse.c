// > Archivo de implementacion de <json.h>
#include "common/json.h"

#include <string.h>
#include <stdio.h>
// =================================================================================================

/* Comparar una vista sin terminador NUL con un nombre del contrato */
static bool text_equals(const char *text, size_t length, const char *name)
{
    return length == strlen(name) && memcmp(text, name, length) == 0;
}

// =================================================================================================

JsonParseResult read_object(
    const char *text,
    size_t length,
    JsonField *fields,
    size_t count
){
    if (text == NULL || fields == NULL || length == 0u)
        return JSON_PARSE_INVALID_ARGUMENT;

    if (JSON_Validate(text, length) != JSONSuccess)
        return JSON_PARSE_INVALID_JSON;

    size_t start = 0u;
    size_t next = 0u;
    JSONPair_t pair = {0};
    JSONStatus_t status;

    while (start < length &&
           (text[start] == ' ' || text[start] == '\t' ||
            text[start] == '\r' || text[start] == '\n')) {
        ++start;
    }

    if (start == length || text[start] != '{' || text[length-1] != '}')
        return JSON_PARSE_INVALID_SCHEMA;

    while ((status = JSON_Iterate(text, length, &start, &next, &pair))
           == JSONSuccess) {
        size_t i;

        for (i = 0u; i < count; ++i) {
            if (text_equals(pair.key, pair.keyLength, fields[i].name))
                break;
        }

        if (i == count || fields[i].pair.value != NULL)
            return JSON_PARSE_INVALID_SCHEMA;

        fields[i].pair = pair;
    }

    if (status != JSONNotFound)
        return JSON_PARSE_INVALID_SCHEMA;

    for (size_t i = 0u; i < count; ++i) {
        if (fields[i].pair.value == NULL)
            return JSON_PARSE_INVALID_SCHEMA;
    }

    return JSON_PARSE_OK;
}

JsonParseResult read_array(
    const char *text,
    size_t length,
    void* values,
    size_t count,
    size_t size,
    JsonType type
){
    if (type == JSON_ARRAY || type == JSON_OBJECT)
        return JSON_PARSE_INVALID_ARGUMENT;

    if (text == NULL || values == NULL || length == 0u)
        return JSON_PARSE_INVALID_ARGUMENT;

    size_t start = 0u;

    while (start < length &&
           (text[start] == ' ' || text[start] == '\t' ||
            text[start] == '\r' || text[start] == '\n')) {
        ++start;
    }

    if (start == length || text[start] != '[' || text[length-1] != ']')
        return JSON_PARSE_INVALID_SCHEMA;

    size_t offset = 0u;
    size_t last = ++start;
    for (; start < length; start++){
        if (text[start] == ',' || start == length-1){
            JsonField temp = {
                .name = NULL, 
                .pair = {
                    .jsonType = JSONNumber,
                    .key = NULL,
                    .keyLength = 0,
                    .value = text + last,
                    .valueLength = start - last
                } 
            };
            switch (type){
                case (JSON_UNSIGNED): {
                    PARSE_TRY( read_uint(&temp, values + offset, size) );
                    break;
                }
                case (JSON_INTEGER): {
                    PARSE_TRY( read_int(&temp, values + offset, size) );
                    break;
                }
                case (JSON_FLOAT): {
                    PARSE_TRY( read_float(&temp, values + offset, size) );
                    break;
                }
                case (JSON_BOOL): {
                    PARSE_TRY( read_bool(&temp, values + offset) );
                    break;
                }
                case (JSON_STRING): {
                    temp.pair.jsonType = JSONString;
                    temp.pair.value += 1; // quitar comillas
                    temp.pair.valueLength -= 2; // quitar comillas
                    JsonString* str = (JsonString*) (values + offset);
                    PARSE_TRY( read_string(&temp, str->string, str->length) );
                    break;
                }
                default: break;
            }
            last = start + 1;
            offset += size;
        }
        if (offset == count) break;
    }

    return JSON_PARSE_OK;
}