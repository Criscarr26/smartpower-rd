/*
  ============================================================================
  EJEMPLO 5 de 5 - PROGRAMACION DISTRIBUIDA: varios nodos, una sola aplicacion
  ============================================================================
  Placa: ESP32 DevKit (simulable en Wokwi)
  Autor: Cristian Carrera 2024-1932 - ITLA - IA e IoT 2026-C-2

  Los ejemplos 2, 3 y 4 repartieron el trabajo entre TAREAS de un mismo chip.
  Aqui se da el salto: el trabajo se reparte entre MAQUINAS DISTINTAS, que no
  comparten memoria, no comparten reloj y se comunican unicamente por mensajes
  a traves de la red. Eso es programacion distribuida.

  LA TOPOLOGIA
  ------------
      [Nodo 1: tablero principal]  --\
      [Nodo 2: aire acondicionado] ----> (Broker MQTT) --> [Concentrador Python]
      [Nodo 3: bomba de agua]      --/                          |
                                                                v
                                                       Suma total del hogar
                                                       + deteccion de apagon

  Cada nodo ejecuta ESTE MISMO sketch; solo cambia la constante NODO_ID antes
  de compilar. Ninguno sabe cuantos hermanos tiene: publican en un topico y
  se desentienden. Esa independencia (bajo acoplamiento) es la razon de ser
  del patron publicador/suscriptor.

  LOS TRES PROBLEMAS CLASICOS DE LO DISTRIBUIDO, Y COMO SE ATACAN AQUI
  --------------------------------------------------------------------
  1. FALLO PARCIAL: un nodo puede morir sin que los demas se enteren.
     -> Solucion: "Last Will and Testament" (LWT). Al conectarse, el nodo le
        deja escrito al broker un mensaje de despedida. Si el nodo desaparece,
        el broker lo publica automaticamente. El concentrador se entera.
  2. NO HAY RELOJ COMPARTIDO: millis() de cada placa arranca cuando le da la
     gana.
     -> Solucion: cada mensaje lleva su propio numero de secuencia y su tiempo
        local; la ordenacion global la decide el concentrador, no los nodos.
  3. LA RED FALLA: los mensajes se pierden.
     -> Solucion: QoS y mensajes RETENIDOS, para que un suscriptor nuevo
        reciba de inmediato el ultimo estado conocido de cada nodo.

  Ademas se mantiene la concurrencia local: una tarea mide y otra publica, de
  modo que un corte de WiFi no detiene el muestreo.
  ============================================================================
*/

#include <WiFi.h>
#include <PubSubClient.h>

// ---------------------------------------------------------------------------
// >>> CAMBIE ESTAS DOS LINEAS PARA CADA NODO ANTES DE COMPILAR <<<
// ---------------------------------------------------------------------------
#define NODO_ID     "nodo1-tablero"
#define NODO_CARGA  "Tablero principal"

#define WIFI_SSID   "Wokwi-GUEST"
#define WIFI_PASS   ""
#define WIFI_CANAL  6

#define MQTT_HOST   "test.mosquitto.org"
#define MQTT_PORT   1883

#define TOPICO_DATOS   "itla/2024-1932/smartpower/nodos/" NODO_ID
#define TOPICO_ESTADO  "itla/2024-1932/smartpower/estado/" NODO_ID
#define TOPICO_ORDENES "itla/2024-1932/smartpower/ordenes"

const uint8_t PIN_SENSOR = 34;
const uint8_t PIN_LED    = 2;

WiFiClient   red;
PubSubClient mqtt(red);

QueueHandle_t colaEnvio = NULL;

struct Lectura {
  uint32_t secuencia;
  uint32_t msLocal;
  float    watts;
};

volatile bool cargaHabilitada = true;   // el concentrador puede apagar la carga

