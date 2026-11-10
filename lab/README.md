# Laboratorio

Prototipo y regla de medición del motor de baja latencia (modos En vivo del plugin).

- `pv.py`: prototipo en Python del motor: vocoder de fase con desplazamiento rígido de regiones alrededor de picos (Laroche y Dolson, 1999), ventana de análisis asimétrica, detector de ataques y remuestreo directo durante el ataque. La versión del plugin es `Source/PeakEngine.h`.
- `regla.py`: mide un candidato con guitarra real: retraso, limpieza en notas sostenidas y definición de los ataques, contra una referencia de máxima calidad (`rb_ref.cpp`, Rubber Band fuera de línea).
- `ronda.py`: corre una configuración del prototipo en los cuatro intervalos de prueba.
- `medir_cpp.py`: mide con la regla el motor en C++ (`tools/offline_peak.cpp`).
- `diag.py`, `color.py`, `hf.py`: diagnósticos puntuales.

Las rutas de los guiones apuntan al entorno donde se desarrolló. Las grabaciones de prueba no se incluyen en el repositorio.
