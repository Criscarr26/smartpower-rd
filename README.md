# SmartPower RD

Sistema IoT de monitoreo eléctrico con transferencia automática a inversor y notificación remota,
sobre una arquitectura multitarea (FreeRTOS) en ESP32.

| | |
|---|---|
| **Asignatura** | Inteligencia Artificial e Internet de las Cosas — 2026-C-2 |
| **Facilitador** | Luis Bessewell Feliz |
| **Sustentante** | Cristian Carrera — Matrícula 2024-1932 |
| **Correo** | 20241932@itla.edu.do |
| **Institución** | Instituto Tecnológico de Las Américas (ITLA) |

---

## Contenido

```
SmartPowerRD/                        proyecto principal (ESP32 + FreeRTOS)
  SmartPowerRD.ino                     5 tareas, colas, mutex y semáforos
  diagram.json                         montaje listo para el simulador Wokwi
  libraries.txt                        dependencias

ejemplos_concurrencia/               las seis demostraciones del informe
  01_secuencial_bloqueante/            el problema: delay() congela todo
  02_multitarea_cooperativa/           concurrencia con millis(), un núcleo
  03_hilos_freertos/                   expulsión por prioridad y dos núcleos
  04_condicion_de_carrera_mutex/       la carrera y su corrección con mutex
  05_distribuido_mqtt/                 nodo ESP32 + concentrador en Python
  06_python_hilos_vs_procesos/         medición con cronómetro (GIL)
```

---

## Cómo ejecutarlo

**En el simulador (sin hardware):** entra a [wokwi.com](https://wokwi.com), crea un proyecto nuevo
—Arduino UNO para los ejemplos 1 y 2, ESP32 para el resto—, pega el `.ino` en la pestaña del sketch
y el `diagram.json` en la pestaña de ese nombre. El montaje aparece ya cableado.

Para el proyecto principal hacen falta dos librerías: **LiquidCrystal I2C** (Wokwi ofrece
instalarla sola al primer intento) y **PubSubClient** (añadir la línea en `libraries.txt`).

**En Python (ejemplo 6):** no requiere instalar nada.

```bash
python ejemplos_concurrencia/06_python_hilos_vs_procesos/concurrencia_comparada.py
```

El archivo `salida_de_la_corrida.txt` guarda la salida real de la ejecución que se cita en el
informe: aceleración de 7.87× con hilos en trabajo de espera, 0.99× en trabajo de CPU, y 74.9 % de
operaciones perdidas por no proteger un dato compartido.

---

## Arquitectura de tareas del proyecto principal

| Tarea | Núcleo | Prioridad | Período | Responsabilidad |
|---|---|---|---|---|
| Muestreo | 1 | 3 | 50 ms | Lee el ADC y publica en la cola |
| Control | 1 | 2 | por evento | Máquina de estados y relé de transferencia |
| Pantalla | 1 | 1 | 500 ms | LCD I2C |
| Red | 0 | 1 | 5 s o evento | WiFi y MQTT |
| Diagnóstico | 0 | 0 | 10 s | Pila libre por tarea y heap |

Mecanismos de sincronización: cola productor/consumidor, mutex sobre el estado compartido,
semáforo binario para señalizar eventos y `portMUX_TYPE` para la sección crítica de la interrupción.

---

## Aviso

`test.mosquitto.org` es un broker **público**: sirve para la demostración, pero no debe usarse en una
instalación real, porque el patrón de consumo eléctrico revela cuándo la vivienda está vacía.

Antes de conectar nada a 120 V AC reales, trabajar siempre con la instalación desenergizada.
