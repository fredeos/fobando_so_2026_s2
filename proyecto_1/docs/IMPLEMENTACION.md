## 1. Funcionamiento previsto

1. El **Host** lee `system.conf` y `tasks.csv`, inicia procesos y solicita la calibración de cada nodo (**UT/tick**, medida en cada ejecución).
2. El **planificador** calcula `C(i,n) = ceil(W_i / s_n)`, ordena las tareas por utilización en el nodo más rápido y asigna mediante **First-Fit** o **Worst-Fit**. Comprueba admisión **EDF** (`Σ C/T ≤ 1`) o **RMS** (`Σ C/T ≤ n(2^(1/n) − 1)`).
3. Los **drivers** despliegan las tareas admitidas por USB serial. Cada **nodo** ejecuta su propio RMS/EDF en ticks de hardware, el Host **no** decide qué ejecutar en cada tick.
4. Los drivers publican eventos y notifican fallas al planificador, el proceso de **reportes** consume una cola compartida y genera estadísticas y Gantt. Con `q` se detiene y libera todo ordenadamente.

El Host final debe tener **al menos cuatro procesos**: planificador, un driver por nodo y reportes. IPC mediante **memoria compartida POSIX y semáforos**, sin *busy waiting*.

## 2. Archivos existentes y función

La columna «Estado» distingue la lógica ya escrita del funcionamiento integrado. 

| Archivo(s) | Función | Estado |
|---|---|---|
| `include/common/task.h` | Define `Task`: ID, tipo P/S, trabajo, período, plazo y criticidad. | Base lista |
| `include/common/events.h` | Define `Event`, tipos de evento y motivos de rechazo para reportes/Host. **No es una trama serial.** | Base lista |
| `include/common/protocol.h` | Constantes de trama y mensajes seriales. | Parcial |
| `src/common/protocol.c` | Implementa CRC-16, faltan codificación y decodificación de tramas. | Parcial |
| `include/host/node.h` + `src/host/planner/node.c` | Estado/capacidad del nodo, tareas asignadas, agregar, buscar y quitar tareas, utilización. | Lógica lista |
| `include/host/admission.h` + `src/host/planner/admission.c` | Valida tareas, calcula costo y comprueba admisión EDF/RMS. | Lógica lista |
| `include/host/planner.h` + `src/host/planner/planner.c` | First-Fit, Worst-Fit, ordenamiento de carga inicial y decisiones de aceptación/rechazo. | Lógica lista |
| `include/host/failover.h` + `src/host/planner/failover.c` | Marca nodo caído, conserva asignaciones sobrevivientes y reubica periódicas priorizando criticidad, rechaza esporádicas. | Pruebas aprobadas |
| `include/host/manual_tasks.h` + `src/host/planner/manual_tasks.c` | Interpreta tarea manual, reserva capacidad, confirma tras ACK o cancela reserva. | Lógica lista |
| `include/host/config.h` + `src/host/config/config.c` | Lee y valida algoritmo, heurística, tick, puertos, supervisión y archivo de tareas. | Lógica lista |
| `include/host/task_parser.h` + `src/host/config/task_parser.c` | Lee `id,tipo,W,T,D,criticidad`, detecta valores/IDs inválidos y libera memoria. | Lógica lista |
| `src/host/main.c` | Carga archivos y ejecuta una **demo** con velocidades suministradas, aún no inicia el sistema multiproceso real. | Parcial |
| `config/system.conf` y `config/tasks.csv` | Parámetros de ejecución y carga de trabajo, el formato del CSV es fijo. | Son ejemplos listos se pueden cambiar|
| `tests/test_foundation.c`, `test_planner.c`, `test_config.c`, `test_task_parser.c`, `test_failover.c`, `test_manual_tasks.c` | Pruebas unitarias de los componentes anteriores. | Son pruebas, las estoy usando tipo testbench, todas han pasado |
| `Makefile` | Compila Host y tests en `debug`/`release`, excluye firmware. | Base lista |
| `src/nodes/{esp32,pico}/ut.c` y `ut.h` | Función `unidad_trabajo()` proporcionada por Barboza, **tiene que quedarse intacta como esta en ambos nodos**. | Metidos en las carpetas |

## 3. Archivos pendientes: qué implementar y cómo conectarlos (es una idea, si les sirve otra, jpya)

