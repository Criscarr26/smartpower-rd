"""
================================================================================
EJEMPLO 5 (parte 2) - EL CONCENTRADOR DEL SISTEMA DISTRIBUIDO
================================================================================
Asignatura : Inteligencia Artificial e Internet de las Cosas (2026-C-2)
Profesor   : Luis Bessewell Feliz
Estudiante : Cristian Carrera - Matricula 2024-1932 - ITLA

Este programa corre en la PC (o en un Raspberry Pi) y cierra el lazo del
sistema distribuido: se suscribe a TODOS los nodos SmartPower, suma el consumo
del hogar, detecta nodos caidos y, si el banco de baterias no da abasto, emite
la orden de deslastre de carga.

Es el contrapeso de la placa: aqui se ve que "distribuido" no significa
"varios Arduinos", sino varios procesos autonomos, en maquinas distintas,
coordinados solo por mensajes.

Se usa un HILO por responsabilidad:
  - El hilo de red (el que crea paho-mqtt) recibe los mensajes.
  - El hilo supervisor revisa cada 5 s quien dejo de reportar.
  - El hilo principal imprime el tablero.
El diccionario de estado lo tocan los tres, asi que se protege con un Lock:
es exactamente el mismo problema del Ejemplo 4, pero en Python.

INSTALACION
-----------
    pip install paho-mqtt

EJECUCION
---------
    python gateway_concentrador.py
================================================================================
"""

import json
import threading
import time
from collections import defaultdict

import paho.mqtt.client as mqtt

BROKER = "test.mosquitto.org"
PUERTO = 1883

BASE = "itla/2024-1932/smartpower"
TOPICO_DATOS = f"{BASE}/nodos/+"
TOPICO_ESTADO = f"{BASE}/estado/+"
TOPICO_ORDENES = f"{BASE}/ordenes"

# Si el consumo total supera esto, el inversor no aguanta y hay que deslastrar.
LIMITE_INVERSOR_W = 2400.0
SEGUNDOS_SIN_REPORTAR = 10.0

# --- Estado compartido entre los tres hilos ----------------------------------
nodos = defaultdict(lambda: {"w": 0.0, "visto": 0.0, "estado": "?", "seq": 0})
candado = threading.Lock()          # <-- el mutex de Python
deslastre_activo = False


def al_conectar(cliente, datos, banderas, rc, propiedades=None):
    print(f"[MQTT] Conectado al broker (codigo {rc})")
    cliente.subscribe(TOPICO_DATOS)
    cliente.subscribe(TOPICO_ESTADO)


def al_recibir(cliente, datos, mensaje):
    """Corre en el HILO DE RED que crea paho-mqtt."""
    topico = mensaje.topic
    texto = mensaje.payload.decode("utf-8", errors="replace")

    if "/estado/" in topico:
        nombre = topico.rsplit("/", 1)[-1]
        with candado:                              # seccion critica
            nodos[nombre]["estado"] = texto
            nodos[nombre]["visto"] = time.time()
            if texto == "offline":
                nodos[nombre]["w"] = 0.0
        print(f"[ESTADO] {nombre} -> {texto}")
        return

    try:
        d = json.loads(texto)
    except json.JSONDecodeError:
        print(f"[AVISO] Mensaje ilegible en {topico}: {texto!r}")
        return

    nombre = d.get("nodo", "desconocido")
    with candado:                                  # seccion critica
        # Los mensajes pueden llegar desordenados: se descarta lo viejo usando
        # el numero de secuencia que cada nodo lleva por su cuenta.
        if d.get("seq", 0) < nodos[nombre]["seq"]:
            print(f"[AVISO] Mensaje fuera de orden de {nombre}, descartado")
            return
        nodos[nombre]["seq"] = d.get("seq", 0)
        nodos[nombre]["w"] = float(d.get("w", 0.0))
        nodos[nombre]["visto"] = time.time()
        nodos[nombre]["estado"] = "online"


def supervisor(cliente, parar):
    """Hilo 2: detecta nodos mudos y decide el deslastre de carga."""
    global deslastre_activo

    while not parar.is_set():
        time.sleep(5)
        ahora = time.time()

        with candado:
            for nombre, d in nodos.items():
                if d["estado"] == "online" and ahora - d["visto"] > SEGUNDOS_SIN_REPORTAR:
                    d["estado"] = "SIN RESPUESTA"
                    d["w"] = 0.0
                    print(f"[ALERTA] {nombre} dejo de reportar")

            total = sum(d["w"] for d in nodos.values() if d["estado"] == "online")

        if total > LIMITE_INVERSOR_W and not deslastre_activo:
            deslastre_activo = True
            cliente.publish(TOPICO_ORDENES, "apagar")
            print(f"[ORDEN] {total:.0f} W > {LIMITE_INVERSOR_W:.0f} W -> DESLASTRE")
        elif total < LIMITE_INVERSOR_W * 0.8 and deslastre_activo:
            deslastre_activo = False
            cliente.publish(TOPICO_ORDENES, "encender")
            print(f"[ORDEN] {total:.0f} W -> se restituyen las cargas")


def tablero(parar):
    """Hilo principal: solo presenta la informacion."""
    while not parar.is_set():
        time.sleep(3)
        with candado:
            instantanea = {n: dict(d) for n, d in nodos.items()}

        if not instantanea:
            print("(esperando nodos...)")
            continue

        total = sum(d["w"] for d in instantanea.values() if d["estado"] == "online")
        print("\n" + "=" * 58)
        print(f"{'NODO':<22}{'ESTADO':<16}{'POTENCIA':>12}{'SEQ':>8}")
        print("-" * 58)
        for nombre, d in sorted(instantanea.items()):
            print(f"{nombre:<22}{d['estado']:<16}{d['w']:>10.1f} W{d['seq']:>8}")
        print("-" * 58)
        print(f"{'TOTAL DEL HOGAR':<38}{total:>10.1f} W")
        print(f"Capacidad del inversor: {LIMITE_INVERSOR_W:.0f} W   "
              f"Deslastre: {'ACTIVO' if deslastre_activo else 'no'}")
        print("=" * 58)


def main():
    parar = threading.Event()

    cliente = mqtt.Client(client_id="concentrador-2024-1932")
    cliente.on_connect = al_conectar
    cliente.on_message = al_recibir
    cliente.connect(BROKER, PUERTO, keepalive=60)

    cliente.loop_start()                       # hilo 1: red (lo crea paho)
    h = threading.Thread(target=supervisor,    # hilo 2: supervision
                         args=(cliente, parar), daemon=True)
    h.start()

    try:
        tablero(parar)                         # hilo 3: presentacion
    except KeyboardInterrupt:
        print("\nCerrando concentrador...")
    finally:
        parar.set()
        cliente.loop_stop()
        cliente.disconnect()


if __name__ == "__main__":
    main()
