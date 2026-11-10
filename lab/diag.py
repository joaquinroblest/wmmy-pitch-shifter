import numpy as np, sys, json
import regla, pv
SR=48000
d=regla.Data("sub")
def aligned(c,st):
    L=d.lag(c); n=len(d.src); ca=np.zeros(n)
    if L>=0: ca[:n-L]=c[L:n][:n-L]
    else: ca[-L:]=c[:n+L]
    r=d.ref(st)[0]; ca*=np.sqrt(np.mean(r**2)/np.mean(ca**2)); return ca
def avgenv(x):
    e=regla.env(x,1.0); acc=[]
    for p in d.onsets:
        w=e[p-int(.03*SR):p+int(.05*SR)]; pk=regla.env(d.src,1.0)[p:p+int(.02*SR)].max()
        acc.append(w)
    return np.array(acc)
if __name__=="__main__":
    st=int(sys.argv[1]); kw=json.loads(sys.argv[2]) if len(sys.argv)>2 else {}
    y=pv.process(d.src,st,**kw)
    S={"src":avgenv(d.src),"ref":avgenv(d.ref(st)[0]),"ht":avgenv(aligned(d.ht(st),st)),"proto":avgenv(aligned(y,st))}
    # normalizar cada ataque por su nivel medio 20-40 ms despues del ataque
    t=(np.arange(S["src"].shape[1])/SR*1000-30)
    print("t_ms  "+"".join(f"{k:>9s}" for k in S))
    idx=[np.argmin(abs(t-v)) for v in (-20,-10,-6,-4,-2,-1,0,1,2,3,4,6,8,10,15,20,30,40)]
    norm={k:np.maximum(v[:, int(.05*SR):int(.07*SR)].mean(1,keepdims=True),1e-5) for k,v in S.items()}
    for i in idx:
        print(f"{t[i]:5.0f} "+"".join(f"{np.median(20*np.log10(S[k][:,i:i+1]/norm[k]+1e-6)):9.1f}" for k in S))
