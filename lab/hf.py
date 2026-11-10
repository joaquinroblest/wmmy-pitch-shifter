import numpy as np, sys, json
import pv
from scipy.signal import stft
SR=48000
x=np.fromfile("sub/src.f32",dtype=np.float32).astype(float)[:SR*25]
def lv(y,ref=None):
    f,t,Z=stft(y,SR,nperseg=4096,noverlap=3072); P=(np.abs(Z)**2).mean(1)
    e=[70,300,1200,2400,4800,8000,12000]
    return [round(10*np.log10(P[(f>=a)&(f<b)].sum()+1e-20),1) for a,b in zip(e[:-1],e[1:])]
st=int(sys.argv[1])
r=np.fromfile(f"sub/ref_{st}.f32",dtype=np.float32).astype(float)[:SR*25]
print("ref      ",lv(r)); print("ht       ",lv(np.fromfile(f"sub/ht_{st}.f32",dtype=np.float32).astype(float)[:SR*25]))
for kw in [json.loads(a) for a in sys.argv[2:]]:
    print(kw, lv(pv.process(x,st,**kw)))
