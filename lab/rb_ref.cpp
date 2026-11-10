// Referencia fuera de linea: Rubber Band R3 en modo offline (maxima calidad).
#include "rubberband/RubberBandStretcher.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
using R = RubberBand::RubberBandStretcher;
int main(int argc,char**argv){
  if(argc<4) return 1;
  std::FILE*f=std::fopen(argv[1],"rb"); std::vector<float> in; float t[4096]; size_t n;
  while((n=std::fread(t,4,4096,f))>0) in.insert(in.end(),t,t+n); std::fclose(f);
  R rb(48000,1,R::OptionProcessOffline|R::OptionEngineFiner,1.0,std::pow(2.0,std::atof(argv[3])/12.0));
  rb.setExpectedInputDuration(in.size());
  const float* p=in.data(); rb.study(&p,in.size(),true);
  std::vector<float> out, ob(65536);
  const size_t B=8192;
  for(size_t pos=0;pos<in.size();pos+=B){ size_t l=std::min(B,in.size()-pos); const float*ip=in.data()+pos; rb.process(&ip,l,pos+l>=in.size());
    int a; while((a=rb.available())>0){ float*op=ob.data(); int g=rb.retrieve(&op,std::min(a,65536)); out.insert(out.end(),ob.begin(),ob.begin()+g);} }
  int a; while((a=rb.available())>0){ float*op=ob.data(); int g=rb.retrieve(&op,std::min(a,65536)); out.insert(out.end(),ob.begin(),ob.begin()+g);}
  out.resize(in.size(),0.f);
  f=std::fopen(argv[2],"wb"); std::fwrite(out.data(),4,out.size(),f); std::fclose(f); return 0; }
