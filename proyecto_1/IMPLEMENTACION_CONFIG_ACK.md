# Implementación detallada de comunicación y calibración Host–MCU

## 1. Propósito y alcance

Este documento explica el intercambio de aplicación entre el Host y los dos
microcontroladores y, en particular, cómo se integra la calibración existente
en ambos firmwares. Incluye el flujo, los formatos, los cambios de
comunicación, la medición y la validación física.

```text
Host ── CONFIG en JSON ──> ESP32
Host <── ACK en JSON ───── ESP32
Host ── CALIBRATE_REQUEST > ESP32
Host <── CALIBRATE_RESPONSE ESP32
Host ── DEPLOY_TASK JSON ──> ESP32
Host <── ACK/NACK ───────── ESP32

Host ── CONFIG en JSON ──> Raspberry Pi Pico
Host <── ACK en JSON ───── Raspberry Pi Pico
Host ── CALIBRATE_REQUEST > Raspberry Pi Pico
Host <── CALIBRATE_RESPONSE Pico
Host ── DEPLOY_TASK JSON ──> Pico
Host <── ACK/NACK ───────── Pico
```

El JSON viaja dentro del payload del protocolo binario serial ya existente.
Por lo tanto, el framing, la máquina de estados de transmisión/recepción y el
CRC no fueron reemplazados: se añadió una capa de contenido tipado sobre ellos.

El flujo implementado configura ambos nodos y mide su capacidad en unidades de
trabajo por tick (`UT/tick`). Todavía no realiza admisión o despliegue de
tareas, inicio del scheduler, transmisión de eventos ni parada coordinada de
una ejecución. El Host ahora carga y planifica las tareas, y los nodos guardan
las asignadas en RAM; sin embargo, `START` todavía no inicia su ejecución.

## 2. Capas de comunicación

La comunicación se puede entender como cuatro capas:

1. **Mensaje de aplicación (`Message`)**  
   Representa tipos como `MSG_CONFIG`, `MSG_ACK`, `MSG_START` y
   `MSG_DEPLOY_TASK`, con los datos asociados.
2. **JSON**  
   `message_serialize()` convierte un `Message` a JSON y `message_parse()`
   valida el JSON y lo convierte de vuelta a un `Message`.
3. **Protocolo de trama**  
   El JSON se coloca en `protocol_frame_t.payload`. El protocolo agrega sus
   cabeceras, longitud, versión y CRC.
4. **Transporte serial**  
   La capa POSIX del Host y las capas seriales del ESP32/Pico escriben y leen
   los bytes de la trama.

El flujo de salida del Host, por ejemplo, es:

```text
HostConfig -> Message(CONFIG) -> JSON -> payload de trama -> UART/USB serial
```

En un nodo, el flujo de respuesta es el inverso:

```text
UART/USB serial -> trama validada -> payload JSON -> Message(CONFIG)
               -> Message(ACK) -> JSON -> trama de respuesta
```

## 3. Sobre JSON compartido

El esquema común tiene cuatro campos de nivel superior:

```json
{
  "type": "CONFIG",
  "node_id": 1,
  "request_id": 1,
  "data": {
    "algorithm": "RMS",
    "tick_ms": 10,
    "report_every_ticks": 10
  }
}
```

- `type` identifica el tipo de mensaje.
- `node_id` identifica el nodo lógico del protocolo, no el índice del hilo del
  Host ni el número de puerto serial.
- `request_id` identifica la solicitud y permite correlacionar la respuesta.
- `data` contiene los campos propios de ese tipo.

Para la configuración, `algorithm` admite `RMS` o `EDF`; `tick_ms` y
`report_every_ticks` deben ser positivos. La instancia usada en la prueba de
hardware tenía 108 bytes de JSON, sin contar el terminador NUL.

La solicitud de calibración reutiliza el mismo sobre:

```json
{
  "type": "CALIBRATE_REQUEST",
  "node_id": 1,
  "request_id": 2,
  "data": { "sample_ticks": 5 }
}
```

`sample_ticks` indica cuántas mediciones de un tick ejecuta el nodo. La
respuesta exitosa informa la media entera de las muestras:

