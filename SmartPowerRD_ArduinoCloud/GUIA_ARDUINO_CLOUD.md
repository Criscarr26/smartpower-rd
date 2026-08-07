# Guía paso a paso — SmartPower RD en Arduino Cloud

El enunciado dice: *«sería inspirador realizar este Caso Práctico de Laboratorio programando la
solución utilizando el Arduino Cloud de forma virtual con su correspondiente Emulador, o su propio
Arduino y Arduino IDE de forma física»*. Esta carpeta cubre esa parte.

Todo se hace desde el navegador, en el plan **gratuito**, y sin comprar hardware.

---

## Qué vas a montar

```
   [ESP32]  --WiFi-->  [Arduino Cloud]  -->  [Tablero web + app del celular]
    5 tareas             10 variables           medidores, gráficas,
    FreeRTOS             de nube                interruptor de alarma
```

---

## Paso 1 — Crear la cuenta

1. Entra a <https://app.arduino.cc> y crea la cuenta (sirve la de Google).
2. El plan **Free** permite 1 Thing con hasta 5 variables y 1 día de histórico.
   Este proyecto declara 10 variables, así que tienes dos caminos:
   - **Opción A (gratis):** deja activas solo 5 variables. Las cinco que mejor
     demuestran el proyecto son `voltaje`, `corriente`, `estadoRed`,
     `cortesDetectados` y `silenciarAlarma`. Comenta las otras cinco líneas
     `ArduinoCloud.addProperty(...)` en `thingProperties.h`.
   - **Opción B:** el plan **Maker** (~US$6.99/mes) levanta el límite. Solo tiene
     sentido si vas a dejar el sistema instalado de verdad.

> Para la entrega de la asignatura, la Opción A es suficiente y no cuesta nada.

---

## Paso 2 — Registrar el ESP32 como dispositivo

El ESP32 no es una placa oficial de Arduino, así que entra como *dispositivo de terceros*:

1. En el menú lateral: **Devices** → **ADD** → **Third party device**.
2. Tipo: **ESP32** → modelo: **DOIT ESP32 DEVKIT V1** (o el que tengas).
3. Ponle nombre: `SmartPowerRD-2024-1932`.
4. Arduino Cloud te mostrará el **Device ID** y la **Secret Key**.

> ⚠️ **La Secret Key se muestra UNA SOLA VEZ.** Cópiala en ese momento. Si la
> pierdes hay que borrar el dispositivo y crearlo de nuevo. Descarga el PDF que
> te ofrece la plataforma.

5. Pega esos dos valores en los archivos de esta carpeta:
   - `Device ID` → en `thingProperties.h`, en `DEVICE_LOGIN_NAME`.
   - `Secret Key` → en `arduino_secrets.h`, en `SECRET_DEVICE_KEY`.
   - Y también tu red WiFi (**de 2.4 GHz**; el ESP32 no ve las de 5 GHz).

---

## Paso 3 — Crear el Thing y sus variables

1. **Things** → **CREATE THING** → nómbralo `SmartPowerRD`.
2. En **Associated Device**, selecciona el dispositivo del paso 2.
3. En **Network**, escribe el nombre y la clave de tu red WiFi.
4. **ADD VARIABLE** por cada una de estas, respetando nombre y tipo exactos:

| Variable | Tipo | Permiso | Actualización |
|---|---|---|---|
| `voltaje` | float | Read Only | On change (Δ 0.5) |
| `corriente` | float | Read Only | On change (Δ 0.1) |
| `potencia` | float | Read Only | On change (Δ 10) |
| `energiaWh` | float | Read Only | Periodic 30 s |
| `estadoRed` | String | Read Only | On change |
| `cortesDetectados` | int | Read Only | Periodic 30 s |
| `minutosEnInversor` | int | Read Only | Periodic 30 s |
| `silenciarAlarma` | bool | **Read & Write** | On change |
| `umbralMinimo` | float | **Read & Write** | On change |
| `umbralMaximo` | float | **Read & Write** | On change |

> Si estás en el plan gratuito, crea solo las cinco marcadas en el Paso 1.

---

## Paso 4 — Cargar el código

