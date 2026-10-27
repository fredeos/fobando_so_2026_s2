# Aporte de LEDs y botón sobre el main de comunicación

El programa principal de cada placa es `main.c`, basado en la comunicación de
feature/edf (commit f1ecf91). Conserva sus funciones serialize_reply, calibrate,
prepare_response y run_scheduler_if_due, así como su bucle RX/TX.

El aporte de hardware está dentro del mismo main, agrupado como `hardware_*`:

- hardware_show_selection: observador del scheduler, llamado antes de las UT.
- hardware_button_irq: registra el último flanco del botón.
- hardware_debounce_irq: detecta 30 ms estables mediante una alarma auxiliar.
- hardware_button_poll: entrega la pulsación a scheduler_runtime_trigger_sporadic.
- hardware_init: prepara el LED de plazo y el botón con interrupción.

Todas las lecturas/escrituras y direcciones GPIO usan board_gpio_* de gpio.c.
La alarma de antirrebote es independiente del tick del scheduler; no imprime
mensajes ni decide qué trabajo se ejecuta. No se reorganizó el protocolo.

| Placa | LEDs de tarea, bit menor a mayor | Plazo perdido | Botón pull-down externo |
|---|---|---|---|
| Pico H | GP10, GP11, GP12, GP13 | GP25 | GP18 |
| ESP32 | GPIO18, GPIO19, GPIO21, GPIO22 | GPIO15 | GPIO27 |

Los cuatro LEDs muestran el identificador de tarea en binario (cuatro bits).
Todos apagados significa idle; 1111 se utiliza como indicación de error de arranque.
El LED de plazo se enciende solo en el tick lógico que detecta un nuevo fallo.
Mantener el botón presionado libera una sola solicitud. Soltarlo rearma la entrada
tras el debounce. El runtime existente comprueba el intervalo mínimo esporádico.
STOP apaga las salidas y limpia las pulsaciones pendientes. START conecta el
observador a la instancia del scheduler que preparó el código de comunicación.

## Programa principal

Desde proyecto_1, los comandos normales compilan el main de comunicación:

```bash
make esp-build
make pico-build
```

Se requiere activar ESP-IDF y tener configuradas las rutas del SDK de Pico,
como antes. Algoritmo, tareas y tick del programa principal llegan desde el host.
El flujo de mensajes, el avance de ticks y la política de despliegue permanecen
como en la implementación de comunicación del compañero.

## Prueba autónoma opcional

El código de las cinco tareas quedó separado en:

- src/nodes/esp32/main/local_test_main.c
- src/nodes/pico/local_test_main.c

Se activa de forma explícita:

```bash
make esp-build ESP_LOCAL_TEST=ON ESP_ALGORITHM=RMS ESP_TICK_MS=1000
make pico-build PICO_LOCAL_TEST=ON
```

Pico usa LOCAL_ALGORITHM y LOCAL_TICK_MS de su configuración de CMake para la
prueba; sus valores predeterminados son RMS y 1000 ms. El Makefile pasa el modo
a CMake para que un `make pico-build` posterior vuelva al main principal incluso
si la carpeta de compilación tenía el test seleccionado en la caché.

```bash
make test
make test-esp-local
make test-pico-local
```

Las últimas dos reglas son pruebas de escritorio de las tareas/debounce; para
probar LEDs físicos se compila y carga el firmware autónomo de la placa.

## Alcance de la restauración

Se retiraron node_control, node_link, sus pruebas y la documentación que dependía
de esa reorganización. También se restauraron el host, el transporte serial y el
runtime del compañero a la versión del merge, deshaciendo las modificaciones de
comunicación del paso anterior. Se conservaron los cambios Task -> SchedulerTask
necesarios para que los arreglos locales compilen.

La compilación Pico incluye sys/types.h para compatibilidad con las macros de
64 bits de la versión instalada de Newlib, sin modificar el serializer común.
Las advertencias sobre variables RX/TX sin utilizar pertenecen al main de origen;
no se cambió la lógica de comunicación para eliminarlas.

La temporización del scheduler y los pendientes de comunicación señalados en la
revisión siguen correspondiendo a esa implementación. Esta entrega integra el
hardware y conserva el test; no pretende resolver ni reemplazar ese subsistema.
