/*
  ============================================================================
  EJEMPLO 3 de 5 - HILOS REALES Y PARALELISMO CON DOS NUCLEOS (FreeRTOS/ESP32)
  ============================================================================
  Placa: ESP32 DevKit (simulable en Wokwi)
  Autor: Cristian Carrera 2024-1932 - ITLA - IA e IoT 2026-C-2

  Aqui se demuestran las DOS ideas separadas que la gente suele confundir:

  (a) CONCURRENCIA CON EXPULSION (preemptive multitasking)
      Dos tareas comparten el Nucleo 1. Una de ellas, "tareaTragona", esta
      escrita a proposito con un delay() de 3 segundos y calculos pesados.
      En el modelo cooperativo del Ejemplo 2 eso habria congelado todo.
      Aqui NO pasa nada: cuando la tareaTragona se duerme o agota su turno,
      el planificador la EXPULSA y le entrega la CPU a la tarea de tiempo
      real. El LED de latido sigue impecable a 1 Hz.

  (b) PARALELISMO VERDADERO
      El ESP32 tiene dos nucleos fisicos. "tareaNucleo0" y "tareaNucleo1"
      corren AL MISMO TIEMPO, cada una en su propio nucleo. Ambas cuentan lo
      mas rapido que pueden durante 1 segundo e imprimen su resultado junto
      con xPortGetCoreID(). Que ambas avancen simultaneamente es la prueba
      de que esto ya no es "turnarse rapido": es ejecutarse a la vez.

  QUE OBSERVAR EN LA DEMOSTRACION
  -------------------------------
  1. El LED de latido nunca se altera, ni siquiera durante los 3 s de la
     tarea tragona. Compare con el Ejemplo 1.
  2. El monitor serie imprime "Nucleo 0" y "Nucleo 1" intercalados, con sus
     contadores creciendo en paralelo.
  3. La prioridad manda: al subir la prioridad de la tarea de latido por
     encima de la tragona, la latencia se mantiene por debajo de 1 ms.
  ============================================================================
*/

const int LED_LATIDO = 2;    // LED integrado del ESP32 DevKit

// ---------------------------------------------------------------------------
// TAREA DE TIEMPO REAL - prioridad ALTA (3)
// Debe encender/apagar el LED con un periodo exacto. Se usa vTaskDelayUntil
// (no vTaskDelay) porque compensa el tiempo que la tarea tardo en ejecutarse
// y evita la deriva acumulativa del reloj.
// ---------------------------------------------------------------------------
void tareaLatido(void *p) {
  TickType_t ultimoDespertar = xTaskGetTickCount();
  bool encendido = false;
  uint32_t pulsos = 0;
  uint32_t t0 = millis();

  for (;;) {
    encendido = !encendido;
    digitalWrite(LED_LATIDO, encendido);

    if (++pulsos % 20 == 0) {   // cada 10 s reporta su exactitud
      uint32_t transcurrido = millis() - t0;
      uint32_t esperado     = (pulsos - 1) * 500;   // el 1er pulso ocurre en t=0
      Serial.printf("[LATIDO ] %u pulsos en %u ms  -> deriva: %d ms\n",
                    pulsos, transcurrido, (int)transcurrido - (int)esperado);
    }
    vTaskDelayUntil(&ultimoDespertar, pdMS_TO_TICKS(500));
  }
}

// ---------------------------------------------------------------------------
// TAREA "TRAGONA" - prioridad BAJA (1), en el MISMO nucleo que el latido
// Simula una rutina mal escrita: bloquea 3 segundos y ademas quema CPU.
// El objetivo es probar que el planificador la expulsa sin piedad.
// ---------------------------------------------------------------------------
void tareaTragona(void *p) {
  for (;;) {
    Serial.println(F("[TRAGONA] Empiezo un bloqueo de 3 segundos..."));
    delay(3000);   // en FreeRTOS, delay() cede la CPU: la tarea se suspende

    Serial.println(F("[TRAGONA] Ahora quemo CPU sin ceder nada (2 s)..."));
    uint32_t fin = millis() + 2000;
    volatile double basura = 0;
    while (millis() < fin) basura += 1.0 / (millis() + 1.0);

    Serial.printf("[TRAGONA] Termine. (el LED nunca se detuvo) %.3f\n",
                  (double)basura);
  }
}

// ---------------------------------------------------------------------------
// PAR DE TAREAS GEMELAS - una anclada a cada nucleo fisico.
// Demuestran PARALELISMO: los dos contadores avanzan en el mismo instante.
// ---------------------------------------------------------------------------
void tareaContadoraParalela(void *parametroNombre) {
  const char *nombre = (const char *)parametroNombre;

  for (;;) {
    uint32_t cuenta = 0;
    uint32_t fin = millis() + 1000;
    while (millis() < fin) cuenta++;

    Serial.printf("[%s] corriendo en Nucleo %d -> %u iteraciones en 1 s\n",
                  nombre, xPortGetCoreID(), cuenta);
    vTaskDelay(pdMS_TO_TICKS(1500));
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(LED_LATIDO, OUTPUT);

  Serial.println(F("\nEJEMPLO 3 - Hilos FreeRTOS con expulsion y dos nucleos"));
  Serial.printf("Frecuencia del tick del planificador: %d Hz\n",
                configTICK_RATE_HZ);
  Serial.printf("setup() se esta ejecutando en el Nucleo %d\n\n",
                xPortGetCoreID());

  // (a) Concurrencia con expulsion: las dos comparten el Nucleo 1.
  //                       funcion       nombre     pila  param prio handle nucleo
  xTaskCreatePinnedToCore(tareaLatido,  "Latido",  2048, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(tareaTragona, "Tragona", 4096, NULL, 1, NULL, 1);

  // (b) Paralelismo verdadero: una tarea anclada a cada nucleo fisico.
  static const char *n0 = "PAR-A";
  static const char *n1 = "PAR-B";
  xTaskCreatePinnedToCore(tareaContadoraParalela, "ParA", 2048, (void *)n0, 2, NULL, 0);
  xTaskCreatePinnedToCore(tareaContadoraParalela, "ParB", 2048, (void *)n1, 2, NULL, 1);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
