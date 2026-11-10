"""Prototipo (numpy) del motor de baja latencia, para iterar rapido.

Tecnica publica: vocoder de fase con desplazamiento rigido de regiones alrededor
de picos espectrales (Laroche & Dolson, "New phase-vocoder techniques for
pitch-shifting, harmonizing and other exotic effects", 1999), con ventana de
analisis asimetrica (larga hacia el pasado, corta hacia el futuro) para que el
retraso dependa solo de la ventana de sintesis corta.
"""
import numpy as np

SR = 48000


def windows(N, S):
    """Ventana de analisis asimetrica (N) y de sintesis (ultimas S muestras)."""
    rise = N - S // 2
    wa = np.empty(N)
    wa[:rise] = np.sin(0.5 * np.pi * (np.arange(rise) + 0.5) / rise) ** 2
    wa[rise:] = np.cos(0.5 * np.pi * (np.arange(S // 2) + 0.5) / (S // 2)) ** 2
    prod = np.sin(np.pi * (np.arange(S) + 0.5) / S) ** 2  # Hann de largo S
    ws = np.zeros(N)
    ws[N - S:] = prod / np.maximum(wa[N - S:], 1e-9)
    return wa, ws


def process(x, semitones, N=2048, S=384, R=64, peak_half=2, reset=True, reset_thresh=2.0,
            floor_db=-70.0, lock=True, valley=False, onset=None, onset_db=9.0, onset_hold=None,
            onset_pos=0.5):
    beta = 2.0 ** (semitones / 12.0)
    wa, ws = windows(N, S)
    K = N // 2 + 1
    k = np.arange(K)
    nc = N - S / 2.0                      # centro de la ventana de sintesis dentro del cuadro
    # normalizacion de la suma solapada
    prod = (wa * ws)[N - S:]
    ola = sum(prod[i::R].sum() for i in range(R)) / R
    gain = 1.0 / ola

    n_frames = (len(x) - N) // R
    y = np.zeros(len(x) + N)
    prev_phase = np.zeros(K)
    prev_mag = np.zeros(K)
    phi_bin = np.zeros(K)                 # fase de rotacion acumulada, por bin de entrada (region)
    omega_bin = 2 * np.pi * k / N
    expected = omega_bin * R
    floor = 10 ** (floor_db / 20)

    # detector de ataques en el tiempo: energia del salto mas reciente contra los ~20 ms previos
    hold = 0
    refractory = 0
    hold_frames = (S // R) if onset_hold is None else onset_hold
    thr = 10 ** (onset_db / 10)
    past = int(0.02 * SR)

    for m in range(n_frames):
        raw = x[m * R: m * R + N]
        seg = raw * wa
        do_reset = False
        if onset:
            e_new = np.mean(raw[N - R:] ** 2)
            e_old = np.mean(raw[N - R - past: N - R] ** 2)
            if refractory > 0:
                refractory -= 1
            elif e_new > thr * e_old + 1e-7:
                do_reset = True
                hold = hold_frames
                refractory = int(0.05 * SR / R)
        X = np.fft.rfft(seg)
        mag = np.abs(X)
        ph = np.angle(X)

        # frecuencia instantanea por bin (rad/muestra)
        d = ph - prev_phase - expected
        d -= 2 * np.pi * np.round(d / (2 * np.pi))
        omega = omega_bin + d / R

        # picos: maximos locales sobre +-peak_half bins
        mx = mag.copy()
        for s in range(1, peak_half + 1):
            mx[s:] = np.maximum(mx[s:], mag[:-s])
            mx[:-s] = np.maximum(mx[:-s], mag[s:])
        is_pk = (mag >= mx) & (mag > floor * mag.max() + 1e-12)
        is_pk[0] = False
        pk = np.flatnonzero(is_pk)
        if len(pk) == 0:
            prev_phase, prev_mag = ph, mag
            continue

        # regiones: cada bin pertenece al pico mas cercano (frontera en el punto medio)
        if valley and len(pk) > 1:
            # frontera en el minimo de magnitud entre dos picos
            cm = np.minimum.accumulate  # (marcador; se calcula abajo por tramo)
            bounds = np.array([a + 1 + int(np.argmin(mag[a + 1: b])) if b > a + 1 else b
                               for a, b in zip(pk[:-1], pk[1:])])
        else:
            bounds = (pk[:-1] + pk[1:] + 1) // 2
        region = np.searchsorted(bounds, k, side="right")   # indice de pico por bin

        if lock:
            w_pk = omega[pk]
        else:
            w_pk = None
        if hold > 0:
            dw = (beta - 1.0) * omega_bin[pk]                # durante el ataque: frecuencia = centro del bin
            hold -= 1
        else:
            dw = (beta - 1.0) * omega[pk]                    # desplazamiento de frecuencia por pico
        dk = np.rint(dw * N / (2 * np.pi)).astype(int)       # en bins enteros

        # fase de rotacion: continua la de la region que ocupaba ese bin en el cuadro anterior
        phi = phi_bin[pk] + dw * R

        if do_reset:
            # ataque detectado: todas las regiones con rotacion nula en el instante del ataque
            phi = dw * (nc - (N - R * onset_pos))
        elif reset and not onset:
            # ataque: energia que sube bruscamente -> alinear fases de las regiones nuevas
            flux = mag[pk] / (prev_mag[pk] + 1e-9 * mag.max() + 1e-12)
            new = flux > reset_thresh
            if new.any():
                # rotacion nula en el borde mas reciente del cuadro (donde entra el ataque)
                phi[new] = dw[new] * (nc - (N - R / 2.0))

        phi_bin = phi[region]
        # el desplazamiento entero equivale a modular por exp(j*2pi*dk*n/N) dentro del cuadro;
        # se corrige para que la fase en el centro de sintesis sea phi
        rot = np.exp(1j * (phi - 2 * np.pi * dk * nc / N))

        Y = np.zeros(K, dtype=complex)
        tgt = k + dk[region]
        ok = (tgt >= 1) & (tgt < K - 1)
        np.add.at(Y, tgt[ok], (X * rot[region])[ok])

        out = np.fft.irfft(Y, N)[N - S:] * ws[N - S:] * gain
        a = m * R + N - S
        y[a: a + S] += out
        prev_phase, prev_mag = ph, mag

    return y[: len(x)]


if __name__ == "__main__":
    import sys
    t = np.arange(SR) / SR
    x = 0.5 * np.sin(2 * np.pi * 220 * t)
    for st in (0, 7, -12):
        y = process(x, st)
        s = y[SR // 4: SR // 4 + 16384]
        S_ = np.abs(np.fft.rfft(s * np.blackman(len(s))))
        f = np.fft.rfftfreq(len(s), 1 / SR)
        print(st, "pico", round(f[np.argmax(S_)], 1), "esperado", round(220 * 2 ** (st / 12), 1),
              "rms", round(np.sqrt(np.mean(s ** 2)), 3))
