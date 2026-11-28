# Implementación del driver de comunicación con la pantalla

## Propósito y alcance

Este documento describe el flujo implementado hasta ahora para enviar desde el
host una imagen procesada al ESP32 mediante el protocolo serial del proyecto.
La biblioteca `libscreen_driver.a` ofrece una interfaz de alto nivel para que
el cliente no tenga que construir tramas, dividir la imagen ni gestionar los
ACK de cada trama. El ESP32 de demostración recibe y valida la transferencia y
señaliza sus etapas mediante LEDs.

El demo actual comprueba el transporte y la integridad lógica del envío, pero
**no decodifica los píxeles para dibujarlos en una pantalla**. La integración
con el controlador físico de la pantalla LED sigue siendo trabajo pendiente.
El firmware normal (`main.c`) conserva su aplicación separada; la recepción de
imágenes y sus LEDs pertenecen al punto de entrada alternativo del demo.

## Componentes

- `include/driver/screen_driver.h`: API pública del driver de pantalla.
- `src/driver/screen_driver.c`: implementación POSIX de la biblioteca; gestiona
  el puerto serial, validaciones, JSON, chunks, protocolo y respuesta final.
- `src/driver/screen_driver_demo.c`: ejecutable anfitrión de prueba; lee una
  imagen RAW, abre el UART, envía la imagen, imprime progreso y cierra el UART.
- `include/common/screen_link_config.h`: baud rate compartido entre host y ESP32.
- `src/firmware/esp32/main/main_demo_screen_comm.c`: firmware demostrativo que
  recibe los chunks, valida su contenido y controla tres LEDs.
- `include/common/protocol.h` y `src/common/protocol/`: protocolo binario
  existente, su máquina de estados TX/RX y el cálculo/validación de CRC.

## API de la biblioteca

La biblioteca expone `screen_driver_open`, `screen_driver_send_image`,
`screen_driver_send_image_with_progress` y `screen_driver_close`.

`screen_driver_open` recibe el contexto, la ruta del dispositivo serial y el
baud rate. Configura el puerto POSIX y deja el contexto marcado como
inicializado. El programa de demo usa `/dev/ttyUSB0` y el baud rate definido en
`SCREEN_LINK_BAUDRATE` (actualmente 921600).

`screen_driver_send_image` recibe un puntero al buffer de píxeles, su tamaño,
ancho, alto y cantidad de canales. Por ahora requiere 128 × 160 píxeles y
acepta un canal (`GRAY8`) o tres canales (`BGR888`). Comprueba que el tamaño
coincida exactamente con `width × height × channels`.

`screen_driver_send_image_with_progress` realiza el mismo envío y permite
registrar una función callback. El callback recibe la cantidad de chunks
confirmados, la cantidad total y un contexto del llamador. Se invoca después
de que cada chunk recibe el ACK binario del ESP32; no representa píxeles ya
dibujados en la pantalla. La función básica `screen_driver_send_image` delega
en esta variante sin callback, por lo que mantiene una API simple para otros
clientes.

`screen_driver_close` cierra el descriptor serial y libera el estado de
inicialización del contexto.

## Imagen de entrada y división en chunks

El buffer esperado contiene píxeles contiguos en orden de filas, sin cabecera
de archivo, padding ni compresión. Los tamaños para la resolución fijada son:

| Formato | Tamaño esperado |
|---|---:|
| `GRAY8` (1 canal) | 20 480 bytes |
| `BGR888` (3 canales) | 61 440 bytes |

El tamaño de chunk no es un parámetro del usuario: se calcula a partir del
espacio disponible en el payload JSON de 512 bytes. Cada byte de píxel se
representa con dos caracteres hexadecimales (`00`–`ff`), además de los campos
JSON y metadatos. Por ello, para BGR888 se transmiten aproximadamente 188
bytes de píxel por chunk y unas 327 tramas por imagen; el valor exacto depende
de la cantidad de dígitos en el ID e índices. Para GRAY8 son aproximadamente
109 tramas. El último chunk puede ser menor.

