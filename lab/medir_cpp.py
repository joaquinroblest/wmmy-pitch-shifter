"""Mide el motor C++ (offline_peak) con la regla. Uso: medir_cpp.py <dataset> [args extra de offline_peak...]"""
import subprocess, sys
import numpy as np
import regla
ds = sys.argv[1]; extra = sys.argv[2:]
d = regla.Data(ds)
rows_all = {}
print(f"## C++ {extra} ({ds})")
print(f"{'st':>4s}" + "".join(f"{k:>13s}" for k in regla.KEYS))
for st in (-12, -5, 7, 12):
    p = f"{d.dir}/c_peak_{st}.f32"
    r = subprocess.run(["/home/claude/build_offline/offline_peak", d.src_path, p, str(st)] + extra, capture_output=True, text=True)
    m = d.measure(regla.rd(p), st)
    print(f"{st:>+4d}" + "".join(f"{m[k]:>13}" for k in regla.KEYS))
print(r.stderr.strip())