```json
{
  "type": "CALIBRATE_RESPONSE",
  "node_id": 1,
  "request_id": 2,
  "data": { "ut_per_tick": 1234 }
}
```

Si no se puede realizar la medición, el nodo responde `NACK` para
`CALIBRATE_REQUEST` con `REJECT_CALIBRATION_FAILED`.

El despliegue reutiliza `MSG_DEPLOY_TASK` y el identificador propio de la
tarea:

```json
{
  "type": "DEPLOY_TASK",
  "node_id": 2,
  "request_id": 3,
  "data": {
    "task": {
      "id": 1,
      "type": "P",
      "work": 40,
      "period": 20,
      "deadline": 20,
      "criticality": 2
    },
    "cost_ticks": 1
  }
}
```

`task.id` identifica la tarea, `node_id` indica el nodo destino y
`request_id` correlaciona esta operación de despliegue con su respuesta. No se
agregó un segundo ID de tarea. El Host reserva el 1 para CONFIG, el 2 para
calibración y asigna IDs de solicitud consecutivos desde 3 para los
despliegues. La respuesta satisfactoria es `ACK` con `message_type` igual a
`DEPLOY_TASK`; errores de validación, duplicado, límite de tareas esporádicas
o almacenamiento lleno producen `NACK` con el mismo nodo, solicitud y tipo.

El ACK de ejemplo tiene esta forma:

```json
{
  "type": "ACK",
  "node_id": 1,
  "request_id": 1,
  "data": {
    "message_type": "CONFIG"
  }
}
```

El ACK conserva el `node_id` y el `request_id` de la solicitud y especifica
que confirma un `CONFIG`.

## 4. Serialización y análisis JSON

### Serializador

La implementación está en `src/common/message_serializer.c`; la API pública
se declara en `include/common/message_parser.h`.

`message_serialize(message, buffer, capacity, length)`:

1. Verifica los punteros y la capacidad recibida.
2. Traduce los enums internos a nombres estables del contrato JSON.
3. Formatea los datos correspondientes al tipo del mensaje.
4. Construye el objeto JSON completo en un buffer temporal.
5. Rechaza mensajes inválidos, resultados demasiado grandes o buffers
   insuficientes.
6. Si todo fue válido, copia el resultado al buffer del llamador y asigna a
   `*length` el número de bytes JSON **sin** el NUL final.

La construcción temporal evita dejar una salida parcialmente escrita cuando
falla la serialización. La función no reserva memoria dinámica.

El serializador conoce los tipos declarados en el contrato común —entre ellos
`CONFIG`, calibración, despliegue de tareas, `START`, eventos, reportes, ACK y
NACK—, pero que un tipo se pueda serializar no significa que ya esté conectado
al flujo de ejecución del Host o implementado en el comportamiento del
firmware. En este flujo están conectados CONFIG/ACK,
CALIBRATE_REQUEST/CALIBRATE_RESPONSE y DEPLOY_TASK con su ACK/NACK.

### Parser

`message_parse()` ya existía y es el complemento del serializador. Valida el
JSON y su esquema, exige la longitud explícita del contenido y llena un
`Message` solo si la operación termina correctamente. El límite de contenido
JSON es `MESSAGE_MAX_BYTES` (256 bytes).

Las pruebas de `tests/test_message_parser.c` cubren la ida y vuelta de los
mensajes, tipos de datos, enteros grandes, argumentos inválidos y buffers
insuficientes.

## 5. Recorrido en el Host

La coordinación del intercambio está en `src/host/main.c`.

### Inicio

El programa recibe los puertos como argumentos, en este orden:

1. ESP32.
2. Raspberry Pi Pico.

Ejemplo:

```bash
./output/host /dev/ttyUSB0 /dev/ttyACM0
```

El Host carga `config/system.conf` para obtener el algoritmo, el período del
tick y la frecuencia de reportes. En el intercambio actual, los nombres de los
puertos efectivos se reciben por argumentos; los `port_esp32` y `port_pico`
del archivo no sustituyen esos argumentos.

### Solicitud por nodo

El Host asocia el puerto del ESP32 con `node_id = 1` y el del Pico con
`node_id = 2`. Estos IDs JSON se mantienen separados del índice interno que
usa cada controlador de comunicación.

Para cada dispositivo:

