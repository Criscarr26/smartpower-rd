/*
  ============================================================================
  EJEMPLO 4 de 5 - CONDICION DE CARRERA Y SU CURA: EL MUTEX
  ============================================================================
  Placa: ESP32 DevKit (simulable en Wokwi)
  Autor: Cristian Carrera 2024-1932 - ITLA - IA e IoT 2026-C-2

  El error mas caro de la programacion concurrente no es que el programa se
  caiga: es que devuelva un numero equivocado sin avisar. Este sketch lo
  reproduce y luego lo corrige, en la misma ejecucion, para poder comparar.

  EL ESCENARIO (tomado del proyecto SmartPower RD)
  ------------------------------------------------
  Dos sensores de consumo, uno por circuito de la casa, acumulan energia en
  un mismo totalizador compartido "energiaTotal". Cada uno suma 1.0 Wh, mil
  veces. El resultado correcto es, obviamente, 2000.0 Wh.

  POR QUE FALLA
  -------------
  La linea "energiaTotal += 1.0" NO es una operacion atomica. El compilador
  la traduce en tres pasos de maquina:

        1. LEER   energiaTotal  ->  registro
        2. SUMAR  1.0           al registro
        3. ESCRIBIR el registro ->  energiaTotal

  Si el planificador expulsa a la tarea A entre el paso 1 y el 3 (o si la
  tarea B corre literalmente al mismo tiempo en el otro nucleo), ambas leen
  el MISMO valor viejo y ambas escriben el mismo valor nuevo. Una de las dos
  sumas se pierde para siempre. A esto se le llama "actualizacion perdida".

  LA CURA
  -------
  Un MUTEX (mutual exclusion) convierte esos tres pasos en una SECCION
  CRITICA indivisible: quien lo toma es el unico que puede tocar la variable,
  y los demas esperan su turno.

  QUE OBSERVAR EN LA DEMOSTRACION
  -------------------------------
  Fase 1 (sin mutex): el total impreso es MENOR que 2000 y cambia en cada
  ejecucion. Esa inestabilidad es la firma inconfundible de una condicion
  de carrera.
  Fase 2 (con mutex): el total es exactamente 2000.00, siempre.
  ============================================================================
*/

const uint32_t SUMAS_POR_TAREA = 1000;

volatile double   energiaTotal = 0.0;   // el recurso compartido en disputa
SemaphoreHandle_t mutexEnergia = NULL;  // el candado
SemaphoreHandle_t semFinalizado = NULL; // semaforo contador: avisa "ya termine"
bool              usarMutex = false;    // interruptor entre las dos fases

// ---------------------------------------------------------------------------
// Tarea trabajadora. Se crean DOS instancias identicas, una en cada nucleo.
// ---------------------------------------------------------------------------
void tareaSensor(void *nombre) {
  for (uint32_t i = 0; i < SUMAS_POR_TAREA; i++) {

    if (usarMutex) {
      // ---------- SECCION CRITICA PROTEGIDA ----------
      xSemaphoreTake(mutexEnergia, portMAX_DELAY);
      double copia = energiaTotal;   // 1. leer
      copia = copia + 1.0;           // 2. sumar
      delayMicroseconds(1);          //    (ensancha la ventana a proposito)
      energiaTotal = copia;          // 3. escribir
      xSemaphoreGive(mutexEnergia);
      // -----------------------------------------------
    } else {
      // ---------- LA MISMA SECCION, SIN PROTEGER ----------
      double copia = energiaTotal;   // 1. leer
      copia = copia + 1.0;           // 2. sumar
      delayMicroseconds(1);          //    aqui es donde el otro nucleo se cuela
      energiaTotal = copia;          // 3. escribir
      // ----------------------------------------------------
    }
  }

  Serial.printf("   Tarea %s termino (Nucleo %d)\n",
                (const char *)nombre, xPortGetCoreID());

  xSemaphoreGive(semFinalizado);     // avisa al coordinador
  vTaskDelete(NULL);                 // la tarea se destruye a si misma
}

// ---------------------------------------------------------------------------
// Lanza las dos tareas, espera a que ambas terminen (barrera de sincronizacion
// construida con un semaforo contador) y reporta el resultado.
// ---------------------------------------------------------------------------
void ejecutarPrueba(const char *titulo, bool conMutex) {
  Serial.printf("\n=== %s ===\n", titulo);

  energiaTotal = 0.0;
  usarMutex    = conMutex;

  static const char *nA = "SensorA";
  static const char *nB = "SensorB";
  xTaskCreatePinnedToCore(tareaSensor, "SensorA", 3072, (void *)nA, 2, NULL, 0);
  xTaskCreatePinnedToCore(tareaSensor, "SensorB", 3072, (void *)nB, 2, NULL, 1);

  // BARRERA: no se sigue hasta recoger las dos senales de "ya termine".
  xSemaphoreTake(semFinalizado, portMAX_DELAY);
  xSemaphoreTake(semFinalizado, portMAX_DELAY);

  double esperado = 2.0 * SUMAS_POR_TAREA;
  Serial.printf("   Esperado : %.2f Wh\n", esperado);
  Serial.printf("   Obtenido : %.2f Wh\n", (double)energiaTotal);
  Serial.printf("   PERDIDAS : %.2f Wh  (%.1f %% de las sumas se evaporaron)\n",
                esperado - energiaTotal,
                100.0 * (esperado - energiaTotal) / esperado);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println(F("\nEJEMPLO 4 - Condicion de carrera y mutex"));

  mutexEnergia  = xSemaphoreCreateMutex();
  semFinalizado = xSemaphoreCreateCounting(2, 0);

  ejecutarPrueba("FASE 1: SIN mutex  (el error silencioso)", false);
  delay(1000);
  ejecutarPrueba("FASE 2: CON mutex  (la correccion)",       true);

  Serial.println(F("\nConclusion: el mismo codigo, con y sin candado, da"));
  Serial.println(F("resultados distintos. El bug no esta en la formula:"));
  Serial.println(F("esta en el ACCESO SIMULTANEO al dato compartido."));
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(5000));
}
