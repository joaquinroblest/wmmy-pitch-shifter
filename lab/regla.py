"""Regla de medicion con guitarra real (version 2).

Compara un candidato contra la senal original (retraso, ataques) y contra una
referencia de maxima calidad sin limite de retraso (Rubber Band offline).

Medidas (menor es mejor en todas):
  retraso_ms    : retraso global (alineando envolventes con la original)
  espectro_db   : distancia espectral media con la referencia, en notas sostenidas
  fantasma_db   : energia donde la referencia no tiene nada, en notas sostenidas
  ataque_ms     : alargamiento mediano de la subida de los ataques
  ataque_p90_ms : idem, en el 10 % peor de los ataques
  preeco_db     : energia antes del ataque, respecto de la original
"""
import os
import sys

import numpy as np
from scipy.ndimage import maximum_filter1d
from scipy.signal import correlate, find_peaks, stft

SR = 48000
LAB = "/home/claude/lab"
KEYS = ["retraso_ms", "espectro_db", "fantasma_db", "golpe_db", "golpe_p10_db", "desfase_ms", "jitter_ms", "preeco_db"]


def flux(x):
    """Funcion de deteccion de ataques: subida del espectro (ventana de 5 ms, paso 0,67 ms)."""
    f, t, Z = stft(x, SR, nperseg=256, noverlap=256 - 32, window="hann", padded=False, boundary=None)
    M = np.log1p(200.0 * np.abs(Z))
    d = np.maximum(0.0, M[:, 4:] - M[:, :-4]).sum(0)
    return d  # indice i  <->  muestra ~ (i + 4) * 32 + 128


def env(x, ms):
    n = max(1, int(SR * ms / 1000))
    return np.sqrt(np.convolve(x * x, np.ones(n) / n, mode="same"))


def rd(p):
    return np.fromfile(p, dtype=np.float32).astype(np.float64)


