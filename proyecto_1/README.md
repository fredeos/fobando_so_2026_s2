# Ejecutar el proyecto: host, ESP32 y Pico H

Ejecutar los comandos desde la carpeta `proyecto_1` del repositorio. Se necesita
Linux, el host compilado y ambos nodos con el firmware principal cargado.
`make help` resume los comandos y `make ports` muestra los puertos conectados.

## 1. Permisos y puertos

Conectar ambas placas y cerrar cualquier monitor serial antes de ejecutar el host.
Comprobar los grupos de la terminal:

```bash
id -nG
```

Si el usuario todavía no pertenece a `dialout`, agregarlo y cerrar/abrir sesión:

```bash
sudo usermod -aG dialout "$USER"
```

Si ya pertenece al grupo pero la terminal no lo muestra, ejecutar `newgrp dialout`.
Este comando abre otra shell: hacerlo **antes** de activar ESP-IDF y definir las
variables de esta guía.

```bash
cd /ruta/al/repositorio/proyecto_1
make ports
```

Reemplazar esa ruta por la ubicación del proyecto en cada computadora. Definir
los puertos según la salida de `make ports`. Por ejemplo, con los dispositivos
usados en esta prueba:

```bash
export ESP_PORT=/dev/serial/by-id/usb-Silicon_Labs_CP2102_USB_to_UART_Bridge_Controller_0001-if00-port0
export PICO_PORT=/dev/serial/by-id/usb-Raspberry_Pi_Pico_E6616407E33C5831-if00
```

La identidad de la Pico puede cambiar entre placas. Cada compañero debe usar
las rutas que aparezcan en su computadora. Las rutas `by-id` se conservan aunque
el sistema cambie `ttyUSB0` por `ttyUSB1` o `ttyACM0` por `ttyACM1`.
Como alternativa, se pueden pasar los puertos actuales directamente:

```bash
make run ESP_PORT=/dev/ttyUSB0 PICO_PORT=/dev/ttyACM0
```

## 2. Ejecución diaria: firmware ya cargado

No se requiere activar ESP-IDF ni compilar/cargar de nuevo los nodos si su código
no cambió. Desde una terminal nueva:

1. Entrar a `proyecto_1` y definir los puertos como en el apartado anterior.
2. Para una sesión nueva, reiniciar ambas placas desconectándolas y
   reconectándolas, sin mantener BOOTSEL presionado. También se puede reiniciar
   el ESP con EN/RESET. Esperar a que vuelvan a aparecer los puertos.
3. Ejecutar:

```bash
make run
```

`make run` compila el host si hace falta, valida que los puertos existan y tengan
permisos de lectura/escritura y ejecuta `output/host` con ESP primero y Pico después.
Usa las variables exportadas; sin ellas, usa `/dev/ttyUSB0` y `/dev/ttyACM0`.
No es necesario ejecutar `stty` ni el script de diagnóstico previo.

El host realiza CONFIG, calibración y despliegue del CSV. Esperar a que aparezca
la consola `>`. Durante la ejecución se pueden agregar tareas manuales una por
comando. Usar el formato `add id,P|S,W,T,D,criticidad`; por ejemplo:

```text
add 31,P,20,50,40,2
add 32,S,10,100,80,-1
```

`P` indica periódica y `S` esporádica; `W` es trabajo en UT, `T` el período o
intervalo mínimo en ticks, `D` el plazo relativo en ticks y la criticidad puede
ser negativa. Cada tarea pasa por el mismo planificador de admisión y asignación
que el CSV, se despliega al nodo asignado y se incluye en el reporte tras su
ACK. El ID debe ser único entre las tareas ya admitidas. El scheduler no se
detiene ni reinicia al agregar una tarea; las tareas periódicas nuevas comienzan
su fase de liberación desde el tick en que son registradas.

La sesión genera los reportes al cerrarse en una carpeta nueva dentro de
`output/`. Escribir los comandos **dentro del host**, no en Bash:

```text
start
```

