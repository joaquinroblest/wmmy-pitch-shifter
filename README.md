# WMMY Pitch Shifter

Plugin de cambio de tono para guitarra y bajo, en formato **Audio Unit** (Logic Pro) y **VST3**, para Mac (Apple Silicon e Intel).

## Controles

| Control | Qué hace |
|---|---|
| **Transpose** | Cuánto sube o baja el tono, de −24 a +24 semitonos (±2 octavas). |
| **Step** | Encendido: salta de semitono en semitono. Apagado: movimiento continuo. |
| **Modo** | **Rápido**: menos retraso, pensado para tocar. **Calidad**: más limpio, pensado para pistas grabadas. |
| **Mix** | Cuánto se oye la señal procesada (100 % = solo el tono cambiado). |
| **Salida** | Volumen final. |

## Retraso

El retraso depende del intervalo: es mayor al bajar el tono y menor al subirlo. El plugin se lo informa a Logic, que lo compensa automáticamente al reproducir pistas grabadas.

| Intervalo | Rápido | Calidad |
|---|---|---|
| −12 | ≈ 85 ms | ≈ 133 ms |
| 0 | ≈ 59 ms | ≈ 91 ms |
| +12 | ≈ 45 ms | ≈ 70 ms |

## Cómo se arma

GitHub Actions lo compila en cada cambio (ver `.github/workflows/build.yml`). El resultado queda como "artifact" descargable en la pestaña **Actions**.

## Créditos y licencia

- Motor de cambio de tono: [Rubber Band Library](https://breakfastquay.com/rubberband/) (GPL v2 o posterior).
- Estructura de plugin: [JUCE](https://juce.com) (AGPL v3).

Por eso este proyecto se distribuye bajo **AGPL v3**: el código es abierto.

`tools/` contiene programas de prueba fuera de línea usados para comparar motores.
