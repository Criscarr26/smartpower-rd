/*
  ============================================================================
  EJEMPLO 2 de 5 - MULTITAREA COOPERATIVA con millis() (sin sistema operativo)
  ============================================================================
  Placa: Arduino UNO (funciona igual en Wokwi)
  Autor: Cristian Carrera 2024-1932 - ITLA - IA e IoT 2026-C-2

  Mismas tres obligaciones del Ejemplo 1, pero ahora ninguna funcion bloquea.
  Cada "tarea" es una maquina de estados que se pregunta "ya me toca?" usando
  millis(), hace su trabajo en microsegundos y DEVUELVE el control.

  Esto es CONCURRENCIA REAL sobre un solo nucleo: las tres tareas progresan
  intercaladas. No es PARALELISMO, porque en cada instante fisico solo una
  instruccion se esta ejecutando; simplemente se turnan tan rapido que para
  el mundo exterior parecen simultaneas.

  Se llama COOPERATIVA porque nadie puede expulsar a una tarea: si una de
  ellas se porta mal y tarda 2 segundos, congela a todas las demas. La
  cooperacion depende de la disciplina del programador, no del hardware.
  Esa es su limitacion, y es exactamente lo que resuelve el Ejemplo 3.

  QUE OBSERVAR EN LA DEMOSTRACION
  -------------------------------
  El boton responde AL INSTANTE con un toque de 50 ms.
  El LED de vida mantiene su periodo exacto de 1000 ms aunque el zumbador
  este trabajando a otro ritmo distinto (300 ms).
  El monitor serie muestra cuantas veces por segundo se ejecuta el loop:
  decenas de miles. Ese numero es el "presupuesto de reaccion" del sistema.
  ============================================================================
*/

const int LED_VIDA   = 13;
const int ZUMBADOR   = 8;
const int BOTON      = 2;
const int LED_ALERTA = 12;

// --- Estado privado de cada tarea (equivale a las variables locales que en
//     un sistema con hilos viviria en la pila de cada hilo) -----------------
unsigned long tLedVida    = 0;   bool ledVidaEncendido = false;
unsigned long tZumbador   = 0;   bool zumbadorSonando  = false;
unsigned long tEstadistica = 0;
unsigned long vueltasLoop = 0;

const unsigned long PERIODO_LED      = 500;   // medio ciclo -> 1 Hz
const unsigned long PERIODO_ZUMBADOR = 150;   // medio ciclo -> 3.3 Hz
const unsigned long PERIODO_REPORTE  = 1000;

// ---------------------------------------------------------------------------
// Tarea A: LED de "sistema vivo". Nunca bloquea.
// ---------------------------------------------------------------------------
void tareaLedVida(unsigned long ahora) {
  if (ahora - tLedVida < PERIODO_LED) return;   // todavia no me toca
  tLedVida = ahora;
  ledVidaEncendido = !ledVidaEncendido;
  digitalWrite(LED_VIDA, ledVidaEncendido);
}

// ---------------------------------------------------------------------------
// Tarea B: pitido intermitente, con un ritmo INDEPENDIENTE del LED.
// ---------------------------------------------------------------------------
void tareaZumbador(unsigned long ahora) {
  if (ahora - tZumbador < PERIODO_ZUMBADOR) return;
  tZumbador = ahora;
  zumbadorSonando = !zumbadorSonando;
  if (zumbadorSonando) tone(ZUMBADOR, 1000);
  else                 noTone(ZUMBADOR);
}

// ---------------------------------------------------------------------------
// Tarea C: boton de emergencia. Al no haber bloqueos, se consulta miles de
// veces por segundo, asi que ningun toque se escapa.
// ---------------------------------------------------------------------------
void tareaBoton(unsigned long ahora) {
  static bool estadoAnterior = HIGH;
  static unsigned long tRebote = 0;

  bool lectura = digitalRead(BOTON);
  if (lectura == estadoAnterior) return;
  if (ahora - tRebote < 30) return;             // antirrebote no bloqueante
  tRebote = ahora;
  estadoAnterior = lectura;

  if (lectura == LOW) {
    digitalWrite(LED_ALERTA, !digitalRead(LED_ALERTA));
    Serial.print(F(">>> Boton detectado en t = "));
    Serial.print(ahora);
    Serial.println(F(" ms"));
  }
}

// ---------------------------------------------------------------------------
// Tarea D: telemetria. Mide cuantas vueltas de loop() caben en un segundo.
// ---------------------------------------------------------------------------
void tareaEstadistica(unsigned long ahora) {
  if (ahora - tEstadistica < PERIODO_REPORTE) return;
  tEstadistica = ahora;
  Serial.print(F("Vueltas de loop() en el ultimo segundo: "));
  Serial.println(vueltasLoop);
  vueltasLoop = 0;
}

void setup() {
  Serial.begin(9600);
  pinMode(LED_VIDA,   OUTPUT);
  pinMode(ZUMBADOR,   OUTPUT);
  pinMode(LED_ALERTA, OUTPUT);
  pinMode(BOTON,      INPUT_PULLUP);
  Serial.println(F("EJEMPLO 2 - Multitarea cooperativa con millis()"));
}

// ---------------------------------------------------------------------------
// El loop() se convierte en un PLANIFICADOR (scheduler) cooperativo: le
// ofrece la CPU a cada tarea, una detras de otra, miles de veces por segundo.
// ---------------------------------------------------------------------------
void loop() {
  unsigned long ahora = millis();
  vueltasLoop++;

  tareaLedVida(ahora);
  tareaZumbador(ahora);
  tareaBoton(ahora);
  tareaEstadistica(ahora);

  // Regla de oro de este modelo: aqui NO puede haber ni un solo delay().
}