Comandos disponibles:

| Comando | Acción |
|---|---|
| `add id,P|S,W,T,D,criticidad` | Admitir y desplegar una tarea manual durante la sesión |
| `start` / `stop` | Iniciar o pausar ambos nodos |
| `start 1` / `stop 1` | Controlar solo ESP32 |
| `start 2` / `stop 2` | Controlar solo Pico |
| `q` | Solicitar STOP, cerrar la sesión y generar `report.html` y `events.csv` |

STOP conserva el estado preparado y START lo reanuda. Antes de volver a abrir
el host para una sesión nueva, reiniciar las placas: el firmware actual rechaza
CONFIG si el scheduler ya fue preparado por un START anterior. Los nodos envían
al host sus eventos de ejecución y de plazo perdido por el mismo enlace serial;
el Host los incorpora al reporte sin recalcular los plazos. Las tramas completas
con CRC inválido se descartan y se contabilizan en el reporte. Reiniciar ambas
placas antes del siguiente intento.

La sesión del Host se divide explícitamente en cuatro procesos: el coordinador
principal, el reporter, un worker serial para el ESP32 y otro para el Pico. El
coordinador admite y despliega tareas y procesa `add`, `start`, `stop` y `q`;
cada worker es el único propietario de su enlace serial. Los workers publican
eventos en la cola compartida y reciben órdenes del coordinador mediante pipes.
El reporter consume esa cola y genera los archivos de salida.

La cola interna que comunica los procesos del Host usa un objeto de memoria
compartida POSIX (`shm_open`/`mmap`) y semáforos POSIX compartidos entre procesos.
Los hijos cierran los extremos de pipes que no utilizan para que la terminación
inesperada de un worker sea detectable. Al cerrar la sesión, el proceso
propietario espera a sus procesos hijos, destruye los semáforos y elimina el
objeto compartido.

## 3. Compilar y cargar el ESP32

Se requiere una instalación funcional de ESP-IDF. La prueba actual usó v5.4.2.

El firmware con comunicación y reportes necesita una pila de 8192 bytes para
la tarea `main`, definida en `src/nodes/esp32/sdkconfig.defaults`. Si ya existe
un `sdkconfig` anterior, configurar `CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192` en
`idf.py menuconfig` (Component config > ESP System Settings > Main task stack
size) y volver a compilar/cargar el ESP. El valor anterior de 3584 bytes puede
provocar un desbordamiento de pila al responder CONFIG.

Activar la instalación propia, por ejemplo:

```bash
source "$HOME/esp/esp-idf/export.sh"
```

En la computadora donde se hicieron las pruebas se usaron estas rutas temporales:

```bash
export IDF_TOOLS_PATH=/tmp/esp-local-tools
source /tmp/esp-local-idf/export.sh
```

Estas carpetas de `/tmp` pueden desaparecer al reiniciar y no son rutas
universales para los compañeros. Después de activar ESP-IDF, desde `proyecto_1`:

```bash
make esp-build ESP_LOCAL_TEST=OFF IDF_ACTIVATE="$IDF_PATH/export.sh"
make esp-flash ESP_PORT="$ESP_PORT" IDF_ACTIVATE="$IDF_PATH/export.sh"
```

Definir antes `ESP_PORT` como en el apartado 1. El binario se genera en
`src/nodes/esp32/build/esp32/esp32_node.bin`. La regla de flash usa ESP-IDF para
cargar el firmware y sus imágenes auxiliares.

También se conserva el comando `make esp-flash DEV=USB0`, que selecciona
`/dev/ttyUSB0` si no se exportó ni se pasó `ESP_PORT`. Un `ESP_PORT` explícito
tiene prioridad sobre DEV.

El monitor opcional se abre con:

```bash
make esp-monitor IDF_ACTIVATE="$IDF_PATH/export.sh"
```

Salir con Ctrl+] antes de abrir el host.

## 4. Compilar y cargar la Pico H