// ---------------------------------------------------------------------------
// Callback: ordenes que LLEGAN desde el concentrador. Un nodo distribuido no
// solo habla; tambien obedece. Aqui se implementa el deslastre de carga:
// si el inversor esta en bateria baja, el concentrador manda apagar las
// cargas no esenciales.
// ---------------------------------------------------------------------------
void alRecibirOrden(char *topico, byte *carga, unsigned int largo) {
  String mensaje;
  for (unsigned int i = 0; i < largo; i++) mensaje += (char)carga[i];

  Serial.printf("[ORDEN] %s -> %s\n", topico, mensaje.c_str());

  if (mensaje.indexOf("apagar") >= 0) cargaHabilitada = false;
  if (mensaje.indexOf("encender") >= 0) cargaHabilitada = true;
  digitalWrite(PIN_LED, cargaHabilitada);
}

// ---------------------------------------------------------------------------
// TAREA A - Medicion local. Nunca se detiene, aunque la red no exista.
// Es la garantia de que un problema de conectividad no se convierte en un
// problema de seguridad electrica.
// ---------------------------------------------------------------------------
void tareaMedicion(void *p) {
  TickType_t ultimo = xTaskGetTickCount();
  uint32_t secuencia = 0;

  for (;;) {
    Lectura l;
    l.secuencia = ++secuencia;
    l.msLocal   = millis();
    l.watts     = cargaHabilitada
                    ? (analogRead(PIN_SENSOR) / 4095.0) * 1800.0
                    : 0.0;

    // Si la cola esta llena (la red lleva rato caida) se descarta la muestra
    // mas vieja: en telemetria, el dato nuevo siempre vale mas que el viejo.
    if (xQueueSend(colaEnvio, &l, 0) != pdTRUE) {
      Lectura vieja;
      xQueueReceive(colaEnvio, &vieja, 0);
      xQueueSend(colaEnvio, &l, 0);
      Serial.println(F("[MEDICION] Cola llena: se descarto la muestra vieja"));
    }

    vTaskDelayUntil(&ultimo, pdMS_TO_TICKS(2000));
  }
}

// ---------------------------------------------------------------------------
// TAREA B - Transporte. Reconecta, publica y escucha ordenes.
// Vive en el Nucleo 0 junto al stack de WiFi.
// ---------------------------------------------------------------------------
void tareaTransporte(void *p) {
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(alRecibirOrden);
  WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CANAL);

  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println(F("[RED] Reconectando WiFi..."));
      WiFi.begin(WIFI_SSID, WIFI_PASS, WIFI_CANAL);
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (!mqtt.connected()) {
      Serial.println(F("[RED] Conectando al broker..."));
      // Testamento (LWT): si este nodo se cae, el broker publicara solo
      // "offline" en el topico de estado, con la bandera de retenido activa.
      bool ok = mqtt.connect(NODO_ID, NULL, NULL,
                             TOPICO_ESTADO, 1, true, "offline");
      if (ok) {
        mqtt.publish(TOPICO_ESTADO, "online", true);   // retenido
        mqtt.subscribe(TOPICO_ORDENES);
        Serial.println(F("[RED] Conectado. Suscrito a ordenes."));
      } else {
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }
    }

    mqtt.loop();   // atiende los mensajes entrantes

    Lectura l;
    if (xQueueReceive(colaEnvio, &l, pdMS_TO_TICKS(500)) == pdTRUE) {
      char json[192];
      snprintf(json, sizeof(json),
               "{\"nodo\":\"%s\",\"carga\":\"%s\",\"seq\":%u,"
               "\"ms\":%u,\"w\":%.1f,\"activa\":%s}",
               NODO_ID, NODO_CARGA, l.secuencia, l.msLocal, l.watts,
               cargaHabilitada ? "true" : "false");
      mqtt.publish(TOPICO_DATOS, json);
      Serial.printf("[ENVIO] %s\n", json);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);

  Serial.printf("\nEJEMPLO 5 - Nodo distribuido \"%s\" (%s)\n",
                NODO_ID, NODO_CARGA);

  colaEnvio = xQueueCreate(20, sizeof(Lectura));
  if (!colaEnvio) {
    Serial.println(F("ERROR: no se pudo crear la cola"));
    while (true) delay(1000);
  }

  xTaskCreatePinnedToCore(tareaMedicion,   "Medicion",   3072, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(tareaTransporte, "Transporte", 8192, NULL, 1, NULL, 0);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}