class Data:
    def __init__(self, name="sub"):
        self.dir = f"{LAB}/{name}"
        self.src_path = f"{self.dir}/src.f32"
        self.src = rd(self.src_path)
        self._ref = {}
        fast = env(self.src, 2)
        slow = env(self.src, 40)
        ratio = fast / (np.roll(slow, int(0.03 * SR)) + 1e-4)
        pk, _ = find_peaks(ratio, height=3.0, distance=int(0.12 * SR))
        self.onsets = np.array([p for p in pk if fast[p] > 0.02 and int(0.1 * SR) < p < len(self.src) - int(0.2 * SR)])
        self.src_att = self.attack_stats(self.src)
        self.src_flux = self.flux_stats(self.src)
        self.loglag = np.log(env(self.src, 5) + 1e-4)
        self.loglag -= self.loglag.mean()

    def ref(self, st):
        if st not in self._ref:
            r = rd(f"{self.dir}/ref_{st}.f32")
            f, R = spec(r)
            self._ref[st] = (r, f, R)
        return self._ref[st]

    def ht(self, st):
        return rd(f"{self.dir}/ht_{st}.f32")

    def lag(self, c, max_ms=200):
        b = np.log(env(c, 5) + 1e-4)
        b -= b.mean()
        cc = correlate(b, self.loglag, "full", method="fft")
        lags = np.arange(-len(self.loglag) + 1, len(b))
        m = (lags > -int(0.02 * SR)) & (lags < int(max_ms / 1000 * SR))
        return int(lags[np.argmax(np.where(m, cc, -np.inf))])

    def flux_stats(self, x):
        """Para cada ataque: fuerza maxima de la subida y en que instante ocurre."""
        fl = flux(x)
        w = int(0.02 * SR / 32)
        val, pos = [], []
        for p in self.onsets:
            i = int((p - 128) / 32) - 4
            seg = fl[max(0, i - w): i + w]
            j = int(np.argmax(seg))
            val.append(seg[j])
            pos.append((j - w) * 32 / SR * 1000)
        return np.array(val), np.array(pos)

    def attack_stats(self, x):
        e = env(x, 1.5)
        rises, pres = [], []
        for p in self.onsets:
            a, b = p - int(0.04 * SR), p + int(0.06 * SR)
            w = e[a:b]
            ipk = int(np.argmax(w))
            pkv = w[ipk]
            up = w[: ipk + 1]
            below10 = np.where(up < 0.1 * pkv)[0]
            t10 = below10[-1] if len(below10) else 0
            t90 = np.where(up >= 0.9 * pkv)[0][0] if pkv > 0 else 0
            rises.append((t90 - t10) / SR * 1000)
            pre = np.mean(x[p - int(0.03 * SR): p - int(0.008 * SR)] ** 2)
            post = np.mean(x[p: p + int(0.02 * SR)] ** 2)
            pres.append(10 * np.log10((pre + 1e-10) / (post + 1e-10)))
        return np.array(rises), np.array(pres)

    def measure(self, c, st):
        r, f, R = self.ref(st)
        n = min(len(c), len(self.src), len(r))
        L = self.lag(c[:n])
        ca = np.zeros(len(r))
        if L >= 0:
            ca[: n - L] = c[L:n]
        else:
            ca[-L:n] = c[: n + L]
        ca *= np.sqrt(np.mean(r ** 2) / (np.mean(ca ** 2) + 1e-20))
        out = {"retraso_ms": round(1000 * L / SR, 1)}

        _, C = spec(ca)
        band = (f >= 70) & (f <= 8000)
        Cb, Rb = C[band], R[band]
        hop = N_SPEC // 4
        centers = np.arange(Cb.shape[1]) * hop + N_SPEC // 2
        frame_e = Rb.sum(0)
        act = frame_e > frame_e.max() * 1e-4
        # solo notas sostenidas: lejos de los ataques
        sustained = np.ones(len(centers), bool)
        for p in self.onsets:
            sustained &= ~((centers > p - int(0.07 * SR)) & (centers < p + int(0.10 * SR)))
        sel = act & sustained
        Cs, Rs = Cb[:, sel], Rb[:, sel]
        floor = Rs.max(0, keepdims=True) * 1e-4
        lsd = np.sqrt(np.mean((10 * np.log10((Cs + floor) / (Rs + floor))) ** 2, axis=0))
        out["espectro_db"] = round(float(np.mean(lsd)), 2)
        Rm = maximum_filter1d(Rs, size=9, axis=0)
        excess = np.maximum(0.0, Cs - 4.0 * Rm).sum(0) / (Rs.sum(0) + 1e-20)
        out["fantasma_db"] = round(float(10 * np.log10(np.mean(excess) + 1e-10)), 1)

        rs, ps = self.src_att
        rc, pc = self.attack_stats(ca[: len(self.src)])
        vs, ts = self.src_flux
        vc, tc = self.flux_stats(ca[: len(self.src)])
        g = 20 * np.log10((vc + 1e-9) / (vs + 1e-9))
        out["golpe_db"] = round(float(np.median(g)), 1)
        out["golpe_p10_db"] = round(float(np.percentile(g, 10)), 1)
        dt = tc - ts
        out["desfase_ms"] = round(float(np.median(dt)), 2)
        out["jitter_ms"] = round(float(np.percentile(dt, 90) - np.percentile(dt, 10)), 2)
        out["preeco_db"] = round(float(np.median(pc - ps)), 1)
        return out


N_SPEC = 4096


def spec(x):
    f, t, Z = stft(x, SR, nperseg=N_SPEC, noverlap=N_SPEC - N_SPEC // 4, window="hann", padded=False, boundary=None)
    return f, np.abs(Z) ** 2


def show(rows, title=""):
    print(f"\n{title:14s}" + "".join(f"{k:>13s}" for k in KEYS))
    for name, m in rows:
        print(f"{name:14s}" + "".join(f"{m[k]:>13}" for k in KEYS))


if __name__ == "__main__":
    import subprocess
    d = Data(sys.argv[1] if len(sys.argv) > 1 else "sub")
    B = "/home/claude/build_offline"
    print("ataques detectados:", len(d.onsets))
    for st in (-12, -5, 7, 12):
        rows = [("HyperTune", d.measure(d.ht(st), st))]
        p = f"{d.dir}/c_live_{st}.f32"
        rows.append(("Referencia", d.measure(d.ref(st)[0], st)))
        if not os.path.exists(p):
            subprocess.run([f"{B}/offline_live", d.src_path, p, str(st)], check=True)
        rows.append(("EnVivo-v3", d.measure(rd(p), st)))
        p = f"{d.dir}/c_rapido_{st}.f32"
        if not os.path.exists(p):
            subprocess.run([f"{B}/offline_rb", d.src_path, p, str(st), "r3", "short"], check=True, capture_output=True)
        rows.append(("Rapido", d.measure(rd(p), st)))
        show(rows, f"=== {st:+d}")
