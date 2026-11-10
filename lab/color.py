import numpy as np, sys, json
import regla, pv
SR=48000
d=regla.Data("sub")
def bands(c,st):
    r,f,R=d.ref(st); L=d.lag(c); n=len(r); ca=np.zeros(n)
    m=min(len(c),n)
    if L>=0: ca[:m-L]=c[L:m]
    else: ca[-L:m]=c[:m+L]
    ca*=np.sqrt(np.mean(r**2)/np.mean(ca**2))
    _,C=regla.spec(ca)
    fe=R.sum(0); act=fe>fe.max()*1e-4
    edges=[70,150,300,600,1200,2400,4800,8000,12000]
    out=[]
    for a,b in zip(edges[:-1],edges[1:]):
        k=(f>=a)&(f<b); Cb=C[k][:,act]; Rb=R[k][:,act]
        tilt=10*np.log10(Cb.sum()/Rb.sum())
        fl=Rb.max(0,keepdims=True)*1e-6+R[:,act].max(0,keepdims=True)*1e-8
        lsd=np.sqrt(np.mean((10*np.log10((Cb+fl)/(Rb+fl)))**2))
        out.append((a,b,round(tilt,1),round(lsd,1)))
    return out
if __name__=="__main__":
    st=int(sys.argv[1]); kw=json.loads(sys.argv[2]) if len(sys.argv)>2 else {}
    y=pv.process(d.src,st,**kw)
    P=bands(y,st); H=bands(d.ht(st),st)
    print("banda          proto(nivel,dist)   HT(nivel,dist)")
    for p,h in zip(P,H): print(f"{p[0]:>5}-{p[1]:<6} {p[2]:>7} {p[3]:>6}     {h[2]:>7} {h[3]:>6}")
