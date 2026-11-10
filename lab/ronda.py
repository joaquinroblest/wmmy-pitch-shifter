"""Corre una configuracion del prototipo sobre el subconjunto y la mide."""
import sys, json, time
import numpy as np
from multiprocessing import Pool
import regla, pv

DS = "sub"

def init(ds):
    global DS
    DS = ds

def one(args):
    st, kw = args
    d = regla.Data(DS)
    y = pv.process(d.src, st, **kw)
    return st, d.measure(y, st)

def run(name, kw, sts=(-12, -5, 7, 12), ds="sub"):
    t = time.time()
    with Pool(2, initializer=init, initargs=(ds,)) as p:
        res = dict(p.map(one, [(st, kw) for st in sts]))
    print(f"\n## {name}  {kw}  ({time.time()-t:.0f}s)")
    print(f"{'st':>4s}" + "".join(f"{k:>13s}" for k in regla.KEYS))
    for st in sts:
        print(f"{st:>+4d}" + "".join(f"{res[st][k]:>13}" for k in regla.KEYS))
    return res

if __name__ == "__main__":
    kw = json.loads(sys.argv[2]) if len(sys.argv) > 2 else {}
    run(sys.argv[1], kw, ds=sys.argv[3] if len(sys.argv) > 3 else "sub")
