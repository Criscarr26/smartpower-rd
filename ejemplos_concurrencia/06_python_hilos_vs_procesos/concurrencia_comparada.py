"""
================================================================================
EJEMPLO 6 - CONCURRENCIA vs PARALELISMO, MEDIDO CON CRONOMETRO (Python)
================================================================================
Asignatura : Inteligencia Artificial e Internet de las Cosas (2026-C-2)
Profesor   : Luis Bessewell Feliz
Estudiante : Cristian Carrera - Matricula 2024-1932 - ITLA

Los ejemplos en Arduino demuestran los conceptos sobre un microcontrolador.
Este los demuestra "en sentido general a nivel de programacion", en una PC,
y sobre todo los MIDE: la conclusion no es una opinion, son segundos.

LA TESIS QUE SE PONE A PRUEBA
-----------------------------
No existe "la forma rapida" de programar concurrente. La herramienta correcta
depende de donde se pierde el tiempo:

  * Si la tarea espera (red, disco, un sensor) -> el problema es de ESPERA.
    Los HILOS (o asyncio) ganan, porque mientras uno espera los demas avanzan.
  * Si la tarea calcula -> el problema es de CPU.
    En CPython los hilos NO ayudan por culpa del GIL (Global Interpreter Lock),
    que permite que solo un hilo ejecute bytecode a la vez. Hay que usar
    PROCESOS, que tienen cada uno su propio interprete y su propio GIL.

Aplicado al proyecto SmartPower RD: leer diez sensores por la red es un
problema de ESPERA (hilos); reentrenar el modelo que predice el consumo es un
problema de CPU (procesos).

EJECUCION
---------
    python concurrencia_comparada.py

No requiere instalar nada: solo la biblioteca estandar.
================================================================================
"""

import asyncio
import math
import multiprocessing as mp
import os
import threading
import time
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor

TAREAS = 8


# =============================================================================
# PARTE 1 - Trabajo LIMITADO POR ESPERA (I/O bound)
# Simula consultar 8 sensores remotos; cada consulta tarda 0.5 s de red.
# =============================================================================
def leer_sensor_remoto(indice: int) -> float:
    time.sleep(0.5)                 # aqui el hilo LIBERA el GIL
    return 110.0 + indice


async def leer_sensor_async(indice: int) -> float:
    await asyncio.sleep(0.5)        # aqui la corrutina cede el control
    return 110.0 + indice


# =============================================================================
# PARTE 2 - Trabajo LIMITADO POR CPU (CPU bound)
# Simula el calculo del valor RMS sobre una ventana grande de muestras.
# =============================================================================
MUESTRAS_RMS = 4_000_000


def calcular_rms(semilla: int) -> float:
    total = 0.0
    for i in range(1, MUESTRAS_RMS):
        total += math.sin(i * 0.001 + semilla) ** 2
    return math.sqrt(total / MUESTRAS_RMS)


# =============================================================================
# Utilidades de medicion
# =============================================================================
def cronometrar(etiqueta: str, funcion) -> float:
    inicio = time.perf_counter()
    funcion()
    transcurrido = time.perf_counter() - inicio
    print(f"  {etiqueta:<42} {transcurrido:7.2f} s")
    return transcurrido


def demostrar_espera():
    print("\n" + "=" * 62)
    print("PARTE 1 - 8 lecturas de sensores remotos (0.5 s de espera c/u)")
    print("=" * 62)

    t_sec = cronometrar(
        "Secuencial (un sensor detras de otro)",
        lambda: [leer_sensor_remoto(i) for i in range(TAREAS)],
    )

    def con_hilos():
        with ThreadPoolExecutor(max_workers=TAREAS) as pool:
            list(pool.map(leer_sensor_remoto, range(TAREAS)))

    t_hil = cronometrar("Con 8 HILOS (concurrencia)", con_hilos)

    def con_asyncio():
        async def principal():
            await asyncio.gather(*(leer_sensor_async(i) for i in range(TAREAS)))
        asyncio.run(principal())

    t_asy = cronometrar("Con ASYNCIO (concurrencia, 1 solo hilo)", con_asyncio)

    print(f"\n  Aceleracion con hilos   : {t_sec / t_hil:5.2f} x")
    print(f"  Aceleracion con asyncio : {t_sec / t_asy:5.2f} x")
    print("  Lectura: cuando el cuello de botella es la ESPERA, ocho tareas")
    print("  caben en el tiempo de una sola. Y asyncio lo logra sin crear")
    print("  ni un solo hilo adicional: la concurrencia no necesita hilos.")