| Archivo(s) previsto(s) | Implementación e integración |
|---|---|
| `src/common/protocol.c` (+ `protocol.h`) | Completar serialización/deserialización de mensajes con inicio, tipo, longitud, payload y CRC. Documentar orden de bytes, secuencias, ACK/NACK y descarte de tramas corruptas. Los nodos deben usar el mismo contrato. |
| `include/host/serial.h` + `src/host/drivers/serial.c` | Abrir/configurar USB serial, leer/escribir tramas completas y manejar errores y desconexiones. **Aqui chambea Fred** |
| `include/host/driver.h` + `src/host/drivers/driver.c` | Un proceso por nodo: configurar tick, calibrar, desplegar, iniciar, recibir eventos, detectar reportes perdidos y notificar fallas. Convertir mensajes seriales a `Event`. |
| `include/host/ipc.h` + `src/host/ipc/ipc.c` | Crear/cerrar memoria compartida POSIX, semáforos y canales para órdenes, respuestas y notificaciones entre procesos. |
| `include/host/event_queue.h` + `src/host/ipc/event_queue.c` | Cola de eventos: drivers/planificador producen y reportes consume, proteger con semáforos sin espera activa. |
| `include/host/report.h` + `src/host/report/report.c` | Consumir `Event` desde la cola, organizar una ejecución y exportar archivos al detener. **Esto me falta** |
| `include/host/statistics.h` + `src/host/report/statistics.c` | Estadísticas por tarea/nodo: liberaciones, finalizaciones, plazos perdidos, fallas, cambios, utilización teórica/medida y latencia esporádica. **Esto me falta**|
| `include/host/gantt.h` + `src/host/report/gantt.c` | Reconstruir por nodo los intervalos `EVENT_EXECUTION_SLICE` / `EVENT_IDLE_SLICE` y generar Gantt, por ejemplo en SVG. **Esto me falta**|
| `src/nodes/esp32/*`, `src/nodes/pico/*` | Firmware en C/C++: temporizador de hardware, RMS/EDF **local**, ≥8 tareas, una esporádica por nodo con botón/interrupt/debounce, LED de tarea y de plazo perdido, aborto de trabajos vencidos, calibración UT/tick y buffer de eventos no bloqueante. Seguir ejecutando si el Host se desconecta. **Chamba de Brayan y Mariana** |
| `src/host/main.c` (ampliación) | Sustituir demo por inicialización de IPC/procesos, coordinación de calibración, despliegue, ejecución y apagado con `q`. Creo que es mejor no meter aquí algoritmos locales ni lógica serial de bajo nivel. |
| `docs/architecture.md`, `docs/protocol.md`, `docs/experiments.md`, `README.md` | Describir procesos/IPC, bytes y tipos de mensajes, experimentos exigidos, compilación, carga y ejecución. |

## 4. Contratos de integración indispensables

- **Calibración y planificador:** cada driver entrega `node_id` y `ut_per_tick > 0`, el proceso planificador crea/actualiza el nodo antes de llamar a `planner_plan_batch(...)`. En producción **no usar** velocidades de la demo.
- **Planificador y driver:** cada `PlannerDecision` aceptada aporta `task_id`, `node_id` y costo, el driver envía la definición completa `Task` al nodo y comunica su ACK o error. Los rechazos se convierten en eventos para reportes.
- **Alta manual:** `manual_task_submit(...)` **reserva**, pero no significa tarea desplegada, confirmar con `manual_task_confirm(...)` **solo tras ACK**. Si falla, ejecutar `manual_task_cancel(...)`. Mantener las solicitudes pendientes identificables entre mensajes.
- **Falla:** el driver notifica `failed_node_id`, el proceso planificador ejecuta `failover_handle_node_failure(...)`, `FailoverResult` indica qué periódicas desplegar de nuevo y qué tareas rechazar. Las asignaciones de nodos sanos no se reinician.
- **Eventos y reportes:** publicar `Event` por **IPC**, incluyendo nodo, tarea, instancia, tick, duración de cada ejecución/idle, liberaciones, finalizaciones y fallas. El proceso de reportes **no** debe leer directamente el estado privado de los drivers.
- **Serial no es memoria compartida:** enviar campos serializados explícitamente, **no** transmitir `struct Task` o `struct Event` como bytes de memoria por USB.

**Comandos de referencia:** `make`, `make test` para los tb, `make demo`, `make clean`, `make run` para el main. El objetivo `demo` valida únicamente la **planificación lógica**, no el sistema distribuido en ejecución real, era para ver si servía.

