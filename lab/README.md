# Laboratorio

Prototipo y regla de medición para el motor de baja latencia (no forman parte del plugin todavía).

- `pv.py`: prototipo en Python del vocoder de fase con desplazamiento rígido de regiones alrededor de picos (Laroche & Dolson, 1999) y ventana de análisis asimétrica.
- `regla.py`: mide un candidato con guitarra real: retraso, limpieza en notas sostenidas y definición de los ataques, contra una referencia de máxima calidad (`rb_ref.cpp`, Rubber Band fuera de línea).
- `ronda.py`: corre una configuración del prototipo en los cuatro intervalos de prueba.

Las grabaciones de prueba no se incluyen en el repositorio.