La biblioteca genera un mismo `id` para todos los chunks de una imagen. Cada
chunk lleva un índice `chunk` base cero y `total_chunks`; el conteo se calcula
internamente teniendo en cuenta el espacio real que ocupan los metadatos.
El driver espera la confirmación del protocolo para cada trama antes de pasar
a la siguiente. Si una trama falla, devuelve un estado de error y no continúa
con los chunks restantes.

## Formato de aplicación

Cada carga JSON es compacta, UTF-8, sin salto de línea. Un chunk tiene esta
forma:

```json
{"id":1,"type":"request","command":"send","data":{"width":128,"height":160,"format":"BGR888","chunk":0,"total_chunks":327,"pixels":"00ff7a..."}}
```

Significado de los campos:

- `id`: identificador de la imagen/solicitud, constante a lo largo del envío.
- `type`: `request` para los chunks del host.
- `command`: `send` para indicar envío de píxeles.
- `data.width`, `data.height`: dimensiones; actualmente 128 × 160.
- `data.format`: `GRAY8` o `BGR888`.
- `data.chunk`: índice base cero del chunk.
- `data.total_chunks`: número de chunks que componen la imagen.
- `data.pixels`: bytes de píxel codificados en hexadecimal, en el orden del
  buffer original.

Cuando todos los chunks y bytes son aceptados, el ESP32 devuelve una respuesta
de aplicación dentro de otra trama del mismo protocolo:

```json
{"id":1,"type":"response","command":"send","data":{"status":"ok"}}
```

La respuesta conserva el `id`. El host considera completada la operación solo
si recibe una trama válida y el JSON coincide con la respuesta exitosa
esperada. El ACK binario por chunk y esta respuesta de aplicación cumplen
propósitos diferentes: el ACK confirma una trama individual; la respuesta
`status: ok` confirma la transferencia completa.

## Encapsulado del protocolo

El JSON de cada chunk se transporta como payload de una trama del protocolo
binario existente. La trama contiene, en orden, el prefijo `A5 5A`, versión,
relevancia, longitud efectiva de 4 bytes en big-endian, payload de capacidad
fija (512 bytes) y CRC-16/CCITT-FALSE. El CRC se calcula sobre versión,
relevancia, longitud y payload, según la implementación compartida del
proyecto. Las rutinas `protocol_tx_*` y `protocol_rx_*` se ocupan del avance de
las máquinas de estados y de la serialización.

El receptor confirma la trama con los bytes binarios ACK/NACK definidos por
`protocol.h`. El contenido JSON no reemplaza ni modifica este protocolo: es el
formato de aplicación que viaja dentro de su payload.

## Flujo del demo anfitrión

El ejecutable `output/screen-driver-demo` implementa el siguiente recorrido:

1. Recibe el dispositivo serial y, opcionalmente, la ruta de imagen RAW y el
   número de canales.
2. Lee exactamente los bytes requeridos para 128 × 160; rechaza archivos con
   tamaño insuficiente o bytes extra.
3. Abre el puerto a 921600 baudios.
4. Invoca `screen_driver_send_image_with_progress`.
5. Imprime una línea de porcentaje y contadores `chunks confirmados/total`,
   actualizada después del ACK de cada chunk.
6. Espera la respuesta de aplicación del ESP32, informa si fue aceptada y
   cierra el UART.

Para construirlo desde la raíz del proyecto:

```sh
make screen-driver-demo
```

Para ejecutarlo:

```sh
./output/screen-driver-demo /dev/ttyUSB0 [imagen.raw [1|3]]
```

El canal predeterminado es 3 (`BGR888`). La ruta predeterminada configurada
por el demo es `images/screen_driver_test.raw`.

## Flujo y señalización del demo ESP32

