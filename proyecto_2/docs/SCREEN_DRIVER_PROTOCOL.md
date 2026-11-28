# Contrato del driver de pantalla y el ESP32

Este documento fija el contrato inicial entre el driver POSIX del anfitrión y
el firmware del ESP32. El cliente actual no implementa esta conexión: su
`main.c` contiene una demostración del serializador JSON. El envío TCP/MPI
actual termina en el cliente con un PNG codificado; el driver de pantalla debe
recibir los píxeles crudos producidos por el procesamiento distribuido, antes
de codificarlos como PNG.

## Formato de imagen que recibe el driver

La salida intermedia del procesamiento es una matriz contigua de píxeles, sin
cabecera de archivo ni padding entre filas. La operación de resize produce tres
canales por píxel; Sobel puede producir un canal. Para la pantalla se fija por
ahora una resolución de 128 × 160:

| Formato | Canales | Bytes por imagen |
|---|---:|---:|
| `GRAY8` | 1 | 20 480 |
| `BGR888` | 3 | 61 440 |

El orden `BGR888` refleja el búfer utilizado por el pipeline actual. El firmware
debe convertir de BGR a RGB si el controlador de pantalla lo requiere. Debe
confirmarse el orden físico de canales de la pantalla antes de desplegarlo.

## Mensaje de aplicación dentro del protocolo

Cada fragmento es un documento JSON UTF-8 compacto, sin salto de línea. Los
bytes de píxel se codifican como dos dígitos hexadecimales por byte en
`data.pixels`; así la carga útil continúa siendo JSON válido y se puede
transportar como bytes crudos en el payload del protocolo:

```json
{
  "id": 1,
  "type": "request",
  "command": "send",
  "data": {
    "width": 128,
    "height": 160,
    "format": "BGR888",
    "chunk": 0,
    "total_chunks": 800,
    "pixels": "00ff7a..."
  }
}
```

`chunk` es un índice base cero. `total_chunks` es la cantidad de fragmentos de
la imagen completa. El `id` es común a todos los fragmentos de una solicitud;
el ESP32 debe descartar o reiniciar una recepción incompleta si llega otro `id`
antes de completar la actual. Los bytes hexadecimales se decodifican en orden y
se concatenan según el índice de fragmento.

El protocolo existente fija `PROTOCOL_PAYLOAD_BYTES` en 512 bytes. El driver
calcula el espacio restante después de serializar los metadatos y la cadena
hexadecimal; cada byte de imagen ocupa dos bytes JSON. El tamaño útil varía con
los dígitos de `id`, `chunk` y `total_chunks`; por eso el número de fragmentos
se calcula internamente y no se configura desde el usuario. Con el formato JSON
actual caben aproximadamente 188 bytes de píxel por trama tanto para `BGR888`
(unas 327 tramas por imagen) como para `GRAY8` (unas 109 tramas). El cálculo
exacto se realiza en tiempo de ejecución según los dígitos
del identificador e índices. El payload final puede ser menor que 512 bytes.

Cada JSON se transporta en una trama del protocolo binario existente:

1. Prefijo de sincronización: `A5 5A`.
2. Versión: `1`.
3. Relevancia.
4. Longitud efectiva del JSON (big-endian, máximo 512).
5. Payload: JSON UTF-8 y bytes no usados rellenados con cero en la trama fija.
6. CRC-16/CCITT-FALSE calculado sobre versión, relevancia, longitud y payload.

El receptor de protocolo envía su confirmación binaria `ACK`/`NACK` por cada
trama. Este ACK confirma la integridad de una trama; no significa que la imagen
completa ya se haya mostrado.

## Respuesta de aplicación del ESP32

Después de recibir y ensamblar todos los fragmentos, y de aceptar la imagen
para mostrarla, el ESP32 debe enviar una trama de protocolo cuyo payload sea:

```json
{"id":1,"type":"response","command":"send","data":{"status":"ok"}}
```

La respuesta debe conservar el `id` de la solicitud. `status: "ok"` significa
que se recibieron todos los bytes esperados (`width × height × bytes_per_pixel`)
y que la operación de presentación fue aceptada. Si falla, el mismo esquema
debe responder, por ejemplo, con `data.status` igual a `error` y un campo
`data.message` descriptivo. El driver actual acepta únicamente la respuesta
exitosa; el manejo de respuestas de error detalladas puede ampliarse después.

## Conexión serial

El driver del anfitrión abre el dispositivo POSIX indicado por el llamador (por
ejemplo `/dev/ttyUSB0`) a 921600 baudios, 8 bits, sin paridad y un
bit de parada. TX/RX son full-duplex lógico sobre UART; el firmware debe
configurar los mismos parámetros y conectar TX del anfitrión a RX del ESP32,
RX del anfitrión a TX del ESP32 y compartir GND. Los niveles eléctricos deben
ser compatibles con UART TTL de 3.3 V; no conectar RS-232 directamente al ESP32.

La aplicación habitual del ESP32 no procesa todavía este contrato ni controla
la pantalla LED con las imágenes recibidas. El firmware de demostración
descrito a continuación añade la recepción y validación necesarias para
verificar la comunicación, pero deliberadamente no dibuja los píxeles.

## Demo de recepción en ESP32

El firmware de demostración está separado del programa normal en
`src/firmware/esp32/main/main_demo_screen_comm.c`. Se selecciona al configurar
ESP-IDF con `SCREEN_COMM_DEMO=ON`; si la opción no se activa, se conserva
`main.c` como punto de entrada habitual. El demo recibe el mismo protocolo,
valida CRC y versión, comprueba que cada payload sea JSON `request/send`, y
verifica resolución, formato, secuencia de índices y cantidad total de bytes.
No decodifica ni dibuja los píxeles.

El significado de sus tres LEDs activos en alto es:

| LED | GPIO | Indicación |
|---|---:|---|
| Inicio | 25 | Primer chunk válido de una transferencia |
| Paquete | 26 | Pulso de 40 ms por cada chunk válido recibido |
| Finalización | 27 | Todos los chunks y bytes esperados fueron recibidos; respuesta `ok` enviada |

El UART de datos de este demo usa UART0 a 921600 baudios, 8N1, mediante el
puente USB-UART integrado de la placa (normalmente aparece como `/dev/ttyUSB0`).
No conectar un adaptador UART externo a GPIO1/GPIO3 mientras se use ese puente.
La consola de depuración comparte UART0, por lo que sus mensajes pueden
interferir con el protocolo durante la transferencia. Los tres LEDs externos
requieren resistencias limitadoras y GND común.

Para seleccionar el demo, pasar `-D SCREEN_COMM_DEMO=ON` al configure/build de
ESP-IDF (por ejemplo, `idf.py -D SCREEN_COMM_DEMO=ON build`). El firmware normal
se mantiene seleccionando la opción como `OFF`.

## Ejemplo C del anfitrión

Compilar el driver y el programa de demostración:

```sh
make screen-driver-demo
```

El ejecutable generado es `output/screen-driver-demo`. Uso:

```sh
output/screen-driver-demo /dev/ttyUSB0 imagen.raw [1|3]
```

El archivo debe contener únicamente los píxeles en orden de filas, sin PNG/JPEG
ni cabecera: 20 480 bytes para `GRAY8` (canales `1`) o 61 440 bytes para
`BGR888` (canales `3`, valor predeterminado). El ejemplo ejecuta el ciclo
completo `open → send_image → close`. El envío solo completará correctamente
cuando el firmware ESP32 implemente el receptor y responda con los ACK y el
mensaje de aplicación definidos en este documento.
