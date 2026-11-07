# WMMY Pitch Shifter

Plugin de cambio de tono para guitarra y bajo, en formato **Audio Unit** (Logic Pro) y **VST3**, para Mac (Apple Silicon e Intel).

## Controles

| Control | Qué hace |
|---|---|
| **Transpose** | Cuánto sube o baja el tono, de −24 a +24 semitonos (±2 octavas). |
| **Step** | Encendido: salta de semitono en semitono. Apagado: movimiento continuo. |
| **Modo** | **En vivo**: el menor retraso (~15 ms), pensado para tocar en tiempo real; acordes algo menos limpios. **Rápido**: muy buena calidad con retraso medio. **Calidad**: lo más limpio, pensado para pistas grabadas. |
| **Mix** | Cuánto se oye la señal procesada (100 % = solo el tono cambiado). |
| **Salida** | Volumen final. |

## Retraso

El retraso depende del intervalo: es mayor al bajar el tono y menor al subirlo. El plugin se lo informa a Logic, que lo compensa automáticamente al reproducir pistas grabadas.

| Intervalo | En vivo | Rápido | Calidad |
|---|---|---|---|
| −12 | ≈ 15 ms | ≈ 83 ms | ≈ 131 ms |
| 0 | ≈ 15 ms | ≈ 56 ms | ≈ 88 ms |
| +12 | ≈ 21 ms | ≈ 43 ms | ≈ 67 ms |

Al instalar una versión nueva, hay que **cerrar Logic por completo** (no solo el proyecto) para que cargue el plugin actualizado.

## Cómo se arma

GitHub Actions lo compila en cada cambio (ver `.github/workflows/build.yml`). El resultado queda como "artifact" descargable en la pestaña **Actions**.

## Créditos y licencia

- Motor de cambio de tono: [Rubber Band Library](https://breakfastquay.com/rubberband/) (GPL v2 o posterior).
- Motor del modo En vivo: [stftPitchShift](https://github.com/jurihock/stftPitchShift) de Jürgen Hock (MIT).
- Estructura de plugin: [JUCE](https://juce.com) (AGPL v3).

Por eso este proyecto se distribuye bajo **AGPL v3**: el código es abierto.

`tools/` contiene programas de prueba fuera de línea usados para comparar motores.