1. Inicializa un `comms_handler_t`.
2. Prepara buffers de envío y recepción con espacio para el máximo JSON y el
   terminador NUL requerido por la API de cadenas de `comms`.
3. Construye un `Message` `MSG_CONFIG`, con `request_id = 1`.
4. Llama a `message_serialize()` para generar JSON.
5. Entrega el JSON a `comms_request_send()`.
6. Inicia el hilo del controlador serial, que encapsula el payload en una
   trama y lo transmite.

Los dos hilos de comunicación se inician antes de comenzar a esperar respuestas.
El Host espera primero la confirmación del nodo 1 y después la del nodo 2. Cada
espera tiene un límite de 30 segundos.

Después de confirmar CONFIG en ambos, el Host envía a cada nodo
`CALIBRATE_REQUEST` con `sample_ticks = CALIBRATION_DEFAULT_SAMPLES` (5) y
`request_id = 2`. Ambos mensajes se encolan antes de esperar resultados. Los
controladores seriales permanecen activos entre CONFIG y calibración y se
detienen una vez que termina la sesión, no inmediatamente después de los ACK.

Al terminar la calibración, el Host:

1. Lee `config.tasks_file` con `task_parser_load()` y obtiene un arreglo de
   `Task` desde el CSV.
2. Inicializa un `Planner` con el algoritmo y la heurística de
   `config/system.conf`.
3. Agrega los nodos 1 y 2 al `Planner`, usando el resultado `ut_per_tick`
   medido para cada dispositivo.
4. Llama a `planner_plan_batch()`. El planificador reutiliza la lógica
   existente de costo, utilización, límites RMS/EDF, duplicados y límite de
   una tarea esporádica por nodo. Las decisiones conservan el orden original
   del CSV, aunque la asignación interna se ordena para planificar el lote.
5. Imprime las tareas que el planificador no admite y no las envía.
6. Para cada tarea admitida, crea un `MSG_DEPLOY_TASK` con la tarea completa,
   el `cost_ticks` calculado para el nodo elegido y un `request_id` único.
   Envía una tarea y espera su respuesta antes de continuar con la siguiente
   asignada a ese controlador.
7. Si llega ACK, registra en la salida que la tarea quedó almacenada. Si llega
   NACK, muestra su razón y elimina la reserva del `Planner` local con
   `planner_remove_task()`, para que su estado refleje lo que realmente aceptó
   el nodo. Un timeout o error de transporte interrumpe el despliegue y
   termina la sesión.

La decisión del planificador ocurre **antes** de mandar la tarea: un ACK del
nodo significa “la guardé en mi tabla de tareas”, no “la tarea ya está
ejecutándose” ni que el nodo haya repetido la prueba de admisión del Host.

### Validación de la respuesta

`wait_for_ack()` copia la recepción bajo el mutex del controlador y procesa los
bytes recibidos:

1. Quita el NUL final si está presente.
2. Llama a `message_parse()` usando la longitud exacta.
3. Acepta solo un mensaje parseado que cumpla todos estos puntos:
   - tipo `MSG_ACK`;
   - `node_id` esperado;
   - `request_id` de la solicitud;
   - `data.reply.type == MSG_CONFIG`.
4. Si no coincide o no es JSON válido, informa el problema. Si expira el
   tiempo, informa timeout.

La respuesta de calibración solo se acepta si el parser la valida, el tipo es
`MSG_CALIBRATE_RESPONSE`, coinciden `node_id` y `request_id`, y
`ut_per_tick > 0`. Un `NACK` correlacionado se informa como rechazo de
calibración; los mensajes de heartbeat `PONG` no son respuestas válidas y se
ignoran. Al terminar, el Host solicita la parada de los controladores y espera
sus hilos. El proceso devuelve éxito únicamente si los dos nodos completan la
calibración.

## 6. Capa de comunicación del Host

Los archivos `src/host/comms/comms.h` y `src/host/comms/comms.c` implementan el
adaptador entre mensajes como cadenas y el protocolo serial existente.

- `COMMS_MESSAGE_CAPACITY` reserva `MESSAGE_MAX_BYTES + 1` bytes: hasta 256
  bytes de contenido más un NUL para manejar el buffer como cadena.
