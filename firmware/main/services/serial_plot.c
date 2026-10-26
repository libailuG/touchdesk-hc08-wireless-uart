#include "serial_plot.h"
#include <math.h>
#include <string.h>
void serial_plot_push(serial_plot_t *h,const serial_sample_t *sample) {
 h->samples[h->head]=*sample;h->head=(h->head+1)%SERIAL_PLOT_CAPACITY;
 if(h->count<SERIAL_PLOT_CAPACITY)h->count++;
}
const serial_sample_t *serial_plot_at(const serial_plot_t *h,unsigned index) {
 return &h->samples[(h->head+SERIAL_PLOT_CAPACITY-h->count+index)%SERIAL_PLOT_CAPACITY];
}
void serial_plot_reduce(const serial_plot_t *h,unsigned channel,unsigned window_ms,serial_plot_view_t *v) {
 memset(v,0,sizeof(*v));if(!h->count||channel>=8||!window_ms)return;
 int64_t end=serial_plot_at(h,h->count-1)->received_us,width=(int64_t)window_ms*1000,start=end-width;
 unsigned first=0;while(first<h->count&&serial_plot_at(h,first)->received_us<start)first++;
 v->visible=h->count-first;v->span_us=end-serial_plot_at(h,first)->received_us;
 v->truncated=h->count==SERIAL_PLOT_CAPACITY&&serial_plot_at(h,0)->received_us>start;
 unsigned cursor=first;
 for(unsigned k=0;k<SERIAL_PLOT_POINTS;k++) {
  int64_t target=start+width*k/(SERIAL_PLOT_POINTS-1);
  float y=NAN;
  /* Leave unavailable early history blank instead of extending the first value. */
  if(target>=serial_plot_at(h,first)->received_us) {
   while(cursor+1<h->count&&serial_plot_at(h,cursor+1)->received_us<=target)cursor++;
   unsigned nearest=cursor;
   if(cursor+1<h->count&&serial_plot_at(h,cursor+1)->received_us-target<target-serial_plot_at(h,cursor)->received_us)nearest++;
   y=serial_plot_at(h,nearest)->values[channel];
  }
  v->points[v->count++]=(serial_plot_point_t){k*10000/(SERIAL_PLOT_POINTS-1),y};
  if(isfinite(y)){if(!v->finite){v->minimum=v->maximum=y;v->finite=true;}else {if(y<v->minimum)v->minimum=y;if(y>v->maximum)v->maximum=y;}}
 }
}
