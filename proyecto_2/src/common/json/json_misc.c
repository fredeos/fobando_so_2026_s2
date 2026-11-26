// Archivo de implementacion de <common/json.h>
#include "common/json.h"

// =================================================================================================

const char* parse_error(JsonParseResult result){
    switch (result){
        case (JSON_PARSE_OK): return "ok";
        case (JSON_PARSE_INVALID_ARGUMENT): return "invalid_argument: argumento dado durante parsing no es valido";
        case (JSON_PARSE_TOO_LARGE): return "too_large: el objeto es muy largo";
        case (JSON_PARSE_INVALID_JSON): return "invalid_json: el objeto tiene un estructura json invalida";
        case (JSON_PARSE_INVALID_SCHEMA): return "invalid_schema: valor con formato inesperado";
        case (JSON_PARSE_OUT_OF_RANGE): return "out_of_range: fuera del rango permitod";
        case (JSON_PARSE_UNKNOWN_TYPE): return "unknown_type: tipo de datos desconocido o no permitido";
    }
    return "";
}

/* Obtener informacion de debug del error de serializacion */
const char* serialize_error(JsonSerializeResult result){
    switch (result){
        case (JSON_SERIALIZE_OK): return "ok";
        case (JSON_SERIALIZE_INVALID_ARGUMENT): return "invalid_argument: argumento dado durante serializado no es valido";
        case (JSON_SERIALIZE_INVALID_MESSAGE): return "invalid_message: el objeto no tiene el formato esperado";
        case (JSON_SERIALIZE_INVALID_JSON): return "invalid_json: el objeto tiene un estructura json invalida";
        case (JSON_SERIALIZE_INVALID_SCHEMA): return "invalid_schema: valor con formato inesperado";
        case (JSON_SERIALIZE_TOO_LARGE): return "too_large: el objeto es muy largo";
    }
    return "";
}