def demostrar_calculo():
    print("\n" + "=" * 62)
    print(f"PARTE 2 - 8 calculos de RMS (CPU pura) en {os.cpu_count()} nucleos")
    print("=" * 62)

    t_sec = cronometrar(
        "Secuencial",
        lambda: [calcular_rms(i) for i in range(TAREAS)],
    )

    def con_hilos():
        with ThreadPoolExecutor(max_workers=TAREAS) as pool:
            list(pool.map(calcular_rms, range(TAREAS)))

    t_hil = cronometrar("Con 8 HILOS (frenados por el GIL)", con_hilos)

    def con_procesos():
        with ProcessPoolExecutor(max_workers=os.cpu_count()) as pool:
            list(pool.map(calcular_rms, range(TAREAS)))

    t_pro = cronometrar("Con PROCESOS (paralelismo real)", con_procesos)

    print(f"\n  Aceleracion con hilos    : {t_sec / t_hil:5.2f} x")
    print(f"  Aceleracion con procesos : {t_sec / t_pro:5.2f} x")
    print(f"  Los procesos son {t_hil / t_pro:.1f} veces mas rapidos que los hilos.")
    print("  Lectura: los hilos no aceleran nada porque el GIL solo deja")
    print("  ejecutar bytecode a uno a la vez. Los procesos si, porque cada")
    print("  uno es un interprete independiente sobre un nucleo distinto.")
    print("  Ese es, exactamente, el precio de confundir CONCURRENCIA")
    print("  (organizar tareas) con PARALELISMO (ejecutarlas a la vez).")


# =============================================================================
# PARTE 3 - La condicion de carrera del Ejemplo 4, ahora en Python
# =============================================================================
def demostrar_carrera():
    print("\n" + "=" * 62)
    print("PARTE 3 - Condicion de carrera y Lock (el Ejemplo 4, en Python)")
    print("=" * 62)

    VUELTAS = 50_000
    HILOS = 4

    # El GIL de CPython cambia de hilo cada 5 ms. Con un bucle tan corto, la
    # carrera EXISTE pero casi nunca cae dentro de esa ventana y el resultado
    # sale correcto "de milagro". El time.sleep(0) fuerza la cesion del GIL
    # justo entre el LEER y el ESCRIBIR, que es exactamente lo que hace el
    # delayMicroseconds(1) del Ejemplo 4 en el ESP32.
    # Esto no inventa el error: solo lo saca a la luz de forma repetible.

    def sumar_sin_proteger(estado):
        for _ in range(VUELTAS):
            copia = estado["total"]      # 1. leer
            time.sleep(0)                #    <-- aqui entra otro hilo
            estado["total"] = copia + 1  # 2. escribir (con un valor ya rancio)

    def sumar_con_lock(estado, lock):
        for _ in range(VUELTAS):
            with lock:                   # nadie mas puede entrar hasta salir
                copia = estado["total"]
                time.sleep(0)
                estado["total"] = copia + 1

    esperado = HILOS * VUELTAS

    estado = {"total": 0}
    hilos = [threading.Thread(target=sumar_sin_proteger, args=(estado,))
             for _ in range(HILOS)]
    for h in hilos: h.start()
    for h in hilos: h.join()
    sin_lock = estado["total"]

    estado = {"total": 0}
    lock = threading.Lock()
    hilos = [threading.Thread(target=sumar_con_lock, args=(estado, lock))
             for _ in range(HILOS)]
    for h in hilos: h.start()
    for h in hilos: h.join()
    con_lock = estado["total"]

    print(f"  Esperado                : {esperado:,}")
    print(f"  Sin Lock ({HILOS} hilos)      : {sin_lock:,}"
          f"   -> perdidas: {esperado - sin_lock:,}"
          f"  ({100 * (esperado - sin_lock) / esperado:.1f} %)")
    print(f"  Con Lock ({HILOS} hilos)      : {con_lock:,}"
          f"   -> perdidas: {esperado - con_lock:,}"
          f"  ({100 * (esperado - con_lock) / esperado:.1f} %)")
    print("\n  El mismo algoritmo, los mismos datos, la misma maquina.")
    print("  Lo unico que cambia es el candado. Un bug de concurrencia que")
    print("  NO se manifiesta en una corrida sigue estando ahi: por eso no")
    print("  se corrigen 'probando', se corrigen protegiendo el dato.")


if __name__ == "__main__":
    mp.freeze_support()   # necesario en Windows para ProcessPoolExecutor
    print("=" * 62)
    print(" CONCURRENCIA, PARALELISMO Y DISTRIBUCION - EVIDENCIA MEDIDA")
    print(" Cristian Carrera - 2024-1932 - ITLA - IA e IoT 2026-C-2")
    print("=" * 62)
    demostrar_espera()
    demostrar_calculo()
    demostrar_carrera()
    print("\nFin de la demostracion.\n")
