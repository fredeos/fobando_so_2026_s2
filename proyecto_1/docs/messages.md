# Contenido de mensajes JSON

## Alcance

`common/message_parser.h` interpreta un string completo y su longitud exacta.
El mismo código se comparte entre Linux, ESP32 y Raspberry Pi Pico. Reutiliza
`MessageType`, `Task`, `Event` y `RejectReason` ya definidos en el proyecto.

Esta es la propuesta de contrato de contenido para conectar los módulos que
están en desarrollo. El módulo no abre puertos, recibe tramas, verifica CRC,
envía respuestas ni ejecuta órdenes. El protocolo entrega el contenido completo;
el consumidor llama a `message_parse(...)` y decide qué hacer con el resultado.
Tampoco se incluye un generador de JSON: quien envía construye el contenido
según este contrato y entrega sus bytes al protocolo.

Se utiliza **coreJSON v3.3.1**, de FreeRTOS, con licencia MIT. Es portable en C,
no requiere ejecutar FreeRTOS ni usa memoria dinámica. Las fuentes originales
están incluidas en `third_party/coreJSON`; no hay que instalar una biblioteca
distinta en cada plataforma. Referencias:
[repositorio](https://github.com/FreeRTOS/coreJSON/tree/v3.3.1) y
[documentación](https://freertos.github.io/coreJSON/v3.3.1/).

## Sobre común

```json
{"type":"START","node_id":1,"request_id":7,"data":{}}
```

| Campo | Significado |
|---|---|
| `type` | Nombre literal del tipo, sin el prefijo `MSG_`. |
| `node_id` | Identificador `uint32_t` del nodo destinatario o emisor. |
| `request_id` | Identificador `uint32_t` de la solicitud. ACK/NACK deben copiarlo. |
| `data` | Objeto con los campos específicos del tipo. Siempre está presente. |

Los cuatro campos son obligatorios. El orden y el espacio en blanco JSON son
libres. Los IDs admiten cero, igual que los tipos existentes del proyecto.
Se recomienda reservar `request_id = 0` para eventos y reportes espontáneos;
la generación de IDs y la asociación de respuestas son responsabilidad del driver.
El parser solo comprueba el contenido: no conoce solicitudes pendientes ni
verifica si un mensaje corresponde al nodo conectado.

## Contenido por tipo

Cada fila describe **todos** los campos obligatorios de `data`.

| `type` | Campos de `data` |
|---|---|
| `CONFIG` | `algorithm`: `"RMS"` o `"EDF"`; `tick_ms` y `report_every_ticks`: `uint32_t > 0`. |
| `CALIBRATE_REQUEST` | `sample_ticks`: `uint32_t > 0`, cantidad de ticks de la medición. |
| `CALIBRATE_RESPONSE` | `ut_per_tick`: `uint32_t > 0`, capacidad calibrada. |
| `DEPLOY_TASK` | `task`: objeto descrito abajo; `cost_ticks`: `uint32_t > 0`, costo calculado por el Host. |
| `START` | Objeto vacío `{}`. |
| `EVENT` | `event_type`: número de `EventType` (1–15); `task_id`, `job_id`, `duration_ticks`: `uint32_t`; `tick`: `uint64_t`; `detail`: `int32_t`. |
| `TICK_REPORT` | `tick`: `uint64_t`; `task_id`: `uint32_t`, tarea informada por el nodo. |
| `TRIGGER_SPORADIC` | `task_id`: `uint32_t`. |
| `STOP` | Objeto vacío `{}`. |
| `ACK` | `message_type`: nombre del tipo de la solicitud respondida. |
| `NACK` | `message_type`: nombre del tipo de la solicitud respondida; `reason`: número de `RejectReason` (1–6). |

`MessageAlgorithm` permite interpretar la configuración en los nodos sin incluir
cabeceras del Host. El consumidor debe traducirlo explícitamente a su algoritmo
local; el parser no inicia el planificador ni calibra el dispositivo.

Una tarea utiliza los mismos nombres y restricciones básicas de `Task`:

```json
{"id":8,"type":"P","work":40,"period":20,"deadline":15,"criticality":-1}
```

- `id`: `uint32_t`.
- `type`: `"P"` para periódica o `"S"` para esporádica.
- `work`, `period`, `deadline`: `uint32_t > 0`.
- `criticality`: `int32_t`, admite valores negativos.

Ejemplo completo de despliegue:

```json
{"type":"DEPLOY_TASK","node_id":1,"request_id":12,"data":{"task":{"id":8,"type":"P","work":40,"period":20,"deadline":15,"criticality":-1},"cost_ticks":3}}
```

La validación de admisión, IDs de tareas repetidos, capacidad del nodo y
consistencia de `cost_ticks` con la calibración corresponde a los módulos
existentes del Host y al ejecutor del nodo.

## Eventos, reportes y respuestas

```json
{"type":"EVENT","node_id":1,"request_id":0,"data":{"event_type":5,"task_id":8,"job_id":3,"tick":99,"duration_ticks":2,"detail":0}}
```

En `Message.data.event`, el parser copia `node_id` del sobre y establece
`source = EVENT_SOURCE_NODE`. `host_time_ns`, `cost_ticks`, `ut_per_tick` y
`task` quedan en cero: el driver puede completarlos desde su reloj, calibración
y tabla de tareas antes de publicar el evento por IPC. `detail` mantiene el
significado definido por cada evento. Para representar inactividad se utiliza
`EVENT_IDLE_SLICE`; el parser no reserva un ID de tarea como indicador de idle.

```json
{"type":"TICK_REPORT","node_id":1,"request_id":0,"data":{"tick":100,"task_id":8}}
```

El tick no se convierte a `double`, por lo que se conservan enteros mayores a
`2^53`, hasta `UINT64_MAX`.

```json
{"type":"ACK","node_id":1,"request_id":12,"data":{"message_type":"DEPLOY_TASK"}}
```

```json
{"type":"NACK","node_id":1,"request_id":12,"data":{"message_type":"DEPLOY_TASK","reason":3}}
```

Para ACK, `Message.data.reply.reason` queda en `REJECT_NONE`. NACK utiliza los
motivos existentes de rechazo: tarea inválida (1), duplicada (2), capacidad
insuficiente (3), límite de esporádicas (4), nodo no disponible (5) o falla del
nodo de una esporádica (6). No se inventa otro catálogo de errores de aplicación.
Los errores de parseo se informan mediante `MessageParseResult`; el llamador
decide la respuesta apropiada si puede identificar la solicitud.

Estos ACK/NACK son mensajes de aplicación. Su contenido confirma o rechaza una
orden; cualquier confirmación de recepción de bytes pertenece al transporte.
Por ejemplo, el driver puede confirmar una reserva manual con
`manual_task_confirm(...)` después de recibir el ACK de `DEPLOY_TASK` y verificar
que `node_id`, `request_id` y `message_type` coinciden con la solicitud pendiente.

## Uso desde C

```c
#include "common/message_parser.h"

void interpretar(const char *text, size_t length)
{
    Message message;
    MessageParseResult result = message_parse(text, length, &message);

    if (result != MESSAGE_PARSE_OK) {
        /* Registrar message_parse_result_string(result) si corresponde. */
        return;
    }

    switch (message.type) {
        case MSG_DEPLOY_TASK:
            /* message.data.deploy_task.task contiene la definición Task. */
            /* message.data.deploy_task.cost_ticks contiene el costo. */
            break;
        case MSG_STOP:
            /* El consumidor solicita detener su ejecución. */
            break;
        default:
            break;
    }
}
```

El buffer de entrada no necesita terminar en `\0` y puede ser de solo lectura.
`length` excluye el terminador, cabeceras, CRC y relleno. El parser no modifica
la entrada ni conserva punteros hacia ella; se puede reutilizar el buffer
después de la llamada. No hay memoria que liberar. Entrada y salida deben
estar en regiones independientes. Ante cualquier error, `out` queda intacto;
el llamador solo debe utilizarlo cuando el resultado sea `MESSAGE_PARSE_OK`.

## Restricciones y errores

- Máximo **256 bytes**, incluido cualquier espacio JSON. El límite se define
  en `MESSAGE_MAX_BYTES`, sin incluir cabeceras ni CRC. Coincide con la capacidad
  de payload actualmente declarada, sin depender de `protocol.h`. Si cambia
  esa capacidad, se deben revisar ambos límites. Un mensaje de exactamente
  256 bytes no necesita espacio adicional para NUL. Los emisores deben medir
  el resultado: números largos y espacios también consumen capacidad.
- Un único objeto JSON completo por llamada. Las entradas parciales, bytes
  sobrantes, NUL embebido y documentos concatenados se rechazan. Corresponde
  al transporte reconstruir el string completo antes de invocar el parser.
- Se rechazan campos faltantes, desconocidos y repetidos en cualquier objeto.
- Los nombres y valores de enumeraciones se escriben literalmente, con las
  mayúsculas mostradas. No se aceptan variantes con escapes como `"ST\u0041RT"`.
- Los números son enteros decimales JSON. No se admiten booleanos, strings
  numéricos, fracciones ni exponentes. Los campos sin signo rechazan `-0`.
  Se detectan desbordamientos de 32 y 64 bits antes de convertirlos.
- No se convierten structs a bytes de memoria: el formato de intercambio es
  el JSON descrito, independiente del padding o endianness de cada equipo.

| Resultado | Motivo |
|---|---|
| `MESSAGE_PARSE_OK` | Mensaje interpretado. |
| `MESSAGE_PARSE_INVALID_ARGUMENT` | Puntero nulo o longitud cero. |
| `MESSAGE_PARSE_TOO_LARGE` | Más de `MESSAGE_MAX_BYTES`. |
| `MESSAGE_PARSE_INVALID_JSON` | Documento inválido, incompleto o demasiado anidado para coreJSON. |
| `MESSAGE_PARSE_INVALID_SCHEMA` | Objeto/campo faltante, extra, repetido, tipo incorrecto o formato de entero no admitido. |
| `MESSAGE_PARSE_OUT_OF_RANGE` | Desbordamiento, cero donde se exige un positivo o enum numérico fuera de rango. |
| `MESSAGE_PARSE_UNKNOWN_TYPE` | Nombre desconocido en `type` o en el tipo respondido por ACK/NACK. |

## Compilación e integración

Desde `proyecto_1`:

```sh
make
make test
```

`test_message_parser` se ejecuta junto a los tests existentes. Cubre los once
tipos, precisión numérica, buffers sin NUL, cada prefijo truncado de un mensaje,
límites de tamaño, entradas inválidas y conservación de la salida ante errores.

El Makefile y ambos CMakeLists compilan `message_parser.c` y la misma copia de
`core_json.c`, agregando el directorio de la biblioteca a los includes. También
se corrige la ruta del glob de fuentes comunes del Pico para apuntar a
`src/common`. Los bucles actuales de serial y los archivos del protocolo no
cambian; cuando el transporte entregue un JSON completo, el punto de conexión
es `message_parse(payload, payload_length, &message)`.

Se verificó la compilación y ejecución en Linux. La compilación completa de
firmware requiere ESP-IDF y Pico SDK, que no estaban disponibles en el entorno
de esta implementación; no se ha validado en dispositivos físicos.
