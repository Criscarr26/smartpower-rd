/*
  arduino_secrets.h — Credenciales del dispositivo en Arduino Cloud.

  Arduino Cloud genera estos tres valores cuando se asocia el dispositivo:

    SECRET_DEVICE_KEY     Se muestra UNA SOLA VEZ, al crear el dispositivo.
                          Anotarla en ese momento: despues ya no se puede
                          volver a consultar y habria que crear el dispositivo
                          de nuevo.
    SECRET_SSID           Nombre de la red WiFi de la vivienda (2.4 GHz; el
                          ESP32 no se conecta a redes de 5 GHz).
    SECRET_OPTIONAL_PASS  Clave de esa red.

  Este archivo NO debe subirse a un repositorio publico.
*/

#ifndef ARDUINO_SECRETS_H
#define ARDUINO_SECRETS_H

#define SECRET_DEVICE_KEY     "PEGAR-AQUI-LA-SECRET-KEY-DEL-DISPOSITIVO"
#define SECRET_SSID           "NOMBRE-DE-SU-RED-WIFI"
#define SECRET_OPTIONAL_PASS  "CLAVE-DE-SU-RED-WIFI"

#endif  // ARDUINO_SECRETS_H