- `comms_request_send()` valida argumentos y tamaños, y copia el mensaje al
  buffer protegido por el mutex del controlador.
- El hilo de `comms_handler()` construye la trama y la envía usando la máquina
  de estados de TX del protocolo.
- Después espera la trama de respuesta con la máquina de estados RX.
- Al guardar un payload recibido, comprueba el tamaño, copia exactamente los
  bytes recibidos y agrega un terminador NUL en el buffer local.

La interfaz heredada de `comms` mide los mensajes enviados con `strlen()` e
incluye el NUL en el payload. El firmware elimina ese byte antes de llamar al
parser JSON. En la dirección de respuesta, `message_serialize()` reporta la
longitud JSON sin NUL y los nodos envían esa longitud exacta; el Host agrega el
NUL al almacenar la respuesta. `comms_request_send()` permite que el buffer de
envío sea también el origen y utiliza `memmove()` para hacer segura esa copia.
El Host libera el indicador de envío completado antes de encolar el siguiente
mensaje.

El controlador conservó su comportamiento anterior de mensajes de prueba
`PING`/`PONG`. Como el buffer de recepción solo guarda una trama pendiente, un
`PONG` podría ocuparlo antes que el resultado JSON. La capa de recepción ahora
permite que un payload JSON de aplicación reemplace texto heredado pendiente,
como `PONG`. No reemplaza otro JSON pendiente, para evitar descartar una
respuesta estructurada.

Después de que el controlador transmite una solicitud, el Host libera el
indicador de envío completado con `comms_acquire_send()` antes de encolar la
siguiente solicitud. Esto es necesario para poder reutilizar el mismo handler
en CONFIG y luego en CALIBRATE_REQUEST.

## 7. Recorrido en los microcontroladores

Los puntos de integración son:

- ESP32: `src/nodes/esp32/main/main.c`.
- Pico: `src/nodes/pico/main.c`.
- Calibrador ESP32: `src/nodes/esp32/src/calibrator.c`.
- Calibrador Pico: `src/nodes/pico/src/calibrator.c`.

Cada firmware comparte la misma forma de proceder:

1. La máquina RX del protocolo recibe y valida una trama.
2. `protocol_set_buffer_to_frame()` extrae el frame.
3. `prepare_response()` comprueba el tamaño del payload, lo copia a un buffer
   local y agrega un NUL de seguridad.
4. Si el payload termina en NUL, reduce la longitud antes de analizar JSON,
   porque el parser espera longitud exacta y contenido sin terminador.
5. `message_parse()` valida el contenido.
6. El firmware valida que `node_id` coincida con el ID del dispositivo (1 para
   ESP32, 2 para Pico).
7. Para `MSG_CONFIG`, guarda `tick_ms` para la calibración y genera un
   `MSG_ACK` correlacionado.
8. Para `MSG_CALIBRATE_REQUEST`, comprueba que exista configuración y que
   `sample_ticks` esté entre 1 y `CALIBRATION_MAX_SAMPLES` (100). Ejecuta el
   calibrador propio de la plataforma una vez por muestra, con una reserva del
   20% del tick, y promedia el resultado `units_per_tick`.
9. Si la medición funciona, crea `MSG_CALIBRATE_RESPONSE`; si falla, responde
   `MSG_NACK` con `REJECT_CALIBRATION_FAILED`.
10. Llama a `message_serialize()` y usa la longitud devuelta para asignar
    `tx_frame.length`.
11. La máquina TX del protocolo transmite la respuesta.

Para `MSG_DEPLOY_TASK`, ambos firmware usan el almacenamiento común
`TaskStore`:

1. El parser llena `incoming.data.deploy_task.task` y
   `incoming.data.deploy_task.cost_ticks`.
2. `task_store_add()` valida que los campos esenciales sean válidos y que el
   costo sea positivo.
3. Rechaza un `task.id` ya almacenado con `REJECT_DUPLICATE_TASK`, una segunda
   tarea esporádica con `REJECT_SPORADIC_LIMIT` y una tabla llena con
   `REJECT_INSUFFICIENT_CAPACITY`.
4. Si acepta, copia la definición completa y `cost_ticks` a un `StoredTask` en
   la tabla fija del MCU, y aumenta `TaskStore.count`.
