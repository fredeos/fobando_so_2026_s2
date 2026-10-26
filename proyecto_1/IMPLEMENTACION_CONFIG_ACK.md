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

Host ── CONFIG en JSON ──> Raspberry Pi Pico
Host <── ACK en JSON ───── Raspberry Pi Pico
Host ── CALIBRATE_REQUEST > Raspberry Pi Pico
Host <── CALIBRATE_RESPONSE Pico
```

El JSON viaja dentro del payload del protocolo binario serial ya existente.
Por lo tanto, el framing, la máquina de estados de transmisión/recepción y el
CRC no fueron reemplazados: se añadió una capa de contenido tipado sobre ellos.

El flujo implementado configura ambos nodos y mide su capacidad en unidades de
trabajo por tick (`UT/tick`). Todavía no realiza admisión o despliegue de
tareas, inicio del scheduler, transmisión de eventos ni parada coordinada de
una ejecución. Los resultados se imprimen en el Host; todavía no se guardan
en el `Planner`.

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
firmware. En este incremento están conectados CONFIG/ACK y
CALIBRATE_REQUEST/CALIBRATE_RESPONSE.

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
optimizaciones y la carga del dispositivo pueden afectar el resultado. La
medición todavía no inicializa el planificador ni equivale a desplegar o
ejecutar tareas.

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
| `src/host/main.c` | Carga configuración, genera CONFIG por nodo, espera ACK y coordina el cierre. |
| `src/host/comms/comms.h` | Define la capacidad de buffers para el payload JSON. |
| `src/host/comms/comms.c` | Transporta mensajes y sincroniza buffers; permite que JSON de aplicación reemplace texto heartbeat antiguo en RX. |
| `src/nodes/esp32/main/main.c` | Maneja CONFIG y calibración del nodo 1; responde ACK, resultado o NACK. |
| `src/nodes/pico/main.c` | Maneja CONFIG y calibración del nodo 2; responde ACK, resultado o NACK, con GPIO de estado válidos para RP2040. |
| `src/nodes/esp32/src/calibrator.c` | Mide unidades de trabajo por intervalo del ESP32. |
| `src/nodes/pico/src/calibrator.c` | Mide unidades de trabajo por intervalo del Pico. |
| `src/nodes/esp32/main/CMakeLists.txt` | Incluye serializador, calibrador y dependencia `esp_timer`. |
| `src/nodes/pico/CMakeLists.txt` | Incluye fuentes comunes y el calibrador del Pico. |
| `src/common/ut.c` | Define el trabajo unitario medido por los calibradores. |
| `src/common/message_parser.c` | Reconoce `REJECT_CALIBRATION_FAILED` como motivo NACK. |
| `include/common/events.h` | Añade `REJECT_CALIBRATION_FAILED` como motivo de NACK. |
| `src/host/report/statistics.c` | Asigna una descripción al nuevo motivo de rechazo. |
| `tests/test_message_parser.c` | Comprueba round-trip de respuesta y NACK de calibración y errores de buffers. |
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
de recepción en el Host, se ejecutó el intercambio completo:

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

- El Host implementa CONFIG/ACK y solicitud/recepción de calibración, no el
  ciclo entero de tareas.
- Los nodos guardan `tick_ms` para calibrar. El algoritmo y la frecuencia de
  reportes recibidos en CONFIG aún no se aplican a un scheduler.
- No se envían tareas de `config/tasks.csv`.
- No se envían `DEPLOY_TASK`, `START`, `STOP`, `EVENT` ni `TICK_REPORT` como
  parte de una ejecución del scheduler.
- La calibración produce y muestra `UT/tick`, pero todavía no inicializa el
  `Planner` del Host con esos resultados.

El siguiente incremento recomendado es guardar las capacidades medidas en el
planificador del Host y usarlas para planificar las tareas de
`config/tasks.csv`.
