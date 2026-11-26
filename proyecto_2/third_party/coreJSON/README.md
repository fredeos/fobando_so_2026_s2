# coreJSON v3.3.1

Biblioteca de terceros, conservada sin cambios, con licencia MIT en `LICENSE`.

- Repositorio: <https://github.com/FreeRTOS/coreJSON/tree/v3.3.1>
- `core_json.c`: `source/core_json.c` del tag `v3.3.1`.
- `core_json.h`: `source/include/core_json.h` del mismo tag.
- Documentación: <https://freertos.github.io/coreJSON/v3.3.1/>

Es C portable, no requiere FreeRTOS, llamadas al sistema operativo ni heap.
Valida el documento completo y permite recorrer objetos con puntero y longitud.
Se compila desde estas mismas fuentes en Linux, ESP32 y Raspberry Pi Pico,
sin descargas adicionales durante la compilación. Se conserva el límite de
anidamiento predeterminado de la biblioteca: 32 niveles.

El contrato de aplicación, los rangos y el límite de 256 bytes se verifican
en `src/common/message_parser.c`, no en esta biblioteca.
