#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "serial_plot.h"
static serial_plot_t h;static serial_plot_view_t v;
static void verify_nearest(unsigned window_ms,unsigned channel) {
 serial_plot_reduce(&h,channel,window_ms,&v);assert(v.count==SERIAL_PLOT_POINTS);
 int64_t end=serial_plot_at(&h,h.count-1)->received_us,start=end-(int64_t)window_ms*1000;
 unsigned first=0;while(serial_plot_at(&h,first)->received_us<start)first++;
 for(unsigned k=0;k<SERIAL_PLOT_POINTS;k++) {
  assert(v.points[k].x==k*10000/(SERIAL_PLOT_POINTS-1));
  int64_t target=start+(int64_t)window_ms*1000*k/(SERIAL_PLOT_POINTS-1);
  if(target<serial_plot_at(&h,first)->received_us){assert(!isfinite(v.points[k].value));continue;}
  unsigned nearest=first;int64_t distance=INT64_MAX;
  for(unsigned i=first;i<h.count;i++) {
   int64_t t=serial_plot_at(&h,i)->received_us,d=t>target?t-target:target-t;
   if(d<distance){distance=d;nearest=i;}
  }
  float expected=serial_plot_at(&h,nearest)->values[channel];
  assert((isnan(expected)&&isnan(v.points[k].value))||expected==v.points[k].value);
 }
}
int main(void) {
 serial_sample_t s={0};
 for(unsigned i=0;i<15000;i++){s.sequence=i+1;s.received_us=(int64_t)i*1000;s.values[0]=(float)i;s.values[7]=42;serial_plot_push(&h,&s);}
 assert(h.count==12000&&serial_plot_at(&h,0)->sequence==3001);
 unsigned windows[]={250,1000,2000,5000,10000};
 for(unsigned i=0;i<5;i++) {
  verify_nearest(windows[i],0);assert(v.visible==windows[i]+1&&v.span_us==(int64_t)windows[i]*1000&&!v.truncated);
 }
 /* Extreme amplitudes between time-grid selections must not affect selection or Y range. */
 h.samples[(h.head+12000-h.count+2000)%12000].values[0]=123456;
 h.samples[(h.head+12000-h.count+2001)%12000].values[0]=-123456;
 verify_nearest(10000,0);assert(v.minimum==4999&&v.maximum==14999);
 verify_nearest(5000,7);assert(v.minimum==42&&v.maximum==42);
 h.samples[(h.head+12000-1)%12000].values[0]=NAN;verify_nearest(5000,0);assert(isnan(v.points[SERIAL_PLOT_POINTS-1].value));
 /* Irregular arrival times select nearest samples, never value extrema or interpolated values. */
 memset(&h,0,sizeof(h));int64_t times[]={0,1000,4000,17000,51000,300000,701000,1000000};
 for(unsigned i=0;i<8;i++){s.received_us=times[i];s.values[0]=i;serial_plot_push(&h,&s);}verify_nearest(1000,0);
 memset(&h,0,sizeof(h));for(unsigned i=0;i<1000;i++){s.received_us=(int64_t)i*20000;s.values[0]=i;serial_plot_push(&h,&s);}
 verify_nearest(5000,0);assert(v.visible==251&&v.span_us==5000000);
 /* Early unavailable history remains blank, and equal-distance ties choose earlier samples. */
 memset(&h,0,sizeof(h));s.received_us=0;s.values[0]=10;serial_plot_push(&h,&s);
 s.received_us=2000;s.values[0]=20;serial_plot_push(&h,&s);s.received_us=999000;s.values[0]=30;serial_plot_push(&h,&s);
 verify_nearest(999,0);assert(v.points[1].value==10);verify_nearest(1000,0);assert(isnan(v.points[0].value)&&v.points[SERIAL_PLOT_POINTS-1].value==30);
 memset(&h,0,sizeof(h));for(unsigned i=0;i<13000;i++){s.received_us=i*100;serial_plot_push(&h,&s);}
 serial_plot_reduce(&h,0,10000,&v);assert(v.truncated&&v.count==SERIAL_PLOT_POINTS&&isnan(v.points[0].value));
 h.head=h.count=0;serial_plot_reduce(&h,0,5000,&v);assert(!v.visible&&!v.count&&!v.finite);
 puts("PASS uniform 1000-point time grid, nearest raw values at 1ms/20ms/irregular times, all 5 windows, no extrema bias, selected NaN gap, ties, partial history, 12000 ring/truncation/clear");return 0;
}