Se requiere el SDK de Pico con sus dependencias y la herramienta de compilación
ARM. Usar la ruta de la instalación propia. En la computadora de las pruebas:

```bash
make pico-build PICO_LOCAL_TEST=OFF \
  PICO_SDK_PATH=/home/maarigonzalezz/Documentos/pico-sdk \
  CMAKE=/usr/bin/cmake
```

Cambiar la ruta del SDK para cada compañero. También se puede exportar
`PICO_SDK_PATH` una vez: el Makefile respeta ese valor.
El firmware se genera en:

```text
src/nodes/pico/build/pico/pico.uf2
```

Para cargarlo:

1. Desconectar la Pico.
2. Mantener BOOTSEL presionado mientras se conecta por USB; soltarlo después.
3. Copiar `pico.uf2` a la unidad `RPI-RP2` que aparece en el administrador de archivos.
4. La placa se reinicia y vuelve a aparecer su puerto serial. Ejecutar `make ports`
   si hace falta comprobarlo.

Después de cargar ambas placas, seguir el apartado de ejecución diaria.

## 5. Configuración y observación de la prueba

- `config/system.conf`: algoritmo RMS/EDF, heurística y duración del tick.
  Actualmente `tick_ms=1000` equivale a un segundo nominal por tick.
- `connection_timeout_ms` define en milisegundos cuánto espera el Host sin
  actividad serial antes de cerrar y reconectar un enlace. Configúralo por
  encima del tiempo que necesita la calibración; el valor actual es 6000 ms.
- `config/tasks.csv`: tareas; W se expresa en UT y T/D en ticks.
- Cambiar esos archivos requiere una sesión nueva del host con las placas
  reiniciadas. No requiere recompilar el firmware.
- Cambiar código C del nodo requiere compilar y cargar de nuevo esa placa.
  Cambiar código del host requiere recompilarlo; `make run` lo hace si corresponde.

| Placa | LEDs de tarea, bit menor a mayor | LED de plazo perdido | Botón |
|---|---|---|---|
| ESP32, nodo 1 | GPIO18, 19, 21, 22 | GPIO15 | GPIO27 |
| Pico H, nodo 2 | GP10, 11, 12, 13 | GP25 | GP18 |

Los botones usan pull-down externo, presionado = 3.3 V, con debounce de 30 ms.
Los cuatro LEDs representan el ID seleccionado en binario; apagados indican
ningún trabajo seleccionado durante ese tick. El LED de plazo perdido se
enciende durante el tick lógico que detecta una nueva pérdida de plazo.
El patrón 1111 también se utiliza para indicar ciertos errores.

Una pulsación solicita la tarea esporádica del nodo. No necesariamente se ejecuta
inmediatamente: el scheduler decide cuándo seleccionarla y el intervalo mínimo
entre eventos puede rechazar pulsaciones demasiado próximas.

El CSV actual contiene 15 tareas, tres esporádicas. La implementación admite una
esporádica por nodo; con dos nodos se pueden admitir como máximo dos de ellas.
Revisar el despliegue que imprime el host para conocer la asignación efectiva.
Con períodos de 20 a 64 ticks y trabajo de un tick por tarea, son esperables
intervalos con los LEDs apagados.

## 6. Prueba autónoma opcional

El modo normal es el main de comunicación con aportes de LEDs/botones. La prueba
local de cinco tareas es opcional, no se comunica con el host:

```bash
make esp-build ESP_LOCAL_TEST=ON ESP_ALGORITHM=RMS ESP_TICK_MS=1000 \
  IDF_ACTIVATE="$IDF_PATH/export.sh"
make pico-build PICO_LOCAL_TEST=ON PICO_SDK_PATH=/ruta/pico-sdk
```

Estos comandos solo compilan: cargar después el firmware en cada placa.
Para volver al sistema con host, compilar con `ESP_LOCAL_TEST=OFF` y
`PICO_LOCAL_TEST=OFF`, cargar nuevamente ambos firmwares y reiniciar la sesión.
