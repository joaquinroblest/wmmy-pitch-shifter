// Version rapida: busca el margen M minimo (puede ser negativo) sin cortes
// con el esquema exacto "salida t = cruda t - pad - M".
#include "rubberband/RubberBandStretcher.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using R = RubberBand::RubberBandStretcher;
// Devuelve el minimo M sin cortes para esta config, procesando una sola vez:
// guarda en que instante llega cada muestra cruda y calcula el M necesario.
static long minMargin(int mode,int st,int B,bool changeMid){
  const int sr=48000, total=sr*4;
  std::vector<float> in(total); for(int i=0;i<total;++i) in[i]=0.2f*std::sin(2*M_PI*196.0*i/sr)*((i/3000)%3?1.f:0.3f);
  int o=R::OptionProcessRealTime|R::OptionEngineFiner|R::OptionPitchHighConsistency|R::OptionChannelsTogether; if(!mode) o|=R::OptionWindowShort;
  R rb(sr,1,o,1.0,std::pow(2.0,(changeMid?0:st)/12.0)); rb.setMaxProcessSize(B);
  std::vector<float> ob(8192), zeros(B,0.f);
  long rawCount=0;
  auto pull=[&]{int a; while((a=rb.available())>0){float* p=ob.data(); int g=rb.retrieve(&p,std::min(a,8192)); rawCount+=g;} };
  const int pad=rb.getPreferredStartPad(); int rem=pad; while(rem>0){int l=std::min(rem,B); const float* z=zeros.data(); rb.process(&z,l,false); rem-=l; pull();}
  // Tras procesar el bloque que termina en t_end, hay rawCount muestras crudas.
  // Las salidas t in [pos, pos+B) necesitan la cruda t-pad-M < rawCount  =>  M > t-pad-rawCount.
  long need=-100000;
  for(int pos=0; pos+B<=total; pos+=B){
    if(changeMid && pos==sr) rb.setPitchScale(std::pow(2.0,st/12.0));
    const float* ip=in.data()+pos; rb.process(&ip,B,false); pull();
    if(pos<sr/2) continue; // ignorar el arranque
    long t_last=pos+B-1; long m=t_last-pad-rawCount+1; if(m>need) need=m;
  }
  return need;
}
int main(int argc,char**argv){
  setvbuf(stdout,nullptr,_IONBF,0);
  int mode=std::atoi(argv[1]);
  for(int st:{-24,-12,-5,0,7,12,24}){
    long worst=-100000;
    for(int B:{32,64,128,256,512,1024,2048}){ long m=minMargin(mode,st,B,false); if(m>worst) worst=m; }
    long chg=minMargin(mode,st,512,true);
    int o=R::OptionProcessRealTime|R::OptionEngineFiner|R::OptionPitchHighConsistency|R::OptionChannelsTogether; if(!mode) o|=R::OptionWindowShort;
    R p(48000,1,o,1.0,std::pow(2.0,st/12.0));
    long base=p.getPreferredStartPad()+p.getStartDelay();
    printf("%s st=%+3d  M_minimo=%5ld (cambiando en marcha: %5ld)  retraso=pad+delay+M = %.1f ms\n", mode?"calidad":"rapido ", st, worst, chg, 1000.0*(base+std::max(worst,chg))/48000);
  }
}