5. El firmware serializa un ACK correlacionado. Ante rechazo, serializa un
   NACK correlacionado con la razón correspondiente.

`TASK_MAX_PER_NODE` establece una capacidad de 8 entradas por nodo. La tabla
es estática y reside en RAM; no sobrevive al reinicio. El scheduler usa esa
misma constante mediante `MAX_TASKS`, así que no puede aceptar más tareas que
las almacenables en `TaskStore`. Aunque ya existe una función de conversión al
tipo del scheduler, el flujo todavía no la invoca: no se crean `Job`, no se
ejecuta RMS/EDF y no se genera ningún `EVENT` o `TICK_REPORT`. La tarea queda
preparada para un futuro paso de inicio/ejecución.

### Conversión de la tarea guardada al tipo del scheduler

Para poder incluir en un mismo archivo la definición compartida del mensaje y
la definición que usa el scheduler, se renombró el tipo interno del scheduler
de `Task` a `SchedulerTask` en `src/nodes/scheduler/task.h`. El tipo común
`Task` mantiene su nombre y sigue siendo el que guarda `TaskStore` y el que
viaja en JSON.

La función `scheduler_task_from_stored()` está declarada en
`src/nodes/scheduler/task_conversion.h` e implementada en
`src/nodes/scheduler/task_conversion.c`. Recibe un `StoredTask` y produce un
`SchedulerTask` con este mapeo:

| Tarea compartida / almacenada | Tipo del scheduler | Significado |
|---|---|---|
| `task.id` | `id` | Identificador de la tarea |
| `task.type` | `type` | `'P'` periódica o `'S'` esporádica |
| `task.work` | `W` | Trabajo requerido en unidades de trabajo |
| `task.period` | `T` | Período o intervalo mínimo |
| `task.deadline` | `D` | Plazo relativo |
| `task.criticality` | `criticality` | Criticidad |
| `StoredTask.cost_ticks` | `C` | Costo estimado en ticks del nodo |

La función valida que los valores requeridos sean positivos y que la
criticidad no sea negativa: el tipo común permite `int32_t`, mientras que
`SchedulerTask.criticality` es `uint32_t`, por lo que una criticidad negativa
no se puede representar sin cambiar su significado. Escribe el resultado solo
si toda la entrada es válida; devuelve `false` para punteros inválidos o datos
no representables.

Ejemplo para el futuro bucle de carga del scheduler:

```c
for (size_t i = 0u; i < deployed_tasks.count; ++i) {
    SchedulerTask scheduler_task;
    if (!scheduler_task_from_stored(&deployed_tasks.tasks[i],
                                    &scheduler_task) ||
        !add_task(&scheduler_task, &scheduler)) {
        // Manejar explícitamente la tarea no representable o no aceptada.
    }
}
```

Este bucle es ilustrativo y todavía no se ejecuta en el firmware: el flujo
actual almacena las tareas y responde ACK/NACK, pero aún no procesa `START` ni
registra las tareas en `Scheduler`. El conversor se compila para Pico/Pico W y
ESP32, y tiene una prueba unitaria que verifica el mapeo y los casos inválidos.

Si el mensaje no coincide con el comportamiento implementado, el firmware
responde con el texto heredado “didn't understand message”. El manejo de
`PING`/`MARCO` permanece disponible para conservar la prueba anterior.

### Qué mide `calibrator_run()`

Los archivos `src/nodes/esp32/src/calibrator.c` y
`src/nodes/pico/src/calibrator.c` ya existían; el flujo de mensajes los
invoca, no los reemplaza. Ambos implementan la misma interfaz:

- `tick_us`: duración del tick en microsegundos.
- `reserve_percent`: porcentaje del tick que no se consume en el trabajo
  medido.
- `CalibrationResult *`: estructura de salida con `tick_us` y
  `units_per_tick`.

El calibrador calcula el presupuesto como
`tick_us * (100 - reserve_percent) / 100`. Obtiene el instante de inicio con
`esp_timer_get_time()` en ESP32 o `time_us_64()` en Pico/Pico W. Mientras el
tiempo transcurrido sea menor que ese presupuesto, llama a
`unidad_trabajo()` y aumenta un contador. Al final, `units_per_tick` contiene
el número de invocaciones que cupieron dentro del intervalo medido.

