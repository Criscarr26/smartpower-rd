/*
  ============================================================================
  EJEMPLO 1 de 5 - EL PROBLEMA: programacion secuencial bloqueante
  ============================================================================
  Placa: Arduino UNO (funciona igual en Wokwi)
  Autor: Cristian Carrera 2024-1932 - ITLA - IA e IoT 2026-C-2

  Este sketch NO es la solucion: es la demostracion del problema que la
  concurrencia viene a resolver. Se pide que el programa haga tres cosas:

     A) Parpadear un LED de "sistema vivo" cada 1000 ms
     B) Emitir un pitido de alerta cada 300 ms
     C) Leer un boton de emergencia y responder al instante

  Al escribirlo con delay(), el microcontrolador queda DETENIDO dentro de cada
  delay(): no ejecuta nada mas, ni siquiera mirar el boton. El resultado es que
  el boton solo "funciona" si uno lo mantiene presionado mas de 1.3 segundos,
  y los tiempos de A y B se contaminan entre si.

  QUE OBSERVAR EN LA DEMOSTRACION
  -------------------------------
  Presione el boton rapidamente (un toque corto): el programa lo IGNORA.
  El monitor serie imprime el tiempo real entre ciclos y se vera que nunca
  es de 1000 ms, sino de ~1300 ms, porque los delay() se SUMAN.
  ============================================================================
*/

const int LED_VIDA   = 13;
const int ZUMBADOR   = 8;
const int BOTON      = 2;
const int LED_ALERTA = 12;

unsigned long cicloAnterior = 0;

void setup() {
  Serial.begin(9600);
  pinMode(LED_VIDA,   OUTPUT);
  pinMode(ZUMBADOR,   OUTPUT);
  pinMode(LED_ALERTA, OUTPUT);
  pinMode(BOTON,      INPUT_PULLUP);
  Serial.println(F("EJEMPLO 1 - Secuencial bloqueante (el problema)"));
}

void loop() {
  unsigned long ahora = millis();
  Serial.print(F("Ciclo. Tiempo real transcurrido: "));
  Serial.print(ahora - cicloAnterior);
  Serial.println(F(" ms  (deberia ser 1000)"));
  cicloAnterior = ahora;

  // --- Tarea A: parpadeo del LED de vida ------------------------------------
  digitalWrite(LED_VIDA, HIGH);
  delay(500);              // <-- LA CPU QUEDA CONGELADA AQUI 500 ms
  digitalWrite(LED_VIDA, LOW);
  delay(500);              // <-- Y OTROS 500 ms AQUI

  // --- Tarea B: pitido de alerta --------------------------------------------
  tone(ZUMBADOR, 1000);
  delay(150);              // <-- Y 150 ms MAS AQUI
  noTone(ZUMBADOR);
  delay(150);              // <-- Y 150 ms MAS AQUI

  // --- Tarea C: lectura del boton de emergencia -----------------------------
  // Este es el unico instante, en 1300 ms, en que el programa mira el boton.
  // Probabilidad de detectar un toque de 100 ms: practicamente cero.
  if (digitalRead(BOTON) == LOW) {
    digitalWrite(LED_ALERTA, HIGH);
    Serial.println(F(">>> Boton detectado (casi nunca ocurre)"));
  } else {
    digitalWrite(LED_ALERTA, LOW);
  }
}
