#include "mic_meter.h"
#include <math.h>
#include <string.h>
static unsigned level(float db) { return db<=-60?0:db>=0?100:(unsigned)((db+60)*100/60); }
void mic_meter_process(const int16_t *pcm,size_t n,mic_meter_result_t *out) {
 memset(out,0,sizeof(*out)); out->dbfs=-96;
 if(!n) return;
 int64_t sum=0; uint64_t square=0; unsigned peak=0;
 for(size_t i=0;i<n;i++) {
  int32_t v=pcm[i]; unsigned a=v<0?-v:v;
  sum+=v; square+=(int64_t)v*v;
  if(a>peak) peak=a;
  if(a>=32760) out->clipped++;
 }
 double mean=(double)sum/n,variance=(double)square/n-mean*mean;
 double rms=sqrt(variance>0?variance:0)/32768.0;
 out->dbfs=rms>0?20*log10(rms):-96;
 if(out->dbfs<-96) out->dbfs=-96;
 out->level=level(out->dbfs);
 out->peak_level=peak?level(20*log10((double)peak/32768)):0;
}
