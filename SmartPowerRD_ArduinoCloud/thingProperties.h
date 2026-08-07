/*
  ============================================================================
  thingProperties.h — Declaracion de las variables de nube del "Thing"
  ============================================================================
  Proyecto   : SmartPower RD
  Estudiante : Cristian Carrera - 2024-1932 - ITLA
  Asignatura : IA e IoT 2026-C-2 - Prof. Luis Bessewell Feliz
  ----------------------------------------------------------------------------
  Arduino Cloud genera este archivo automaticamente a partir de las variables
  que se declaran en el panel web. Se incluye aqui escrito a mano por dos
  razones: para que el proyecto se pueda leer completo sin tener que entrar a
  la plataforma, y para documentar por que cada variable tiene la politica de
  actualizacion que tiene.

  ----------------------------------------------------------------------------
  POR QUE IMPORTA LA POLITICA DE ACTUALIZACION
  ----------------------------------------------------------------------------
  Cada variable puede publicarse ON_CHANGE (cuando cambia mas de un delta) o
  cada N segundos. Elegir mal esto es la forma mas facil de agotar la cuota
  del plan gratuito y de saturar la red:

    - Las magnitudes que varian todo el tiempo (voltaje, corriente) se envian
      ON_CHANGE con una banda muerta, para no publicar ruido de medicion.
    - El estado de la red se envia ON_CHANGE sin banda muerta: es el dato
      critico y cada transicion debe llegar.
    - Los acumulados (energia, cortes) se envian cada 30 s: no son urgentes.
    - Los umbrales y el silenciador son READWRITE: el usuario los cambia
      desde el tablero y el dispositivo obedece.

  ----------------------------------------------------------------------------
  COMO DECLARARLAS EN EL PANEL DE ARDUINO CLOUD
  ----------------------------------------------------------------------------
  En el Thing, boton "ADD VARIABLE", con estos tipos exactos:

    voltaje            float    Read Only    On change  (delta 0.5)
    corriente          float    Read Only    On change  (delta 0.1)
    potencia           float    Read Only    On change  (delta 10)
    energiaWh          float    Read Only    Periodic 30 s
    estadoRed          String   Read Only    On change
    cortesDetectados   int      Read Only    Periodic 30 s
    minutosEnInversor  int      Read Only    Periodic 30 s
    silenciarAlarma    bool     Read & Write On change
    umbralMinimo       float    Read & Write On change
    umbralMaximo       float    Read & Write On change
  ============================================================================
*/

#ifndef THING_PROPERTIES_H
#define THING_PROPERTIES_H

#include <ArduinoIoTCloud.h>
#include <Arduino_ConnectionHandler.h>
#include "arduino_secrets.h"

// ---------------------------------------------------------------------------
// Identidad del dispositivo. El DEVICE_LOGIN_NAME es el "Device ID" que
// aparece en la pestana Devices de Arduino Cloud (formato UUID).
// ---------------------------------------------------------------------------
const char DEVICE_LOGIN_NAME[] = "PEGAR-AQUI-EL-DEVICE-ID";
const char DEVICE_KEY[]        = SECRET_DEVICE_KEY;
const char SSID[]              = SECRET_SSID;
const char PASS[]              = SECRET_OPTIONAL_PASS;

// ---------------------------------------------------------------------------
// Variables de nube. Son variables de C++ normales: el sketch les asigna un
// valor y la biblioteca se encarga de sincronizarlas con el tablero web.
// ---------------------------------------------------------------------------
float  voltaje;
float  corriente;
float  potencia;
float  energiaWh;
String estadoRed;
int    cortesDetectados;
int    minutosEnInversor;

// Estas tres viajan en los dos sentidos: el tablero puede escribirlas.
bool   silenciarAlarma;
float  umbralMinimo;
float  umbralMaximo;

// Callbacks que la biblioteca invoca cuando el tablero cambia una variable.
// Se definen en el sketch principal.
void onSilenciarAlarmaChange();
void onUmbralMinimoChange();
void onUmbralMaximoChange();

// ---------------------------------------------------------------------------
void initProperties() {
  ArduinoCloud.setBoardId(DEVICE_LOGIN_NAME);
  ArduinoCloud.setSecretDeviceKey(DEVICE_KEY);

  // Solo lectura: el dispositivo informa, el tablero muestra.
  ArduinoCloud.addProperty(voltaje,           READ, ON_CHANGE, NULL, 0.5);
  ArduinoCloud.addProperty(corriente,         READ, ON_CHANGE, NULL, 0.1);
  ArduinoCloud.addProperty(potencia,          READ, ON_CHANGE, NULL, 10.0);
  ArduinoCloud.addProperty(energiaWh,         READ, 30 * SECONDS);
  ArduinoCloud.addProperty(estadoRed,         READ, ON_CHANGE);
  ArduinoCloud.addProperty(cortesDetectados,  READ, 30 * SECONDS);
  ArduinoCloud.addProperty(minutosEnInversor, READ, 30 * SECONDS);

  // Lectura y escritura: el tablero manda, el dispositivo obedece.
  ArduinoCloud.addProperty(silenciarAlarma, READWRITE, ON_CHANGE, onSilenciarAlarmaChange);
  ArduinoCloud.addProperty(umbralMinimo,    READWRITE, ON_CHANGE, onUmbralMinimoChange);
  ArduinoCloud.addProperty(umbralMaximo,    READWRITE, ON_CHANGE, onUmbralMaximoChange);
}

// Manejador de conexion WiFi. Para una placa con SIM o Ethernet se cambiaria
// por GSMConnectionHandler o EthernetConnectionHandler sin tocar nada mas.
WiFiConnectionHandler ArduinoIoTPreferredConnection(SSID, PASS);

#endif  // THING_PROPERTIES_H