`unidad_trabajo()` se implementa en `src/common/ut.c`: realiza 1000
iteraciones de operaciones aritméticas deterministas y actualiza una variable
`volatile` llamada `sink` para que el compilador no elimine el trabajo. Por
tanto, `UT/tick` representa cuántas invocaciones de esta función caben en el
presupuesto del tick; no es un porcentaje de CPU ni una medición de una tarea
de usuario.

La configuración actual especifica `tick_ms=10` y el firmware reserva el 20%
del tick. Por ello, cada muestra intenta ejecutar trabajo durante
aproximadamente 8 ms. El Host solicita cinco muestras; el nodo suma sus
`units_per_tick` y responde con la media entera `total_units / 5`. El tiempo de
ejecución del calibrador por nodo es de aproximadamente 40 ms, más el tiempo
de comunicación y el overhead del sistema.

Cada plataforma mide su propio hardware. La frecuencia, la compilación, las
optimizaciones y la carga del dispositivo pueden afectar el resultado. El Host
usa el valor para la admisión, pero la calibración por sí sola no inicia el
scheduler ni equivale a desplegar o ejecutar tareas.

## 8. Integración de archivos comunes en los builds

El serializador es código común para Host, ESP32 y Pico:

- El Makefile del Host compila `src/common/message_serializer.c`.
- `src/nodes/pico/CMakeLists.txt` reúne los fuentes comunes con
  `CONFIGURE_DEPENDS`, de modo que CMake vuelve a configurar la lista si se
  agrega un archivo común.
- `src/nodes/esp32/main/CMakeLists.txt` registra explícitamente
  `message_serializer.c` en el componente ESP-IDF. El archivo se excluye de la
  lista glob antes de agregarlo explícitamente, ya que la evaluación de
  componentes de ESP-IDF ocurre en modo script.
- ESP32 incorpora `esp_timer` para medir el intervalo de calibración y Pico
  usa `time_us_64()` mediante su SDK.
- `third_party/coreJSON/core_json.c` sigue siendo la dependencia usada por el
  parser.

El Makefile también contiene dependencias de enlace para que:

- `test_foundation` enlace protocolo, conversión y serial;
- `test_message_parser` enlace parser, serializador y coreJSON;
- `make test` ejecute los binarios de tests del scheduler con sus nombres
  definidos.

## 9. Archivos del cambio

| Archivo | Función en el incremento |
|---|---|
| `src/common/message_serializer.c` | Implementa la conversión de `Message` a JSON y serializa el nuevo motivo NACK. |
| `include/common/message_parser.h` | Declara el parser/serializador y los límites de muestras de calibración. |
| `src/host/main.c` | Coordina CONFIG, calibración, carga CSV, admisión, despliegue y ACK/NACK de cada tarea. |
| `src/host/planner/planner.c` | Conserva la asignación existente y permite revertir una reserva tras un NACK del MCU. |
| `include/host/planner.h` | Declara la operación `planner_remove_task()` para rollback. |
| `src/host/config/task_parser.c` | Carga las definiciones de tareas desde el CSV configurado. |
| `src/host/comms/comms.h` | Define la capacidad de buffers para el payload JSON. |
| `src/host/comms/comms.c` | Transporta mensajes y sincroniza buffers; permite que JSON de aplicación reemplace texto heartbeat antiguo en RX. |
| `src/nodes/esp32/main/main.c` | Maneja CONFIG y calibración del nodo 1; responde ACK, resultado o NACK. |
| `src/nodes/pico/main.c` | Maneja CONFIG y calibración del nodo 2; responde ACK, resultado o NACK, con GPIO de estado válidos para RP2040. |
| `src/nodes/esp32/src/calibrator.c` | Mide unidades de trabajo por intervalo del ESP32. |
| `src/nodes/pico/src/calibrator.c` | Mide unidades de trabajo por intervalo del Pico. |
| `src/nodes/scheduler/task.h` | Define `SchedulerTask` y `Job` para separar el tipo de ejecución del `Task` compartido. |
| `src/nodes/scheduler/task_conversion.h` | Expone la conversión de `StoredTask` a `SchedulerTask`. |
| `src/nodes/scheduler/task_conversion.c` | Valida y convierte campos, incluyendo el costo guardado a `C`. |
| `include/common/task.h` | Define el máximo común de 8 tareas por nodo, compartido por almacenamiento y scheduler. |
| `include/common/task_store.h` | Define `StoredTask` (tarea y costo) y `TaskStore` (tabla fija). |
| `src/common/task_store.c` | Valida, detecta duplicados/límites y almacena tareas recibidas. |
| `src/nodes/esp32/main/CMakeLists.txt` | Incluye serializador, calibrador y dependencia `esp_timer`. |
| `src/nodes/pico/CMakeLists.txt` | Incluye fuentes comunes y el calibrador del Pico. |
| `src/common/ut.c` | Define el trabajo unitario medido por los calibradores. |
| `src/common/message_parser.c` | Reconoce `REJECT_CALIBRATION_FAILED` como motivo NACK. |
| `include/common/events.h` | Añade `REJECT_CALIBRATION_FAILED` como motivo de NACK. |
| `src/host/report/statistics.c` | Asigna una descripción al nuevo motivo de rechazo. |
| `tests/test_message_parser.c` | Comprueba mensajes de calibración y NACK de despliegue, ida y vuelta. |
| `tests/test_task_store.c` | Comprueba copia de parámetros, duplicados, límite esporádico y capacidad. |
| `tests/test_task_conversion.c` | Comprueba el mapeo a `SchedulerTask` y rechazos de entradas inválidas. |
| `tests/test_planner.c` | Comprueba que el rollback del planner libera la asignación rechazada. |
| `Makefile` | Mantiene dependencias y tests; ofrece targets `pico-build` y `picow-build`. |
| `IMPLEMENTACION_CONFIG_ACK.md` | Documenta el diseño, compilación, carga y validación física. |