El demo configura UART0 a la velocidad compartida y recibe el protocolo usando
el driver serial ESP32 existente. Para cada trama recibida, verifica la
estructura de protocolo/CRC, versión, JSON, tipo y comando, dimensiones,
formato, índice de chunk, ID de transferencia y límites de bytes. Solo acepta
chunks consecutivos de una transferencia coherente. En esta etapa valida los
caracteres hexadecimales y contabiliza sus bytes, pero no necesita almacenar
ni convertir el buffer para dibujarlo.

Los tres LEDs externos son activos en alto y usan GPIO 25, 26 y 27:

| Indicador | GPIO | Función |
|---|---:|---|
| Inicio | 25 | Se enciende al reconocer el primer chunk válido de una imagen. |
| Paquete | 26 | Pulso breve al aceptar cada chunk válido. |
| Finalización | 27 | Se enciende cuando se recibieron todos los chunks y el total exacto de bytes. |

Al iniciar una nueva imagen se reinicia el estado de transferencia y se apagan
los indicadores de inicio/finalización. Si se detecta una trama o secuencia
inválida, el demo descarta/reinicia la transferencia. En un error fatal entra
en un patrón de parpadeo del LED de paquete. Cuando la transferencia completa
es válida, enciende el LED de finalización y envía la respuesta de aplicación
`status: ok` al host. Esta indicación significa recepción íntegra, no que los
píxeles ya estén desplegados en el panel.

## Conexión física y selección del firmware

La conexión usada por el demo es UART0 del ESP32 a través del puente USB-UART
integrado de la placa. El puerto suele aparecer como `/dev/ttyUSB0`. La
configuración es 921600 baudios, 8 bits, sin paridad y un bit de parada (8N1).
En esta configuración no se debe conectar un adaptador externo simultáneamente
a GPIO1/GPIO3; esos pines corresponden a TX/RX de UART0. El host y el ESP32
deben usar la misma velocidad.

GPIO25, GPIO26 y GPIO27 se conectan cada uno a un LED con su resistencia
limitadora y GND común. Antes de montar el circuito, verificar el pinout exacto
de la placa y evitar pines ya usados por periféricos del firmware.

El firmware demo se selecciona con la opción de configuración de ESP-IDF
`SCREEN_COMM_DEMO=ON`; con la opción desactivada se conserva el punto de
entrada normal del ESP32. El host se compila por separado mediante el target
`screen-driver-demo`.

## Ajustes de rendimiento implementados

La velocidad compartida del enlace se fijó en 921600 baudios. Además, en el
host se eliminaron pausas de 1 ms entre transiciones inmediatas de los estados
TX; se conserva el pacing mientras se espera el ACK, para evitar una espera
activa continua. En el receptor ESP32 se quitó el `vTaskDelay(1)` adicional
después de una lectura incompleta, ya que la lectura UART ya espera hasta su
timeout configurado. Se aplicó el mismo criterio al bucle TX de la respuesta:
la espera por el ACK la realiza la lectura UART.

Estos cambios reducen latencia artificial sin cambiar el formato JSON ni el
protocolo binario. El porcentaje que muestra el host es el avance de chunks
confirmados; no mide el tiempo que pueda requerir un procesamiento o una
presentación física futura.

## Aspectos pendientes

- Decodificar la cadena hexadecimal y almacenar/reensamblar los píxeles en el
  ESP32 si el controlador de pantalla lo requiere.
- Conectar el flujo recibido con el driver real del panel LED y presentar la
  imagen completa.
- Verificar el orden de color esperado por el panel (por ejemplo, BGR frente a
  RGB) y convertirlo si corresponde.
- Definir y probar respuestas de aplicación de error con detalle; el host
  actualmente reconoce la respuesta exitosa `status: ok`.
- Medir tiempos reales con firmware recompilado y placa conectada; la
  compilación del host por sí sola no valida el rendimiento del hardware.
