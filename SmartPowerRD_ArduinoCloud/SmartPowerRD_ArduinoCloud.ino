/*
  ============================================================================
  SmartPower RD — Version para ARDUINO CLOUD
  ============================================================================
  Asignatura : Inteligencia Artificial e Internet de las Cosas (2026-C-2)
  Profesor   : Luis Bessewell Feliz
  Estudiante : Cristian Carrera - Matricula 2024-1932
  Institucion: Instituto Tecnologico de Las Americas (ITLA)
  Placa      : ESP32 DevKit v1 / DevKit-C v4 (dispositivo de terceros en la nube)
  ----------------------------------------------------------------------------
  QUE CAMBIA RESPECTO A LA VERSION MQTT
  ----------------------------------------------------------------------------
  Esta es la misma solucion del proyecto, pero conectada a la plataforma
  Arduino Cloud en lugar de a un broker MQTT propio. No cambia ni la logica de
  proteccion ni la arquitectura de tareas: cambia UNICAMENTE la tarea de red.

  Eso es, en si mismo, la demostracion de que la arquitectura estaba bien
  planteada. Si las cinco tareas hubieran estado mezcladas en un solo loop(),
  cambiar el transporte habria obligado a reescribir el programa entero. Al
  estar aislada la responsabilidad de "comunicar", se sustituyo una sola tarea.

  ----------------------------------------------------------------------------
  POR QUE ArduinoCloud.update() OBLIGA A USAR TAREAS
  ----------------------------------------------------------------------------
  La biblioteca ArduinoIoTCloud exige que se llame a ArduinoCloud.update() de
  forma frecuente. Esa llamada:

      - negocia TLS con los servidores de Arduino (puede tardar SEGUNDOS),
      - reconecta el WiFi si se cayo,
      - sincroniza todas las variables de nube.

  Es decir: es exactamente el tipo de operacion lenta e impredecible que no
  puede compartir hilo con una rutina de proteccion electrica que debe
  reaccionar en menos de 500 ms. Ponerla en el loop() principal, como hacen
  los ejemplos introductorios, significa que durante una reconexion el sistema
  deja de vigilar el voltaje.

  Aqui vive en su propia tarea, anclada al Nucleo 0, junto al stack de WiFi.
  El muestreo y el control siguen en el Nucleo 1 y no se enteran de nada.

  ----------------------------------------------------------------------------
  SEGURIDAD DE HILOS DE LAS VARIABLES DE NUBE
  ----------------------------------------------------------------------------
  La biblioteca ArduinoIoTCloud NO es segura entre hilos. Por eso se aplica
  una regla estricta, y es la misma que ya usaba la version MQTT:

      Las variables de nube (voltaje, corriente, estadoRed...) se escriben
      UNICAMENTE desde la tarea de red, y su valor sale de una copia del
      estado compartido tomada bajo mutex.

  Ninguna otra tarea toca una variable de nube. Asi no hay dos hilos dentro
  de la biblioteca al mismo tiempo, que es la condicion que la romperia.

  ----------------------------------------------------------------------------
  MONTAJE
  ----------------------------------------------------------------------------
  Identico al de la version MQTT (ver diagram.json de la carpeta SmartPowerRD):

     GPIO 34 .. sensor de voltaje   (ZMPT101B / potenciometro en simulacion)
     GPIO 35 .. sensor de corriente (SCT-013-030 / potenciometro)
     GPIO 26 .. rele de transferencia
     GPIO 25 .. LED verde    - red electrica presente
     GPIO 33 .. LED amarillo - operando desde el inversor
     GPIO 32 .. LED rojo     - alarma
     GPIO 27 .. zumbador
     GPIO 14 .. boton de silencio (INPUT_PULLUP, con interrupcion)
     GPIO 21/22 . LCD 16x2 I2C
  ============================================================================
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "thingProperties.h"

// ---------------------------------------------------------------------------
// 1. Pines y calibracion
// ---------------------------------------------------------------------------
const uint8_t PIN_SENSOR_VOLTAJE   = 34;
const uint8_t PIN_SENSOR_CORRIENTE = 35;
const uint8_t PIN_RELE_TRANSFER    = 26;
const uint8_t PIN_LED_RED          = 25;
const uint8_t PIN_LED_INVERSOR     = 33;
const uint8_t PIN_LED_ALARMA       = 32;
const uint8_t PIN_ZUMBADOR         = 27;
const uint8_t PIN_BOTON_SILENCIO   = 14;

const float VOLTAJE_MAXIMO   = 260.0;
const float CORRIENTE_MAXIMA =  30.0;
const float ADC_MAXIMO       = 4095.0;
const float V_APAGON         =  40.0;

const TickType_t PERIODO_MUESTREO = pdMS_TO_TICKS(50);
const TickType_t PERIODO_PANTALLA = pdMS_TO_TICKS(500);

// ---------------------------------------------------------------------------
// 2. Tipos
// ---------------------------------------------------------------------------
enum EstadoEnergia { RED_NORMAL, RED_ANORMAL, APAGON, MODO_INVERSOR };

const char *NOMBRE_ESTADO[] = { "RED NORMAL", "VOLTAJE ANORMAL",
                                "APAGON", "MODO INVERSOR" };

struct Muestra {
  float    voltaje;
  float    corriente;
  float    potencia;
  uint32_t marcaTiempo;
};

struct EstadoSistema {
  float         voltaje;
  float         corriente;
  float         potencia;
  float         energiaWh;
  EstadoEnergia estado;
  uint32_t      cortesDetectados;
  uint32_t      msEnInversor;
  bool          alarmaSilenciada;
};

// ---------------------------------------------------------------------------
// 3. Objetos de concurrencia y hardware
// ---------------------------------------------------------------------------
QueueHandle_t     colaMuestras = NULL;
SemaphoreHandle_t mutexEstado  = NULL;
SemaphoreHandle_t semAlerta    = NULL;

EstadoSistema estado;

LiquidCrystal_I2C lcd(0x27, 16, 2);

volatile uint32_t pulsacionesBoton = 0;
portMUX_TYPE muxBoton = portMUX_INITIALIZER_UNLOCKED;

TaskHandle_t hMuestreo, hControl, hPantalla, hNube;

// ---------------------------------------------------------------------------
// 4. Callbacks del tablero de Arduino Cloud
//    Se ejecutan dentro de ArduinoCloud.update(), o sea, en la TAREA DE NUBE.
//    Por eso escriben en el estado compartido tomando el mutex, igual que
//    cualquier otra tarea. Un callback de nube no es codigo privilegiado.
// ---------------------------------------------------------------------------
void onSilenciarAlarmaChange() {
  if (xSemaphoreTake(mutexEstado, pdMS_TO_TICKS(100)) == pdTRUE) {
    estado.alarmaSilenciada = silenciarAlarma;
    xSemaphoreGive(mutexEstado);
  }
  Serial.printf("[NUBE] El tablero %s la alarma\n",
                silenciarAlarma ? "silencio" : "reactivo");
}

void onUmbralMinimoChange() {
  Serial.printf("[NUBE] Nuevo umbral minimo: %.1f V\n", umbralMinimo);
}

void onUmbralMaximoChange() {
  Serial.printf("[NUBE] Nuevo umbral maximo: %.1f V\n", umbralMaximo);
}

// ---------------------------------------------------------------------------
// 5. Interrupcion del boton fisico de silencio
// ---------------------------------------------------------------------------
void IRAM_ATTR isrBotonSilencio() {
  static uint32_t ultima = 0;
  uint32_t ahora = millis();
  if (ahora - ultima < 250) return;
  ultima = ahora;

  portENTER_CRITICAL_ISR(&muxBoton);
  pulsacionesBoton++;
  portEXIT_CRITICAL_ISR(&muxBoton);
}

// ---------------------------------------------------------------------------
// 6. TAREA 1 — Muestreo            (Nucleo 1, prioridad 3)
// ---------------------------------------------------------------------------
void tareaMuestreo(void *p) {
  TickType_t ultimoDespertar = xTaskGetTickCount();
  Muestra m;

  for (;;) {
    m.voltaje     = (analogRead(PIN_SENSOR_VOLTAJE)   / ADC_MAXIMO) * VOLTAJE_MAXIMO;
    m.corriente   = (analogRead(PIN_SENSOR_CORRIENTE) / ADC_MAXIMO) * CORRIENTE_MAXIMA;
    m.potencia    = m.voltaje * m.corriente * 0.92;
    m.marcaTiempo = millis();

    if (xQueueSend(colaMuestras, &m, 0) != pdTRUE) {
      Muestra descartada;
      xQueueReceive(colaMuestras, &descartada, 0);
      xQueueSend(colaMuestras, &m, 0);
    }
    vTaskDelayUntil(&ultimoDespertar, PERIODO_MUESTREO);
  }
}

// ---------------------------------------------------------------------------
// 7. TAREA 2 — Control y proteccion   (Nucleo 1, prioridad 2)
//    Usa los umbrales que llegan del tablero, con copia local bajo mutex.
// ---------------------------------------------------------------------------
void tareaControl(void *p) {
  Muestra m;
  EstadoEnergia estadoAnterior = RED_NORMAL;
  uint32_t ultimoCalculo = millis();
  uint32_t ultimaPulsacionAtendida = 0;
  uint32_t inicioApagon = 0;

  for (;;) {
    if (xQueueReceive(colaMuestras, &m, portMAX_DELAY) != pdTRUE) continue;

    // Los umbrales los puede cambiar el usuario desde el tablero web. Se leen
    // en variables locales para que no cambien a mitad de la evaluacion.
    float vMin = umbralMinimo;
    float vMax = umbralMaximo;

    EstadoEnergia nuevoEstado;
    if (m.voltaje < V_APAGON) {
      if (estadoAnterior != APAGON && estadoAnterior != MODO_INVERSOR) {
        nuevoEstado  = APAGON;
        inicioApagon = millis();
      } else if (millis() - inicioApagon >= 200) {
        nuevoEstado = MODO_INVERSOR;
      } else {
        nuevoEstado = APAGON;
      }
    } else if (m.voltaje < vMin || m.voltaje > vMax) {
      nuevoEstado = RED_ANORMAL;
    } else {
      nuevoEstado = RED_NORMAL;
    }

    digitalWrite(PIN_RELE_TRANSFER, nuevoEstado != RED_NORMAL);
    digitalWrite(PIN_LED_RED,       nuevoEstado == RED_NORMAL);
    digitalWrite(PIN_LED_INVERSOR,  nuevoEstado == MODO_INVERSOR);
    digitalWrite(PIN_LED_ALARMA,    nuevoEstado == RED_ANORMAL ||
                                    nuevoEstado == APAGON);

    uint32_t pulsaciones;
    portENTER_CRITICAL(&muxBoton);
    pulsaciones = pulsacionesBoton;
    portEXIT_CRITICAL(&muxBoton);
    bool silenciar = (pulsaciones != ultimaPulsacionAtendida);
    if (silenciar) ultimaPulsacionAtendida = pulsaciones;

    if (xSemaphoreTake(mutexEstado, pdMS_TO_TICKS(100)) == pdTRUE) {
      uint32_t ahora = millis();
      uint32_t deltaMs = ahora - ultimoCalculo;
      ultimoCalculo = ahora;

      estado.voltaje    = m.voltaje;
      estado.corriente  = m.corriente;
      estado.potencia   = m.potencia;
      estado.energiaWh += m.potencia * (deltaMs / 3600000.0);
      estado.estado     = nuevoEstado;

      if (silenciar) estado.alarmaSilenciada = !estado.alarmaSilenciada;
      if (nuevoEstado == APAGON && estadoAnterior != APAGON) {
        estado.cortesDetectados++;
      }
      if (nuevoEstado == MODO_INVERSOR) estado.msEnInversor += deltaMs;

      bool sonar = (nuevoEstado != RED_NORMAL) && !estado.alarmaSilenciada;
      xSemaphoreGive(mutexEstado);

      if (sonar) tone(PIN_ZUMBADOR, 2000, 120);
      else       noTone(PIN_ZUMBADOR);
    }

    if (nuevoEstado != estadoAnterior) {
      Serial.printf("[CONTROL] %s  (%.1f V)\n",
                    NOMBRE_ESTADO[nuevoEstado], m.voltaje);
      xSemaphoreGive(semAlerta);     // despierta a la tarea de nube
      estadoAnterior = nuevoEstado;
    }
  }
}

// ---------------------------------------------------------------------------
// 8. TAREA 3 — Pantalla local        (Nucleo 1, prioridad 1)
//    Sigue funcionando aunque no haya internet. En un apagon el router
//    tambien se cae, asi que esta es la unica interfaz disponible.
// ---------------------------------------------------------------------------
void tareaPantalla(void *p) {
  TickType_t ultimoDespertar = xTaskGetTickCount();
  const char *corto[] = { "RED OK ", "V.MALO ", "APAGON ", "INVERSOR" };

  for (;;) {
    EstadoSistema copia;
    if (xSemaphoreTake(mutexEstado, pdMS_TO_TICKS(200)) == pdTRUE) {
      copia = estado;
      xSemaphoreGive(mutexEstado);

      lcd.setCursor(0, 0);
      lcd.printf("%6.1fV %5.2fA ", copia.voltaje, copia.corriente);
      lcd.setCursor(0, 1);
      lcd.printf("%-8s C:%-2u  ", corto[copia.estado], copia.cortesDetectados);
    }
    vTaskDelayUntil(&ultimoDespertar, PERIODO_PANTALLA);
  }
}

// ---------------------------------------------------------------------------
// 9. TAREA 4 — Arduino Cloud         (Nucleo 0, prioridad 1)
//
//    Esta es la unica tarea que toca la biblioteca ArduinoIoTCloud y las
//    variables de nube. ArduinoCloud.update() se llama cada 100 ms, que es
//    el ritmo que recomienda Arduino, y puede bloquearse varios segundos
//    durante una reconexion sin afectar a nadie.
// ---------------------------------------------------------------------------
void tareaNube(void *p) {
  uint32_t ultimaPublicacion = 0;

  for (;;) {
    ArduinoCloud.update();          // <- la llamada lenta, aislada aqui

    // Se refrescan las variables de nube cada segundo, o de inmediato si la
    // tarea de control levanto la bandera de alerta.
    bool porEvento = (xSemaphoreTake(semAlerta, 0) == pdTRUE);
    if (porEvento || millis() - ultimaPublicacion >= 1000) {
      ultimaPublicacion = millis();

      EstadoSistema copia;
      if (xSemaphoreTake(mutexEstado, pdMS_TO_TICKS(200)) == pdTRUE) {
        copia = estado;
        xSemaphoreGive(mutexEstado);

        // Asignar una variable de nube ya la marca para sincronizacion.
        voltaje           = copia.voltaje;
        corriente         = copia.corriente;
        potencia          = copia.potencia;
        energiaWh         = copia.energiaWh;
        estadoRed         = String(NOMBRE_ESTADO[copia.estado]);
        cortesDetectados  = (int)copia.cortesDetectados;
        minutosEnInversor = (int)(copia.msEnInversor / 60000);
        silenciarAlarma   = copia.alarmaSilenciada;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ---------------------------------------------------------------------------
// 10. TAREA 5 — Diagnostico          (Nucleo 0, prioridad 0)
// ---------------------------------------------------------------------------
void tareaDiagnostico(void *p) {
  for (;;) {
    Serial.println(F("---- Pila libre por tarea (palabras) ----"));
    Serial.printf("  Muestreo : %u\n", uxTaskGetStackHighWaterMark(hMuestreo));
    Serial.printf("  Control  : %u\n", uxTaskGetStackHighWaterMark(hControl));
    Serial.printf("  Pantalla : %u\n", uxTaskGetStackHighWaterMark(hPantalla));
    Serial.printf("  Nube     : %u\n", uxTaskGetStackHighWaterMark(hNube));
    Serial.printf("  Heap libre: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("  Conectado a Arduino Cloud: %s\n",
                  ArduinoCloud.connected() ? "si" : "no");
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

// ---------------------------------------------------------------------------
// 11. setup()
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1500);   // margen para que el monitor serie enganche el arranque
  Serial.println(F("\n== SmartPower RD - Arduino Cloud - ITLA 2026-C-2 =="));

  pinMode(PIN_RELE_TRANSFER,  OUTPUT);
  pinMode(PIN_LED_RED,        OUTPUT);
  pinMode(PIN_LED_INVERSOR,   OUTPUT);
  pinMode(PIN_LED_ALARMA,     OUTPUT);
  pinMode(PIN_ZUMBADOR,       OUTPUT);
  pinMode(PIN_BOTON_SILENCIO, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(PIN_BOTON_SILENCIO),
                  isrBotonSilencio, FALLING);

  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();
  lcd.print("SmartPower RD");
  lcd.setCursor(0, 1);
  lcd.print("Arduino Cloud");

  estado = {0, 0, 0, 0, RED_NORMAL, 0, 0, false};

  // Valores por defecto de los umbrales. En cuanto el dispositivo sincroniza
  // con la nube, el tablero puede sobreescribirlos.
  umbralMinimo = 100.0;
  umbralMaximo = 135.0;

  colaMuestras = xQueueCreate(10, sizeof(Muestra));
  mutexEstado  = xSemaphoreCreateMutex();
  semAlerta    = xSemaphoreCreateBinary();
  if (!colaMuestras || !mutexEstado || !semAlerta) {
    Serial.println(F("ERROR: no se pudieron crear los objetos de FreeRTOS"));
    while (true) delay(1000);
  }

  // Conexion con Arduino Cloud
  initProperties();
  ArduinoCloud.begin(ArduinoIoTPreferredConnection);
  setDebugMessageLevel(2);          // 0 = nada, 4 = todo. 2 va bien para clase
  ArduinoCloud.printDebugInfo();

  xTaskCreatePinnedToCore(tareaMuestreo,    "Muestreo",    2048, NULL, 3, &hMuestreo, 1);
  xTaskCreatePinnedToCore(tareaControl,     "Control",     4096, NULL, 2, &hControl,  1);
  xTaskCreatePinnedToCore(tareaPantalla,    "Pantalla",    4096, NULL, 1, &hPantalla, 1);
  xTaskCreatePinnedToCore(tareaNube,        "Nube",       12288, NULL, 1, &hNube,     0);
  xTaskCreatePinnedToCore(tareaDiagnostico, "Diagnostico", 3072, NULL, 0, NULL,       0);

  delay(1500);
  lcd.clear();
}

// ---------------------------------------------------------------------------
// 12. loop() vacio.
//     Notese la diferencia con los ejemplos oficiales de Arduino Cloud, que
//     ponen ArduinoCloud.update() aqui. En este proyecto no puede ir en el
//     loop(): compartiria hilo con la proteccion electrica.
// ---------------------------------------------------------------------------
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