## 10. Compilación y pruebas

Desde la carpeta `proyecto_1`:

```bash
make debug
make test
make pico-build
make picow-build
make esp-build
```

`pico-build` genera firmware para la placa Pico estándar; `picow-build` genera
firmware para Pico W. Para una Pico W se debe cargar
`build/pico_w/pico.uf2`, no `build/pico/pico.uf2`. `esp-build` genera el binario
de aplicación ESP32.

Durante el desarrollo, `make debug`, `make test`, `make pico-build`,
`make picow-build` y `make esp-build` finalizaron correctamente. Los
artefactos generados son:

- Pico: `build/pico/pico.uf2`
- Pico W: `build/pico_w/pico.uf2`
- ESP32: `build/esp32/esp32_node.bin`

La prueba unitaria del serializador es parte de `make test` y también puede
ejecutarse directamente:

```bash
make output/test_message_parser
./output/test_message_parser
./output/test_task_store
```

### Cargar firmware en Pico W

1. Ejecutar `make picow-build`.
2. Desconectar la Pico W del USB.
3. Mantener presionado `BOOTSEL` mientras se conecta de nuevo.
4. Cuando aparezca la unidad `RPI-RP2`, copiar `build/pico_w/pico.uf2` a esa
   unidad.
5. Esperar que la unidad desaparezca y que la placa se reinicie.
6. Confirmar el puerto serial y ejecutar el Host.

El enlace de esta prueba usa USB CDC y no requiere Wi-Fi. Los GPIO de estado
declarados en `src/nodes/pico/main.c` están dentro del rango válido del RP2040.
En la Pico W, el LED integrado está conectado al chip inalámbrico, así que esos
GPIO de estado no son el LED integrado.

## 11. Prueba física realizada

Después de cargar los firmwares, se ejecutó el Host con:

```bash
./output/host /dev/ttyUSB0 /dev/ttyACM0
```

`/dev/ttyUSB0` correspondía al ESP32 mediante el adaptador CP2102 y
`/dev/ttyACM0` a la Pico W. La prueba anterior de CONFIG/ACK mostró:

```text
Enviando CONFIG a nodo 1 por /dev/ttyUSB0 (108 bytes JSON).
Enviando CONFIG a nodo 2 por /dev/ttyACM0 (108 bytes JSON).
CONFIG confirmado por nodo 1 (/dev/ttyUSB0).
CONFIG confirmado por nodo 2 (/dev/ttyACM0).
CONFIG/ACK completado en ambos dispositivos.
```

