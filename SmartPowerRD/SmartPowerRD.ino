/*
  ============================================================================
  SmartPower RD - Monitor de energia domestica con transferencia automatica
                  a inversor y notificacion remota
  ============================================================================
  Asignatura : Inteligencia Artificial e Internet de las Cosas (2026-C-2)
  Profesor   : Luis Bessewell Feliz
  Estudiante : Cristian Carrera - Matricula 2024-1932
  Institucion: Instituto Tecnologico de Las Americas (ITLA)
  Placa      : ESP32 DevKit v1 / DevKit-C v4 (Arduino core 3.x)
  Version    : 2.0 - incorpora la retroalimentacion del facilitador

  ----------------------------------------------------------------------------
  QUE CAMBIO EN LA VERSION 2.0 Y POR QUE
  ----------------------------------------------------------------------------
  La primera version media UNA sola fase, no vigilaba el estado del banco de
  baterias, y -el defecto mas grave- el propio monitor se alimentaba de la red
  electrica. Es decir: en un apagon el equipo se apagaba justo en el instante
  para el que fue construido. Las tres correcciones:

  1. FASE PARTIDA (120/240 V). Se miden las DOS lineas por separado.
  2. RESPALDO PROPIO. El equipo se alimenta del bus de 12 V del inversor y
     vigila el voltaje de la bateria.
  3. QUE SE PROTEGE. Se separa proteger los electrodomesticos de proteger el
     inversor, con un rele de deslastre para las cargas no esenciales.
  4. REGISTRO LOCAL. Una microSD guarda cada evento ANTES de intentar
     enviarlo, y anota aparte si el envio se consiguio. Sin esto, un apagon
     que tumba el router se pierde sin dejar rastro; y son justamente los
     apagones mas severos los que tumban el router.

  ============================================================================
  1. ¿UNA FASE O DOS FASES?
  ============================================================================
  En Republica Dominicana la acometida residencial tiene dos formas:

    MONOFASICA A DOS HILOS (120 V)
      Una linea viva y un neutro. Apartamentos y viviendas pequeñas, sin
      equipos de 240 V.

    MONOFASICA TRIFILAR o "FASE PARTIDA" (120/240 V)   <- el caso general
      DOS lineas vivas (L1 y L2) desfasadas 180 grados, mas un neutro.
      De cada linea al neutro hay 120 V; entre las dos lineas, 240 V.
      Es lo habitual en viviendas con aire acondicionado, calentador
      electrico, estufa o bomba de agua.

  POR QUE NO BASTA CON MEDIR UNA SOLA LINEA:

    a) FASE PERDIDA. Un fusible del transformador de la distribuidora puede
       abrirse y dejar sin servicio UNA sola linea. Si el monitor solo vigila
       L1 y la que muere es L2, el sistema no se entera: la mitad de la casa
       queda sin luz y los equipos de 240 V arrancan con medio voltaje, que
       es la forma mas rapida de quemar el compresor de un aire.

    b) NEUTRO PERDIDO. Es la falla mas destructiva de una instalacion
       residencial. Si el neutro se suelta, las dos lineas quedan en serie a
       traves de las cargas: la linea con menos carga SUBE hacia 200 V o mas
       y la otra COLAPSA. Con un solo sensor esto es invisible; con dos se
       detecta al instante, porque la suma de ambas se mantiene cerca de 240
       mientras el desbalance entre ellas se dispara.

  DECISION DE DISEÑO: se implementa el caso general de fase partida. Para una
  vivienda monofasica simple basta con poner MONOFASICO en true; el mismo
  codigo ignora entonces la segunda linea.

  ============================================================================
  2. ¿QUE SE PROTEGE: EL INVERSOR O LOS ELECTRODOMESTICOS?
  ============================================================================
  Son DOS objetivos distintos y a veces opuestos. Conviene decidirlo, no
  dejarlo implicito:

  ESCENARIO A - Proteger los ELECTRODOMESTICOS (con inversor)
      Ante voltaje peligroso se transfiere la carga al inversor. El equipo
      queda alimentado con onda limpia. Es el objetivo principal.

  ESCENARIO B - Proteger el INVERSOR (y el banco de baterias)
      Aqui aparece el conflicto: el inversor aguanta 2400 W y la vivienda
      puede pedir 5000 W. Transferirlo todo lo sobrecarga y lo apaga por
      proteccion, con lo que se pierde hasta el refrigerador.
      La solucion es el DESLASTRE DE CARGA: un segundo rele corta los
      circuitos NO esenciales (aire acondicionado, calentador, secadora) y
      deja vivos los esenciales (nevera, luces, comunicaciones).
      Ademas se vigila la bateria: por debajo de 11.8 V se corta todo lo no
      esencial, porque descargar plomo-acido por debajo del 50 % le acorta
      la vida a la mitad.

  ESCENARIO C - SIN inversor
      Muchas viviendas no lo tienen. Entonces el sistema no puede dar
      continuidad, pero SI puede proteger: abre el contactor y desconecta la
      casa de la red cuando el voltaje es peligroso, avisa, y vuelve a
      conectar cuando se normaliza. Se activa poniendo HAY_INVERSOR en false.

  ============================================================================
  3. LA BATERIA DEL PROPIO MONITOR (el punto mas importante)
  ============================================================================
  El facilitador señalo un defecto real de la version 1: mientras el programa
  intenta reconectar, o cuando se va la luz, el equipo deja de mirar el
  voltaje. Son DOS problemas distintos con DOS soluciones distintas, y
  conviene no confundirlos:

    PROBLEMA DE SOFTWARE: una tarea lenta bloquea a la critica.
      -> Solucion: FreeRTOS. La tarea de red vive en el Nucleo 0 y puede
         bloquearse 8 segundos reconectando sin que el muestreo, que corre en
         el Nucleo 1 con prioridad mas alta, pierda un solo ciclo.

    PROBLEMA DE HARDWARE: en un apagon el equipo se queda sin corriente.
      -> Solucion: alimentacion de respaldo. Ninguna arquitectura de software
         salva a un microcontrolador apagado.

  COMO SE RESUELVE EL SEGUNDO:
  En lugar de alimentar el ESP32 desde los 120 V AC con una fuente HLK-PM01,
  se alimenta desde el BUS DE 12 V DEL BANCO DE BATERIAS del inversor, con un
  convertidor reductor (MP1584 o LM2596) a 5 V. Ventajas:

    - El monitor sigue vivo durante todo el apagon, que es justo cuando sus
      datos valen algo: puede cronometrar el corte, contarlo y avisar.
    - No hay que mantener una segunda bateria aparte.
    - De paso puede MEDIR el voltaje del banco, y asi estimar la autonomia
      restante y avisar antes de que el inversor se apague solo.

  Se añade un divisor de tension (100k / 33k) para leer los 12 V con el ADC
  de 3.3 V del ESP32. Para la vivienda SIN inversor, la alternativa es una
  celda 18650 con modulo TP4056 y elevador a 5 V.

  ============================================================================
  MONTAJE
  ============================================================================
     GPIO 34 (ADC) .. sensor de voltaje  LINEA 1  (ZMPT101B)
     GPIO 39 (ADC) .. sensor de voltaje  LINEA 2  (ZMPT101B)
     GPIO 35 (ADC) .. sensor de corriente LINEA 1 (SCT-013-030)
     GPIO 36 (ADC) .. sensor de corriente LINEA 2 (SCT-013-030)
     GPIO 33 (ADC) .. voltaje del banco de baterias (divisor 100k/33k)
     GPIO 26 ........ rele de TRANSFERENCIA (red <-> inversor)
     GPIO 25 ........ rele de DESLASTRE (corta cargas no esenciales)
     GPIO  4 ........ LED verde    - red electrica correcta
     GPIO  2 ........ LED amarillo - operando desde el inversor
     GPIO 13 ........ LED rojo     - alarma
     GPIO 27 ........ zumbador
     GPIO 14 ........ boton de silencio (INPUT_PULLUP, con interrupcion)
     GPIO 21/22 ..... LCD 16x2 I2C
     GPIO 5 ......... CS del modulo microSD (SPI: MOSI 23, MISO 19, SCK 18)

  En la simulacion los sensores se sustituyen por potenciometros, que
  permiten provocar cada falla a voluntad durante la demostracion.
  ============================================================================
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <SPI.h>
#include <SD.h>

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
// CONFIGURACION DE LA INSTALACION
// Estas dos constantes adaptan el mismo programa a las tres realidades
// descritas arriba, sin tocar una sola linea de logica.
// ---------------------------------------------------------------------------
const bool MONOFASICO   = false;  // true = 120 V a dos hilos (ignora L2)
const bool HAY_INVERSOR = true;   // false = solo desconecta y avisa

// ---------------------------------------------------------------------------
// Mapa de pines
// ---------------------------------------------------------------------------
const uint8_t PIN_V_L1     = 34;
const uint8_t PIN_V_L2     = 39;
const uint8_t PIN_I_L1     = 35;
const uint8_t PIN_I_L2     = 36;
const uint8_t PIN_V_BAT    = 33;

const uint8_t PIN_RELE_TRANSFER  = 26;
const uint8_t PIN_RELE_DESLASTRE = 25;
const uint8_t PIN_LED_RED        = 4;
const uint8_t PIN_LED_INVERSOR   = 2;
const uint8_t PIN_LED_ALARMA     = 13;
const uint8_t PIN_ZUMBADOR       = 27;
const uint8_t PIN_BOTON_SILENCIO = 14;
const uint8_t PIN_SD_CS          = 5;    // SPI: MOSI 23, MISO 19, SCK 18

const char *ARCHIVO_LOG = "/smartpower.csv";

// ---------------------------------------------------------------------------
// Calibracion
// ---------------------------------------------------------------------------
const float VOLTAJE_MAXIMO   = 260.0;   // fondo de escala del ZMPT101B
const float CORRIENTE_MAXIMA =  30.0;   // fondo de escala del SCT-013-030
const float BATERIA_MAXIMA   =  15.0;   // fondo de escala del divisor
const float ADC_MAXIMO       = 4095.0;  // ADC de 12 bits del ESP32

// Umbrales de la red (norma domestica dominicana: 120 V / 60 Hz)
const float V_MINIMO_SEGURO = 100.0;
const float V_MAXIMO_SEGURO = 135.0;
const float V_APAGON        =  40.0;

// Desbalance entre lineas que delata un NEUTRO PERDIDO.
// En condiciones normales L1 y L2 no difieren mas de unos 5 V.
const float DESBALANCE_CRITICO = 25.0;

// Banco de baterias de 12 V (plomo-acido)
const float BAT_LLENA   = 12.7;   // 100 % en reposo
const float BAT_BAJA    = 11.8;   // ~50 %: hay que deslastrar
const float BAT_CRITICA = 11.4;   // ~20 %: el inversor se apagara pronto

// Capacidad del inversor. Por encima de esto hay que deslastrar.
const float POTENCIA_MAXIMA_INVERSOR = 2400.0;   // watts

// Periodos de cada tarea
const TickType_t PERIODO_MUESTREO = pdMS_TO_TICKS(50);
const TickType_t PERIODO_PANTALLA = pdMS_TO_TICKS(500);
const TickType_t PERIODO_RED      = pdMS_TO_TICKS(5000);

// ---------------------------------------------------------------------------
// Estados del sistema. El orden va de menos a mas grave.
// ---------------------------------------------------------------------------
enum EstadoEnergia {
  RED_NORMAL,      // las dos lineas dentro de rango
  RED_ANORMAL,     // hay red, pero el voltaje es peligroso
  FASE_PERDIDA,    // una linea murio y la otra sigue viva
  NEUTRO_PERDIDO,  // desbalance grave: la falla mas destructiva
  APAGON,          // no hay red en ninguna linea
  MODO_INVERSOR    // la carga ya fue transferida
};

const char *NOMBRE_ESTADO[] = {
  "RED_NORMAL", "RED_ANORMAL", "FASE_PERDIDA",
  "NEUTRO_PERDIDO", "APAGON", "MODO_INVERSOR"
};

struct Muestra {
  float    voltajeL1;
  float    voltajeL2;
  float    corrienteL1;
  float    corrienteL2;
  float    voltajeBateria;
  float    potencia;
  uint32_t marcaTiempo;
};

struct EstadoSistema {
  float         voltajeL1;
  float         voltajeL2;
  float         corrienteL1;
  float         corrienteL2;
  float         potencia;
  float         energiaWh;
  float         voltajeBateria;
  uint8_t       porcentajeBateria;
  EstadoEnergia estado;
  uint32_t      cortesDetectados;
  uint32_t      msEnInversor;
  bool          alarmaSilenciada;
  bool          deslastreActivo;
};

// ---------------------------------------------------------------------------
// REGISTRO LOCAL EN MICROSD
// ---------------------------------------------------------------------------
// Entre DETECTAR un apagon y CONSEGUIR ENVIARLO pueden pasar muchas cosas: que
// el WiFi este caido, que el router se haya apagado con la luz, o que el propio
// equipo se reinicie. Si el unico registro vive en la nube, ese evento se
// pierde y nadie se entera de que se perdio.
//
// La solucion es un registro local de solo-añadir (append-only) con
// RECONCILIACION en dos pasos:
//
//    1. Al detectar el evento se escribe una linea con estado PENDIENTE.
//    2. Cuando MQTT confirma la publicacion se añade otra linea CONFIRMADO
//       con el mismo numero de secuencia.
//
// Asi, al revisar el archivo, cualquier secuencia que tenga PENDIENTE y no
// tenga su CONFIRMADO es un evento que ocurrio de verdad pero que nunca llego
// a la nube. Ese dato es justamente el que necesita el modelo de aprendizaje
// automatico, porque los apagones que tumban la comunicacion son los mas
// severos y son precisamente los que se perderian.
//
// NOTA DE CONCURRENCIA: la libreria SD NO es segura entre hilos. Por eso
// UNA SOLA tarea toca la tarjeta (tareaRegistro) y las demas le hablan por
// colas. Es la misma regla del mutex, aplicada a un periferico.
// ---------------------------------------------------------------------------
struct Evento {
  uint32_t      secuencia;
  uint32_t      marcaTiempo;
  EstadoEnergia estado;
  float         voltajeL1;
  float         voltajeL2;
  float         bateria;
  bool          confirmacion;   // false = evento nuevo, true = acuse de envio
};

QueueHandle_t colaEventos = NULL;   // control y red -> registro
volatile uint32_t secuenciaEventos = 0;
volatile bool     hayTarjeta = false;

// ---------------------------------------------------------------------------
// Objetos de concurrencia y hardware
// ---------------------------------------------------------------------------
QueueHandle_t     colaMuestras = NULL;
SemaphoreHandle_t mutexEstado  = NULL;
SemaphoreHandle_t semAlerta    = NULL;

EstadoSistema estado;

LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient   clienteWiFi;
PubSubClient mqtt(clienteWiFi);

volatile uint32_t pulsacionesBoton = 0;
portMUX_TYPE muxBoton = portMUX_INITIALIZER_UNLOCKED;

TaskHandle_t hMuestreo, hControl, hPantalla, hRed, hRegistro;

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

// ===========================================================================
// TAREA 1 - Muestreo            (Nucleo 1, prioridad 3)
// Unica tarea que toca el ADC. Ahora lee cinco canales en lugar de dos.
// ===========================================================================
void tareaMuestreo(void *p) {
  TickType_t ultimoDespertar = xTaskGetTickCount();
  Muestra m;

  for (;;) {
    m.voltajeL1   = (analogRead(PIN_V_L1)  / ADC_MAXIMO) * VOLTAJE_MAXIMO;
    m.corrienteL1 = (analogRead(PIN_I_L1)  / ADC_MAXIMO) * CORRIENTE_MAXIMA;

    if (MONOFASICO) {
      // En una acometida de dos hilos no existe L2. Se copia L1 para que la
      // logica de desbalance no dispare falsas alarmas.
      m.voltajeL2   = m.voltajeL1;
      m.corrienteL2 = 0;
    } else {
      m.voltajeL2   = (analogRead(PIN_V_L2) / ADC_MAXIMO) * VOLTAJE_MAXIMO;
      m.corrienteL2 = (analogRead(PIN_I_L2) / ADC_MAXIMO) * CORRIENTE_MAXIMA;
    }

    m.voltajeBateria = (analogRead(PIN_V_BAT) / ADC_MAXIMO) * BATERIA_MAXIMA;

    // Potencia total de la vivienda: la suma de las dos lineas.
    m.potencia = (m.voltajeL1 * m.corrienteL1 + m.voltajeL2 * m.corrienteL2) * 0.92;
    m.marcaTiempo = millis();

    if (xQueueSend(colaMuestras, &m, 0) != pdTRUE) {
      Muestra descartada;
      xQueueReceive(colaMuestras, &descartada, 0);
      xQueueSend(colaMuestras, &m, 0);
    }

    vTaskDelayUntil(&ultimoDespertar, PERIODO_MUESTREO);
  }
}

// ===========================================================================
// TAREA 2 - Control y proteccion   (Nucleo 1, prioridad 2)
// ===========================================================================
void tareaControl(void *p) {
  Muestra m;
  EstadoEnergia estadoAnterior = RED_NORMAL;
  uint32_t ultimoCalculo = millis();
  uint32_t ultimaPulsacionAtendida = 0;
  uint32_t inicioApagon = 0;

  for (;;) {
    if (xQueueReceive(colaMuestras, &m, portMAX_DELAY) != pdTRUE) continue;

    EstadoEnergia nuevoEstado = evaluarRed(m, estadoAnterior, inicioApagon);

    // --- Decidir el deslastre de carga -----------------------------------
    // Dos motivos independientes para cortar las cargas no esenciales:
    // que el consumo supere lo que aguanta el inversor, o que la bateria
    // este por debajo del 50 %.
    bool enBateria = (nuevoEstado == MODO_INVERSOR || nuevoEstado == APAGON);
    bool deslastrar = HAY_INVERSOR && enBateria &&
                      (m.potencia > POTENCIA_MAXIMA_INVERSOR ||
                       m.voltajeBateria < BAT_BAJA);

    // --- Actuadores -------------------------------------------------------
    if (HAY_INVERSOR) {
      digitalWrite(PIN_RELE_TRANSFER, nuevoEstado != RED_NORMAL);
    } else {
      // Escenario C: sin inversor el rele solo DESCONECTA la vivienda de una
      // red peligrosa. No hay adonde transferir, pero si de que proteger.
      digitalWrite(PIN_RELE_TRANSFER, nuevoEstado == RED_NORMAL);
    }
    digitalWrite(PIN_RELE_DESLASTRE, deslastrar);

    digitalWrite(PIN_LED_RED,      nuevoEstado == RED_NORMAL);
    digitalWrite(PIN_LED_INVERSOR, nuevoEstado == MODO_INVERSOR);
    digitalWrite(PIN_LED_ALARMA,   nuevoEstado == RED_ANORMAL ||
                                   nuevoEstado == FASE_PERDIDA ||
                                   nuevoEstado == NEUTRO_PERDIDO ||
                                   nuevoEstado == APAGON);

    // --- Boton de silencio ------------------------------------------------
    uint32_t pulsaciones;
    portENTER_CRITICAL(&muxBoton);
    pulsaciones = pulsacionesBoton;
    portEXIT_CRITICAL(&muxBoton);
    bool silenciar = (pulsaciones != ultimaPulsacionAtendida);
    if (silenciar) ultimaPulsacionAtendida = pulsaciones;

    // --- Zona critica: lo mas corta posible -------------------------------
    if (xSemaphoreTake(mutexEstado, pdMS_TO_TICKS(100)) == pdTRUE) {
      uint32_t ahora   = millis();
      uint32_t deltaMs = ahora - ultimoCalculo;
      ultimoCalculo = ahora;

      estado.voltajeL1         = m.voltajeL1;
      estado.voltajeL2         = m.voltajeL2;
      estado.corrienteL1       = m.corrienteL1;
      estado.corrienteL2       = m.corrienteL2;
      estado.potencia          = m.potencia;
      estado.energiaWh        += m.potencia * (deltaMs / 3600000.0);
      estado.voltajeBateria    = m.voltajeBateria;
      estado.porcentajeBateria = porcentajeBateria(m.voltajeBateria);
      estado.estado            = nuevoEstado;
      estado.deslastreActivo   = deslastrar;

      if (silenciar) estado.alarmaSilenciada = !estado.alarmaSilenciada;
      if (nuevoEstado == APAGON && estadoAnterior != APAGON) {
        estado.cortesDetectados++;
      }
      if (nuevoEstado == MODO_INVERSOR) estado.msEnInversor += deltaMs;

      bool sonar = (nuevoEstado != RED_NORMAL) && !estado.alarmaSilenciada;
      xSemaphoreGive(mutexEstado);

      // Sonido distinto segun la gravedad: el neutro perdido y la bateria
      // critica merecen un tono mas agudo e inconfundible.
      if (sonar) {
        bool urgente = (nuevoEstado == NEUTRO_PERDIDO) ||
                       (m.voltajeBateria < BAT_CRITICA && enBateria);
        tone(PIN_ZUMBADOR, urgente ? 3000 : 2000, 120);
      } else {
        noTone(PIN_ZUMBADOR);
      }
    }

    if (nuevoEstado != estadoAnterior) {
      Serial.printf("[CONTROL] %s  L1=%.1fV L2=%.1fV Bat=%.2fV\n",
                    NOMBRE_ESTADO[nuevoEstado], m.voltajeL1, m.voltajeL2,
                    m.voltajeBateria);

      // Se registra el evento ANTES de intentar enviarlo. Si el equipo muere
      // en el intento, el hecho ya quedo escrito en la tarjeta.
      Evento ev;
      ev.secuencia    = ++secuenciaEventos;
      ev.marcaTiempo  = millis();
      ev.estado       = nuevoEstado;
      ev.voltajeL1    = m.voltajeL1;
      ev.voltajeL2    = m.voltajeL2;
      ev.bateria      = m.voltajeBateria;
      ev.confirmacion = false;
      xQueueSend(colaEventos, &ev, 0);

      xSemaphoreGive(semAlerta);
      estadoAnterior = nuevoEstado;
    }
  }
}

// ---------------------------------------------------------------------------
// Maquina de estados de la acometida.
// El orden de las comprobaciones va de MAS GRAVE a menos grave, porque una
// falla grave puede parecerse a una leve si se mira solo una linea.
// ---------------------------------------------------------------------------
EstadoEnergia evaluarRed(const Muestra &m, EstadoEnergia anterior,
                         uint32_t &inicioApagon) {

  bool vivaL1 = m.voltajeL1 >= V_APAGON;
  bool vivaL2 = m.voltajeL2 >= V_APAGON;
  float desbalance = fabs(m.voltajeL1 - m.voltajeL2);

  // 1) APAGON TOTAL: ninguna linea tiene tension.
  if (!vivaL1 && !vivaL2) {
    if (anterior != APAGON && anterior != MODO_INVERSOR) {
      inicioApagon = millis();
      return APAGON;
    }
    // Se esperan 200 ms antes de confirmar, para no conmutar por un parpadeo.
    return (millis() - inicioApagon >= 200) ? MODO_INVERSOR : APAGON;
  }

  // 2) NEUTRO PERDIDO: las dos vivas pero muy desbalanceadas. Es lo mas
  //    destructivo y por eso se comprueba antes que nada.
  if (!MONOFASICO && vivaL1 && vivaL2 && desbalance > DESBALANCE_CRITICO) {
    return NEUTRO_PERDIDO;
  }

  // 3) FASE PERDIDA: una viva y la otra no. Invisible con un solo sensor.
  if (!MONOFASICO && (vivaL1 != vivaL2)) {
    return FASE_PERDIDA;
  }

  // 4) Voltaje fuera de rango en cualquiera de las dos lineas.
  if (m.voltajeL1 < V_MINIMO_SEGURO || m.voltajeL1 > V_MAXIMO_SEGURO) {
    return RED_ANORMAL;
  }
  if (!MONOFASICO &&
      (m.voltajeL2 < V_MINIMO_SEGURO || m.voltajeL2 > V_MAXIMO_SEGURO)) {
    return RED_ANORMAL;
  }

  return RED_NORMAL;
}

// ---------------------------------------------------------------------------
// Estimacion del estado de carga de un banco de plomo-acido de 12 V.
// Es una aproximacion lineal entre 11.4 V (20 %) y 12.7 V (100 %); sirve para
// avisar al usuario, no para un calculo de ingenieria.
// ---------------------------------------------------------------------------
uint8_t porcentajeBateria(float v) {
  if (v >= BAT_LLENA)   return 100;
  if (v <= BAT_CRITICA) return 20;
  return (uint8_t)(20 + (v - BAT_CRITICA) * 80.0 / (BAT_LLENA - BAT_CRITICA));
}

// ===========================================================================
// TAREA 3 - Pantalla local        (Nucleo 1, prioridad 1)
// ===========================================================================
void tareaPantalla(void *p) {
  TickType_t ultimoDespertar = xTaskGetTickCount();
  const char *corto[] = { "RED OK", "V.MALO", "F.PERD", "N.PERD",
                          "APAGON", "INVERS" };

  for (;;) {
    EstadoSistema c;
    if (xSemaphoreTake(mutexEstado, pdMS_TO_TICKS(200)) == pdTRUE) {
      c = estado;
      xSemaphoreGive(mutexEstado);

      // Fila 1: las dos lineas, que es lo que la version 1 no mostraba.
      lcd.setCursor(0, 0);
      if (MONOFASICO) {
        lcd.printf("%5.1fV %5.2fA  ", c.voltajeL1, c.corrienteL1);
      } else {
        lcd.printf("L1%5.1f L2%5.1f ", c.voltajeL1, c.voltajeL2);
      }

      // Fila 2: estado, bateria y aviso de deslastre.
      lcd.setCursor(0, 1);
      lcd.printf("%-6s B%3u%% %s", corto[c.estado], c.porcentajeBateria,
                 c.deslastreActivo ? "DL" : "  ");
    }
    vTaskDelayUntil(&ultimoDespertar, PERIODO_PANTALLA);
  }
}

// ===========================================================================
// TAREA 4 - Comunicaciones        (Nucleo 0, prioridad 1)
// Aqui vive el bloqueo peligroso. Aislada en el otro nucleo, una reconexion
// de 8 segundos no retrasa la deteccion de una falla.
// ===========================================================================
void tareaRed(void *p) {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD, WIFI_CANAL);
  mqtt.setServer(MQTT_SERVIDOR, MQTT_PUERTO);

  for (;;) {
    bool porEvento = (xSemaphoreTake(semAlerta, PERIODO_RED) == pdTRUE);

    // Se captura el numero de secuencia en el instante de despertar. Si el
    // control detectara otro evento mientras se publica, el acuse seguiria
    // apuntando al que desperto a esta tarea y no al mas reciente.
    uint32_t seqEvento = secuenciaEventos;

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

    EstadoSistema c;
    if (xSemaphoreTake(mutexEstado, pdMS_TO_TICKS(200)) != pdTRUE) continue;
    c = estado;
    xSemaphoreGive(mutexEstado);

    char carga[384];
    snprintf(carga, sizeof(carga),
             "{\"nodo\":\"%s\",\"v_l1\":%.1f,\"v_l2\":%.1f,"
             "\"i_l1\":%.2f,\"i_l2\":%.2f,\"w\":%.1f,\"wh\":%.2f,"
             "\"bat_v\":%.2f,\"bat_pct\":%u,\"estado\":\"%s\","
             "\"cortes\":%u,\"seg_inv\":%u,\"deslastre\":%s}",
             MQTT_CLIENTE_ID, c.voltajeL1, c.voltajeL2,
             c.corrienteL1, c.corrienteL2, c.potencia, c.energiaWh,
             c.voltajeBateria, c.porcentajeBateria, NOMBRE_ESTADO[c.estado],
             c.cortesDetectados, c.msEnInversor / 1000,
             c.deslastreActivo ? "true" : "false");

    mqtt.publish(TOPICO_TELEMETRIA, carga);

    if (porEvento) {
      bool enviado = mqtt.publish(TOPICO_ALERTAS, carga);

      // Solo si el broker acepto la publicacion se emite el acuse. Asi el
      // archivo distingue lo que ocurrio de lo que ademas llego a la nube.
      if (enviado) {
        Evento acuse;
        acuse.secuencia    = seqEvento;
        acuse.marcaTiempo  = millis();
        acuse.estado       = c.estado;
        acuse.voltajeL1    = c.voltajeL1;
        acuse.voltajeL2    = c.voltajeL2;
        acuse.bateria      = c.voltajeBateria;
        acuse.confirmacion = true;
        xQueueSend(colaEventos, &acuse, 0);
      }
    }

    Serial.printf("[RED] %s -> %s\n", porEvento ? "ALERTA" : "periodico", carga);
  }
}

// ===========================================================================
// TAREA 6 - Registro en microSD      (Nucleo 0, prioridad 1)
// Unica tarea que toca la tarjeta. Las demas le hablan por cola, porque la
// libreria SD no es segura entre hilos y escribir es lento (decenas de ms).
// ===========================================================================
void tareaRegistro(void *p) {
  Evento ev;

  for (;;) {
    if (xQueueReceive(colaEventos, &ev, portMAX_DELAY) != pdTRUE) continue;
    if (!hayTarjeta) continue;

    File f = SD.open(ARCHIVO_LOG, FILE_APPEND);
    if (!f) {
      Serial.println(F("[SD] No se pudo abrir el archivo de registro"));
      hayTarjeta = false;          // se deja de intentar hasta reiniciar
      continue;
    }

    // Formato: secuencia,ms,tipo,estado,L1,L2,bateria
    f.printf("%u,%u,%s,%s,%.1f,%.1f,%.2f\n",
             ev.secuencia, ev.marcaTiempo,
             ev.confirmacion ? "CONFIRMADO" : "PENDIENTE",
             NOMBRE_ESTADO[ev.estado],
             ev.voltajeL1, ev.voltajeL2, ev.bateria);
    f.close();

    Serial.printf("[SD] #%u %s %s\n", ev.secuencia,
                  ev.confirmacion ? "CONFIRMADO" : "PENDIENTE",
                  NOMBRE_ESTADO[ev.estado]);
  }
}

// ---------------------------------------------------------------------------
// Prepara la tarjeta y escribe la cabecera si el archivo es nuevo.
// ---------------------------------------------------------------------------
bool iniciarTarjeta() {
  if (!SD.begin(PIN_SD_CS)) {
    Serial.println(F("[SD] No se detecto tarjeta. El sistema sigue sin registro."));
    return false;
  }

  bool nuevo = !SD.exists(ARCHIVO_LOG);
  File f = SD.open(ARCHIVO_LOG, FILE_APPEND);
  if (!f) return false;

  if (nuevo) {
    f.println(F("secuencia,ms,tipo,estado,voltaje_l1,voltaje_l2,bateria"));
  }
  // Marca de arranque: permite detectar reinicios inesperados al analizar
  // el archivo, que es en si mismo un dato interesante.
  f.printf("0,%u,ARRANQUE,-,0.0,0.0,0.00\n", millis());
  f.close();

  Serial.print(F("[SD] Registro listo en "));
  Serial.println(ARCHIVO_LOG);
  return true;
}

// ===========================================================================
// TAREA 5 - Diagnostico           (Nucleo 0, prioridad 0)
// ===========================================================================
void tareaDiagnostico(void *p) {
  for (;;) {
    Serial.println(F("---- Pila libre por tarea (palabras) ----"));
    Serial.printf("  Muestreo : %u\n", uxTaskGetStackHighWaterMark(hMuestreo));
    Serial.printf("  Control  : %u\n", uxTaskGetStackHighWaterMark(hControl));
    Serial.printf("  Pantalla : %u\n", uxTaskGetStackHighWaterMark(hPantalla));
    Serial.printf("  Red      : %u\n", uxTaskGetStackHighWaterMark(hRed));
    Serial.printf("  Registro : %u\n", uxTaskGetStackHighWaterMark(hRegistro));
    Serial.printf("  Heap libre: %u bytes\n", ESP.getFreeHeap());
    Serial.printf("  Tarjeta SD: %s   eventos: %u\n",
                  hayTarjeta ? "presente" : "ausente", secuenciaEventos);
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println(F("\n== SmartPower RD v2.0 - ITLA 2026-C-2 =="));
  Serial.printf("Acometida: %s   Inversor: %s\n",
                MONOFASICO ? "monofasica 120 V" : "fase partida 120/240 V",
                HAY_INVERSOR ? "si" : "no");

  pinMode(PIN_RELE_TRANSFER,  OUTPUT);
  pinMode(PIN_RELE_DESLASTRE, OUTPUT);
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
  lcd.print("SmartPower RD 2.0");
  lcd.setCursor(0, 1);
  lcd.print("2024-1932");

  estado = {0, 0, 0, 0, 0, 0, 0, 0, RED_NORMAL, 0, 0, false, false};

  colaMuestras = xQueueCreate(10, sizeof(Muestra));
  colaEventos  = xQueueCreate(20, sizeof(Evento));
  mutexEstado  = xSemaphoreCreateMutex();
  semAlerta    = xSemaphoreCreateBinary();
  if (!colaMuestras || !colaEventos || !mutexEstado || !semAlerta) {
    Serial.println(F("ERROR: no se pudieron crear los objetos de FreeRTOS"));
    while (true) delay(1000);
  }

  // La tarjeta es opcional: si no esta, el sistema sigue protegiendo la casa
  // y solo pierde el historico. Una funcion de seguridad nunca debe depender
  // de un periferico de conveniencia.
  hayTarjeta = iniciarTarjeta();

  xTaskCreatePinnedToCore(tareaMuestreo,    "Muestreo",    2048, NULL, 3, &hMuestreo, 1);
  xTaskCreatePinnedToCore(tareaControl,     "Control",     4096, NULL, 2, &hControl,  1);
  xTaskCreatePinnedToCore(tareaPantalla,    "Pantalla",    4096, NULL, 1, &hPantalla, 1);
  xTaskCreatePinnedToCore(tareaRed,         "Red",         8192, NULL, 1, &hRed,      0);
  xTaskCreatePinnedToCore(tareaRegistro,    "Registro",    4096, NULL, 1, &hRegistro, 0);
  xTaskCreatePinnedToCore(tareaDiagnostico, "Diagnostico", 3072, NULL, 0, NULL,       0);

  delay(1500);
  lcd.clear();
}

// ---------------------------------------------------------------------------
// loop() vacio a proposito: en un diseno con FreeRTOS todo vive en tareas.
// ---------------------------------------------------------------------------
void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