1. Pestaña **Sketch** del Thing. Arduino Cloud habrá generado su propio
   `thingProperties.h` a partir de las variables: **déjalo como está**, ya es
   equivalente al de esta carpeta.
2. Borra el contenido del `.ino` y pega `SmartPowerRD_ArduinoCloud.ino`.
3. Abre la pestaña `arduino_secrets.h` y rellena `SECRET_SSID` y
   `SECRET_OPTIONAL_PASS`.
4. En **Libraries** (icono de libro), añade **LiquidCrystal I2C**.
   `ArduinoIoTCloud` y `Arduino_ConnectionHandler` ya vienen incluidas.
5. Conecta el ESP32 por USB y pulsa **Upload**.
   - Si no tienes la placa física, sáltate esto: el código y el tablero se
     entregan igual, y la simulación funcional está en Wokwi.

---

## Paso 5 — Armar el tablero

**Dashboards** → **BUILD DASHBOARD** → añade estos widgets y enlaza cada uno a
su variable:

| Widget | Variable | Configuración |
|---|---|---|
| Gauge | `voltaje` | Min 0, Max 260, unidad `V` |
| Gauge | `corriente` | Min 0, Max 30, unidad `A` |
| Value | `potencia` | unidad `W` |
| Chart | `voltaje` | histórico, para ver los cortes en el tiempo |
| Status / Value | `estadoRed` | texto grande |
| Value | `cortesDetectados` | contador del día |
| Value | `minutosEnInversor` | tiempo en batería |
| **Switch** | `silenciarAlarma` | silencia el zumbador desde el celular |
| **Slider** | `umbralMinimo` | rango 80–120 V |
| **Slider** | `umbralMaximo` | rango 125–150 V |

Con la app **Arduino IoT Cloud Remote** (Android/iOS) ese mismo tablero se ve en
el teléfono, que es el objetivo del proyecto: enterarte del apagón estés donde estés.

---

## Paso 6 — Probar

1. Abre el **Monitor serie** del Thing. Deberías ver:
   ```
   == SmartPower RD - Arduino Cloud - ITLA 2026-C-2 ==
   ...
   Conectado a Arduino Cloud: si
   ```
2. Mueve el potenciómetro (o la entrada del sensor) hasta bajar el voltaje.
3. En el tablero: `estadoRed` pasa a `APAGON` y luego a `MODO INVERSOR`,
   y `cortesDetectados` sube en uno.
4. Pulsa el **Switch** de `silenciarAlarma` en el tablero: el zumbador del
   dispositivo se calla. Ese es el camino de vuelta, de la nube al hardware.

---

## Lo que hay que saber explicar en la defensa

Esta es la parte que conecta el laboratorio con la teoría del informe:

- **`ArduinoCloud.update()` es una llamada lenta.** Negocia TLS, reconecta WiFi
  y sincroniza variables; puede tardar segundos. Los ejemplos oficiales la ponen
  en `loop()`.
- **En este proyecto no puede ir en `loop()`.** Compartiría hilo con la rutina
  que decide si transferir la carga al inversor, y durante una reconexión el
  sistema dejaría de vigilar el voltaje justo cuando más falta hace.
- **Por eso vive en su propia tarea, anclada al Núcleo 0**, junto al stack de
  WiFi. El muestreo y el control siguen en el Núcleo 1, intactos.
- **La biblioteca no es segura entre hilos.** Por eso las variables de nube las
  escribe *una sola* tarea, a partir de una copia del estado tomada bajo mutex.
- **La prueba de que la arquitectura era correcta:** pasar de MQTT a Arduino
  Cloud cambió una sola tarea de las cinco. Nada más se tocó.

---

## Problemas frecuentes

| Síntoma | Causa habitual |
|---|---|
| No conecta al WiFi | La red es de 5 GHz. El ESP32 solo ve 2.4 GHz. |
| `Connection to Arduino IoT Cloud failed` | Device ID o Secret Key mal copiados. |
| El tablero no se actualiza | Superaste el límite de variables del plan Free. |
| Se reinicia solo cada pocos segundos | Pila insuficiente en la tarea de nube. Revisa el reporte de la tarea de diagnóstico y sube el valor de `12288`. |
| El LCD no muestra nada | La dirección I2C no es `0x27`; prueba `0x3F`. |