Las etiquetas `[0]` y `[1]` que aparecen en los mensajes internos
`Connected to device` son índices de controladores. No son los `node_id` que
viajan en JSON.

Tras cargar el firmware Pico W actualizado y corregir la prioridad del buffer
de recepción en el Host, se ejecutó la calibración completa:

```text
Enviando CONFIG a nodo 1 (108 bytes JSON).
Enviando CONFIG a nodo 2 (108 bytes JSON).
CONFIG confirmado por nodo 1 (/dev/ttyUSB0).
CONFIG confirmado por nodo 2 (/dev/ttyACM0).
Enviando CALIBRATE_REQUEST a nodo 1 (81 bytes JSON).
Enviando CALIBRATE_REQUEST a nodo 2 (81 bytes JSON).
Calibración nodo 1 (/dev/ttyUSB0): 71 UT/tick.
Calibración nodo 2 (/dev/ttyACM0): 99 UT/tick.
CONFIG/ACK y calibración completados en ambos dispositivos.
```

Esto confirma que ambos nodos recibieron CONFIG, respondieron con ACK
correlacionado, procesaron `CALIBRATE_REQUEST` y entregaron una respuesta JSON
que el Host aceptó. `71` y `99` son los resultados de esa ejecución y esa
compilación: son mediciones, no constantes fijas ni valores que deban repetirse
exactamente en otra sesión.

El despliegue de tareas se incorporó después de esa prueba física. El Host,
los dos firmwares y las pruebas unitarias ya compilan con el nuevo flujo, pero
todavía debe validarse el envío/ACK de tareas con los firmwares recién
flasheados en hardware.

### Diagnóstico de los timeouts durante la integración

Durante las pruebas se encontraron dos causas distintas:

1. El Host cerraba los controladores seriales después de recibir CONFIG/ACK,
   antes de encolar CALIBRATE_REQUEST. Se movió el cierre al final de la sesión
   o a las rutas de error. En la salida correcta, `Closing file descriptor`
   aparece después de recibir los resultados, nunca entre ACK y calibración.
2. El controlador envía `PING` automáticos y recibe texto `PONG`. Como cada
   controlador guarda una sola trama pendiente, un `PONG` podía ocupar el
   buffer e impedir guardar la respuesta JSON posterior. Se modificó
   `comms.c` para que JSON de aplicación pueda reemplazar texto heredado
   pendiente, sin sobrescribir otro JSON.

Si CONFIG/ACK funciona pero vuelve a ocurrir un timeout de calibración, revisar:

1. Recompilar el Host con `make debug`.
2. Confirmar que la Pico W tenga el UF2 de
   `build/pico_w/pico.uf2` y no el de `build/pico/pico.uf2`.
3. Compilar el firmware Pico W después de los cambios y reiniciar la placa
   después de copiar el UF2.
4. Cerrar cualquier monitor serial que esté usando `/dev/ttyACM0`.
5. Confirmar que los argumentos siguen el orden ESP32 y luego Pico W.

## 12. Límites actuales y siguiente incremento

- El Host implementa CONFIG/ACK, calibración, admisión, asignación y
  despliegue/almacenamiento de las tareas admitidas.
- Los nodos guardan `tick_ms` para calibrar. El algoritmo y la frecuencia de
  reportes recibidos en CONFIG aún no se aplican a un scheduler.
- El Host usa las capacidades calibradas para llamar al `Planner`, pero la
  tabla resultante solo vive durante esta sesión del Host.
- `TaskStore` es RAM volátil; al reiniciar un nodo se vacía.
- `START` todavía no traduce las tareas almacenadas a los `Job` del scheduler
  ni inicia la ejecución. Tampoco se generan `EVENT` o `TICK_REPORT`.
- `STOP`, cancelación remota y persistencia no están implementados en este
  flujo.

El siguiente incremento recomendado es integrar `TaskStore` con el scheduler
del nodo: convertir cada tarea almacenada al tipo que consume el scheduler,
crear/liberar jobs, recibir `START`, ejecutar ticks y devolver eventos y
reportes. Antes de eso, validar físicamente `DEPLOY_TASK` y sus ACK/NACK.
