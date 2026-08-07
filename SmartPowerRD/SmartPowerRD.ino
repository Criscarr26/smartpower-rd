/*
  ============================================================================
  SmartPower RD - Monitor de energia domestica con transferencia automatica
                  a inversor y notificacion remota
  ============================================================================
  Asignatura : Inteligencia Artificial e Internet de las Cosas (2026-C-2)
  Profesor   : Luis Bessewell Feliz
  Estudiante : Cristian Carrera - Matricula 2024-1932
  Institucion: Instituto Tecnologico de Las Americas (ITLA)
  Placa      : ESP32 DevKit v1 (Arduino core 3.x) - simulable en Wokwi
  ----------------------------------------------------------------------------
  QUE HACE
  ----------------------------------------------------------------------------
  Mide de forma continua el voltaje y la corriente de la acometida electrica
  de la vivienda. Cuando detecta un apagon (o un voltaje fuera del rango
  seguro de 100 V - 135 V) conmuta la carga al inversor mediante un relevador
  de transferencia, avisa localmente con LED y zumbador, y publica el evento
  por MQTT para que llegue al telefono del usuario.

  ----------------------------------------------------------------------------
  POR QUE ESTE PROGRAMA ES MULTITAREA (y no puede no serlo)
  ----------------------------------------------------------------------------
  El sistema tiene cuatro obligaciones con plazos MUY distintos:

     Muestreo electrico ...... cada  50 ms  (no puede perder un solo ciclo)
     Logica de proteccion .... cada 100 ms  (debe reaccionar en < 500 ms)
     Refresco de pantalla .... cada 500 ms  (el I2C es lento: ~30 ms)
     Publicacion en la nube .. cada   5 s   (puede bloquearse varios segundos
                                             si el WiFi se cae)

  Con un unico loop() secuencial, un solo reintento de conexion WiFi (que
  puede tardar 8 segundos) dejaria al sistema CIEGO ante un apagon durante
  todo ese tiempo. La solucion es aislar cada obligacion en su propia tarea
  de FreeRTOS, con su propia prioridad, de modo que el planificador (scheduler)
  expulse a la tarea lenta y le devuelva la CPU a la tarea critica.

  Ademas se aprovechan los DOS nucleos del ESP32:
     Nucleo 1 -> tiempo real (muestreo, control, pantalla)
     Nucleo 0 -> comunicaciones (WiFi/MQTT), que es donde vive el stack de red

  ----------------------------------------------------------------------------
  MECANISMOS DE CONCURRENCIA UTILIZADOS (cada uno resuelve un problema real)
  ----------------------------------------------------------------------------
  1. QueueHandle_t  colaMuestras -> paso de mensajes productor/consumidor entre
     la tarea de muestreo y la de control. Evita variables globales compartidas.
  2. SemaphoreHandle_t mutexEstado -> exclusion mutua sobre la estructura de
     estado que leen la pantalla y la red mientras el control la escribe.
     Sin el, la pantalla podria mostrar el voltaje nuevo con el estado viejo.
  3. SemaphoreHandle_t semAlerta -> semaforo binario usado como SENAL: la tarea
     de red duerme sobre el y despierta al instante cuando ocurre un evento,
     en lugar de encuestar (polling) desperdiciando CPU.
  4. volatile + portMUX_TYPE -> seccion critica corta para el contador que
     modifica la interrupcion (ISR) del boton de silencio.

  ----------------------------------------------------------------------------
  MONTAJE (simulacion en Wokwi)
  ----------------------------------------------------------------------------
  En la vida real el voltaje se lee con un modulo ZMPT101B y la corriente con
  un sensor de efecto Hall no invasivo SCT-013-030. Ambos entregan una senal
  analogica, asi que en el simulador se sustituyen por dos potenciometros que
  permiten "provocar" el apagon a voluntad durante la demostracion.

     GPIO 34 (ADC) .. Potenciometro 1  -> simula el sensor de voltaje  (0-260 V)
     GPIO 35 (ADC) .. Potenciometro 2  -> simula el sensor de corriente (0-30 A)
     GPIO 26 ........ Rele de transferencia (bobina)
     GPIO 25 ........ LED verde    - energia de la red electrica presente
     GPIO 33 ........ LED amarillo - operando desde el inversor
     GPIO 32 ........ LED rojo     - alarma / bateria baja
     GPIO 27 ........ Zumbador piezoelectrico
     GPIO 14 ........ Boton de silencio de alarma (INPUT_PULLUP, con ISR)
     GPIO 21/22 ..... LCD 16x2 I2C (SDA/SCL), direccion 0x27
  ============================================================================
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>

// Si existe el archivo secrets.h se usan sus credenciales; si no (por ejemplo
// al pegar solo este sketch en Wokwi), se aplican las del simulador. Asi el
// proyecto arranca de una sola pieza sin dejar de separar las credenciales
// del codigo cuando se compila para hardware real.
#if __has_include("secrets.h")
  #include "secrets.h"
#else
  #define WIFI_SSID         "Wokwi-GUEST"
  #define WIFI_PASSWORD     ""
  #define WIFI_CANAL        6
  #define MQTT_SERVIDOR     "test.mosquitto.org"
  #define MQTT_PUERTO       1883
  #define MQTT_CLIENTE_ID   "smartpower-rd-2024-1932"
  #define TOPICO_TELEMETRIA "itla/2024-1932/smartpower/telemetria"
  #define TOPICO_ALERTAS    "itla/2024-1932/smartpower/alertas"
  #define TOPICO_COMANDOS   "itla/2024-1932/smartpower/comandos"
#endif

// ---------------------------------------------------------------------------
// 1. Mapa de pines y constantes de calibracion
// ---------------------------------------------------------------------------
const uint8_t PIN_SENSOR_VOLTAJE   = 34;
const uint8_t PIN_SENSOR_CORRIENTE = 35;
const uint8_t PIN_RELE_TRANSFER    = 26;
const uint8_t PIN_LED_RED          = 25;
const uint8_t PIN_LED_INVERSOR     = 33;
const uint8_t PIN_LED_ALARMA       = 32;
const uint8_t PIN_ZUMBADOR         = 27;
const uint8_t PIN_BOTON_SILENCIO   = 14;

const float VOLTAJE_MAXIMO   = 260.0;  // fondo de escala del sensor
const float CORRIENTE_MAXIMA =  30.0;  // fondo de escala del SCT-013-030
const float ADC_MAXIMO       = 4095.0; // ADC de 12 bits del ESP32

// Umbrales de proteccion (norma domestica dominicana: 120 V / 60 Hz)
const float V_MINIMO_SEGURO = 100.0;   // por debajo: bajo voltaje
const float V_MAXIMO_SEGURO = 135.0;   // por encima: sobrevoltaje
const float V_APAGON        =  40.0;   // por debajo: no hay red electrica

// Periodos de cada tarea, expresados en milisegundos
const TickType_t PERIODO_MUESTREO = pdMS_TO_TICKS(50);
const TickType_t PERIODO_PANTALLA = pdMS_TO_TICKS(500);
const TickType_t PERIODO_RED      = pdMS_TO_TICKS(5000);

// ---------------------------------------------------------------------------
// 2. Tipos de datos del dominio
// ---------------------------------------------------------------------------
enum EstadoEnergia {
  RED_NORMAL,      // la red electrica esta presente y dentro de rango
  RED_ANORMAL,     // hay red, pero el voltaje es peligroso para los equipos
  APAGON,          // no hay red electrica
  MODO_INVERSOR    // la carga ya fue transferida al inversor
};

// Mensaje que viaja por la cola: la tarea de muestreo lo produce, la de
// control lo consume. Se envia por VALOR (copia), por eso no hace falta
// proteger nada: la cola de FreeRTOS ya es segura entre tareas.
struct Muestra {
  float    voltaje;
  float    corriente;
  float    potencia;
  uint32_t marcaTiempo;
};

// Estado compartido: lo escribe la tarea de control y lo LEEN la pantalla y
// la red. Todo acceso debe hacerse tomando primero "mutexEstado".
struct EstadoSistema {
  float         voltaje;
  float         corriente;
  float         potencia;
  float         energiaWh;      // energia acumulada desde el arranque
  EstadoEnergia estado;
  uint32_t      cortesDetectados;
  uint32_t      msEnInversor;   // tiempo acumulado alimentando desde bateria
  bool          alarmaSilenciada;
};

// ---------------------------------------------------------------------------
// 3. Objetos globales de concurrencia y de hardware
// ---------------------------------------------------------------------------
QueueHandle_t     colaMuestras = NULL;  // productor/consumidor
SemaphoreHandle_t mutexEstado  = NULL;  // exclusion mutua
SemaphoreHandle_t semAlerta    = NULL;  // senalizacion de eventos

EstadoSistema estado;                   // recurso compartido protegido

LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient   clienteWiFi;
PubSubClient mqtt(clienteWiFi);

// Variables tocadas por la interrupcion del boton. "volatile" le prohibe al
// compilador cachearlas en un registro, y el portMUX garantiza que el acceso
// sea atomico frente a la ISR que corre en el otro nucleo.
volatile uint32_t pulsacionesBoton = 0;
portMUX_TYPE muxBoton = portMUX_INITIALIZER_UNLOCKED;

// ---------------------------------------------------------------------------
// 4. Rutina de interrupcion del boton de silencio
//    IRAM_ATTR obliga a guardar la funcion en RAM interna para que pueda
//    ejecutarse aunque la memoria flash este ocupada.
// ---------------------------------------------------------------------------
void IRAM_ATTR isrBotonSilencio() {
  static uint32_t ultimaPulsacion = 0;
  uint32_t ahora = millis();

  if (ahora - ultimaPulsacion < 250) return;   // antirrebote por software
  ultimaPulsacion = ahora;

  portENTER_CRITICAL_ISR(&muxBoton);
  pulsacionesBoton++;
  portEXIT_CRITICAL_ISR(&muxBoton);
}

// ---------------------------------------------------------------------------
// 5. TAREA 1 - Muestreo electrico          (Nucleo 1, prioridad 3, la mas alta)
//    Es la unica tarea que toca el ADC. Usa vTaskDelayUntil para lograr un
//    periodo ESTABLE de 50 ms: si el trabajo tardo 12 ms, duerme 38 ms.
// ---------------------------------------------------------------------------
void tareaMuestreo(void *parametros) {
  TickType_t ultimoDespertar = xTaskGetTickCount();
  Muestra m;

  for (;;) {
    int lecturaV = analogRead(PIN_SENSOR_VOLTAJE);
    int lecturaI = analogRead(PIN_SENSOR_CORRIENTE);

    m.voltaje     = (lecturaV / ADC_MAXIMO) * VOLTAJE_MAXIMO;
    m.corriente   = (lecturaI / ADC_MAXIMO) * CORRIENTE_MAXIMA;
    m.potencia    = m.voltaje * m.corriente * 0.92;  // factor de potencia tipico
    m.marcaTiempo = millis();

    // Envio no bloqueante: si la cola estuviera llena preferimos descartar la
    // muestra mas vieja antes que frenar el muestreo (los datos rancios no
    // sirven en un sistema de proteccion electrica).
    if (xQueueSend(colaMuestras, &m, 0) != pdTRUE) {
      Muestra descartada;
      xQueueReceive(colaMuestras, &descartada, 0);
      xQueueSend(colaMuestras, &m, 0);
    }

    vTaskDelayUntil(&ultimoDespertar, PERIODO_MUESTREO);
  }
}

// ---------------------------------------------------------------------------
// 6. TAREA 2 - Control y proteccion             (Nucleo 1, prioridad 2)
//    Consume la cola, ejecuta la maquina de estados y actua sobre el rele.
//    Es la unica tarea autorizada a ESCRIBIR en "estado".
// ---------------------------------------------------------------------------
void tareaControl(void *parametros) {
  Muestra m;
  EstadoEnergia estadoAnterior = RED_NORMAL;
  uint32_t ultimoCalculoEnergia = millis();
  uint32_t ultimaPulsacionAtendida = 0;
  uint32_t inicioApagon = 0;

  for (;;) {
    // Bloqueo hasta que llegue una muestra. Mientras espera, esta tarea no
    // consume absolutamente nada de CPU: el planificador se la da a otras.
    if (xQueueReceive(colaMuestras, &m, portMAX_DELAY) != pdTRUE) continue;

    // --- Maquina de estados de la acometida -------------------------------
    EstadoEnergia nuevoEstado;
    if (m.voltaje < V_APAGON) {
      // Primero se DECLARA el apagon; la transferencia al inversor se confirma
      // 200 ms despues, que es lo que tarda el rele en asentarse. Asi se evita
      // conmutar por un simple parpadeo de la red.
      if (estadoAnterior != APAGON && estadoAnterior != MODO_INVERSOR) {
        nuevoEstado  = APAGON;
        inicioApagon = millis();
      } else if (millis() - inicioApagon >= 200) {
        nuevoEstado = MODO_INVERSOR;
      } else {
        nuevoEstado = APAGON;
      }
    } else if (m.voltaje < V_MINIMO_SEGURO || m.voltaje > V_MAXIMO_SEGURO) {
      nuevoEstado = RED_ANORMAL;                   // hay red, pero peligrosa
    } else {
      nuevoEstado = RED_NORMAL;
    }

    // --- Actuadores --------------------------------------------------------
    bool transferirAInversor = (nuevoEstado != RED_NORMAL);
    digitalWrite(PIN_RELE_TRANSFER, transferirAInversor ? HIGH : LOW);
    digitalWrite(PIN_LED_RED,       nuevoEstado == RED_NORMAL);
    digitalWrite(PIN_LED_INVERSOR,  nuevoEstado == MODO_INVERSOR);
    digitalWrite(PIN_LED_ALARMA,    nuevoEstado == RED_ANORMAL ||
                                    nuevoEstado == APAGON);

    // --- Boton de silencio (leido desde la ISR de forma segura) ------------
    uint32_t pulsaciones;
    portENTER_CRITICAL(&muxBoton);
    pulsaciones = pulsacionesBoton;
    portEXIT_CRITICAL(&muxBoton);

    bool silenciar = (pulsaciones != ultimaPulsacionAtendida);
    if (silenciar) ultimaPulsacionAtendida = pulsaciones;

    // --- Zona critica: actualizacion del estado compartido -----------------
    // Se toma el mutex el MENOR tiempo posible. Todo el trabajo pesado
    // (calculos, digitalWrite, Serial) queda deliberadamente fuera.
    if (xSemaphoreTake(mutexEstado, pdMS_TO_TICKS(100)) == pdTRUE) {
      uint32_t ahora   = millis();
      uint32_t deltaMs = ahora - ultimoCalculoEnergia;
      ultimoCalculoEnergia = ahora;

      estado.voltaje    = m.voltaje;
      estado.corriente  = m.corriente;
      estado.potencia   = m.potencia;
      estado.energiaWh += m.potencia * (deltaMs / 3600000.0);
      estado.estado     = nuevoEstado;

      if (silenciar) estado.alarmaSilenciada = !estado.alarmaSilenciada;
      if (nuevoEstado == APAGON && estadoAnterior != APAGON) {
        estado.cortesDetectados++;   // se cuenta una sola vez por corte
      }
      if (nuevoEstado == MODO_INVERSOR) {
        estado.msEnInversor += deltaMs;
      }

      bool sonar = (nuevoEstado != RED_NORMAL) && !estado.alarmaSilenciada;
      xSemaphoreGive(mutexEstado);   // se libera ANTES de tocar el zumbador

      if (sonar) tone(PIN_ZUMBADOR, 2000, 120);
      else       noTone(PIN_ZUMBADOR);
    }

    // --- Senalizacion de evento a la tarea de red --------------------------
    // No se llama a MQTT desde aqui: publicar puede tardar segundos y esta
    // tarea es critica. Solo se levanta la bandera y se sigue trabajando.
    if (nuevoEstado != estadoAnterior) {
      Serial.printf("[CONTROL] Cambio de estado -> %d  (%.1f V)\n",
                    nuevoEstado, m.voltaje);
      xSemaphoreGive(semAlerta);
      estadoAnterior = nuevoEstado;
    }
  }
}

// ---------------------------------------------------------------------------
// 7. TAREA 3 - Interfaz local (LCD)             (Nucleo 1, prioridad 1)
//    Solo LEE el estado. El I2C es lento (~30 ms por refresco completo), por
//    eso vive en su propia tarea de baja prioridad: si el control necesita la
//    CPU, el planificador expulsa a esta sin ningun problema.
// ---------------------------------------------------------------------------
void tareaPantalla(void *parametros) {
  TickType_t ultimoDespertar = xTaskGetTickCount();
  const char *etiquetas[] = {"RED OK ", "V.MALO ", "APAGON ", "INVERSOR"};

  for (;;) {
    EstadoSistema copia;

    // Se copia el estado completo dentro del mutex y se suelta enseguida.
    // Pintar la pantalla con el mutex tomado bloquearia al control 30 ms.
    if (xSemaphoreTake(mutexEstado, pdMS_TO_TICKS(200)) == pdTRUE) {
      copia = estado;
      xSemaphoreGive(mutexEstado);

      lcd.setCursor(0, 0);
      lcd.printf("%6.1fV %5.2fA ", copia.voltaje, copia.corriente);
      lcd.setCursor(0, 1);
      lcd.printf("%-8s C:%-2u  ", etiquetas[copia.estado],
                 copia.cortesDetectados);
    }

    vTaskDelayUntil(&ultimoDespertar, PERIODO_PANTALLA);
  }
}

// ---------------------------------------------------------------------------
// 8. TAREA 4 - Comunicaciones          (Nucleo 0, prioridad 1)
//    Esta es la tarea "lenta y peligrosa": reconectar el WiFi puede tardar
//    varios segundos. Al vivir en otro nucleo y en otra tarea, ese bloqueo
//    JAMAS retrasa la deteccion del apagon.
//
//    Duerme sobre "semAlerta" con un tiempo maximo de espera: si ocurre un
//    evento publica de inmediato; si no ocurre nada, el timeout la despierta
//    a los 5 s para el reporte periodico. Esto es un patron clasico de
//    "espera con vencimiento" y sustituye al polling.
// ---------------------------------------------------------------------------
void tareaRed(void *parametros) {
  const char *etiquetas[] = {"RED_NORMAL", "RED_ANORMAL", "APAGON",
                             "MODO_INVERSOR"};

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CANAL);
  mqtt.setServer(MQTT_SERVIDOR, MQTT_PUERTO);

  for (;;) {
    bool porEvento = (xSemaphoreTake(semAlerta, PERIODO_RED) == pdTRUE);

    if (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CANAL);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    if (!mqtt.connected()) {
      mqtt.connect(MQTT_CLIENTE_ID);
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    mqtt.loop();

    EstadoSistema copia;
    if (xSemaphoreTake(mutexEstado, pdMS_TO_TICKS(200)) != pdTRUE) continue;
    copia = estado;
    xSemaphoreGive(mutexEstado);

    char carga[256];
    snprintf(carga, sizeof(carga),
             "{\"nodo\":\"%s\",\"v\":%.1f,\"i\":%.2f,\"w\":%.1f,"
             "\"wh\":%.2f,\"estado\":\"%s\",\"cortes\":%u,\"seg_inv\":%u}",
             MQTT_CLIENTE_ID, copia.voltaje, copia.corriente, copia.potencia,
             copia.energiaWh, etiquetas[copia.estado], copia.cortesDetectados,
             copia.msEnInversor / 1000);

    mqtt.publish(TOPICO_TELEMETRIA, carga);
    if (porEvento) mqtt.publish(TOPICO_ALERTAS, carga);

    Serial.printf("[RED] %s -> %s\n", porEvento ? "ALERTA" : "periodico", carga);
  }
}

// ---------------------------------------------------------------------------
// 9. TAREA 5 - Diagnostico del planificador     (Nucleo 0, prioridad 0)
//    Imprime cada 10 s cuanta memoria de pila le queda libre a cada tarea.
//    Es la evidencia de que las cinco tareas realmente coexisten, y sirve
//    para dimensionar las pilas sin adivinar.
// ---------------------------------------------------------------------------
TaskHandle_t hMuestreo, hControl, hPantalla, hRed;

void tareaDiagnostico(void *parametros) {
  for (;;) {
    Serial.println(F("---- Pila libre por tarea (palabras) ----"));
    Serial.printf("  Muestreo : %u\n", uxTaskGetStackHighWaterMark(hMuestreo));
    Serial.printf("  Control  : %u\n", uxTaskGetStackHighWaterMark(hControl));
    Serial.printf("  Pantalla : %u\n", uxTaskGetStackHighWaterMark(hPantalla));
    Serial.printf("  Red      : %u\n", uxTaskGetStackHighWaterMark(hRed));
    Serial.printf("  Heap libre: %u bytes\n", ESP.getFreeHeap());
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

// ---------------------------------------------------------------------------
// 10. setup() - se ejecuta una sola vez: crea los objetos de sincronizacion
//     y despues las tareas. A partir del ultimo xTaskCreatePinnedToCore, el
//     planificador toma el control del microcontrolador.
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n== SmartPower RD - ITLA 2026-C-2 =="));

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
  lcd.print("2024-1932");

  estado = {0, 0, 0, 0, RED_NORMAL, 0, 0, false};

  // Objetos de concurrencia. Si alguno falla no hay sistema: se detiene aqui.
  colaMuestras = xQueueCreate(10, sizeof(Muestra));
  mutexEstado  = xSemaphoreCreateMutex();
  semAlerta    = xSemaphoreCreateBinary();

  if (!colaMuestras || !mutexEstado || !semAlerta) {
    Serial.println(F("ERROR: no se pudieron crear los objetos de FreeRTOS"));
    while (true) delay(1000);
  }

  //                  funcion          nombre       pila   param prio  handle nucleo
  xTaskCreatePinnedToCore(tareaMuestreo,   "Muestreo",   2048, NULL, 3, &hMuestreo, 1);
  xTaskCreatePinnedToCore(tareaControl,    "Control",    4096, NULL, 2, &hControl,  1);
  xTaskCreatePinnedToCore(tareaPantalla,   "Pantalla",   4096, NULL, 1, &hPantalla, 1);
  xTaskCreatePinnedToCore(tareaRed,        "Red",        8192, NULL, 1, &hRed,      0);
  xTaskCreatePinnedToCore(tareaDiagnostico,"Diagnostico",3072, NULL, 0, NULL,       0);

  delay(1500);
  lcd.clear();
}

// ---------------------------------------------------------------------------
// 11. loop() queda VACIO a proposito.
//     En un diseno con FreeRTOS todo el trabajo vive en tareas. loop() es en
//     realidad la tarea "loopTask" de Arduino; dejarla vacia y cediendo el
//     tiempo evita que compita con las tareas del sistema.
// ---------------------------------------------------------------------------
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
