#include "serial_frame.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <float.h>
typedef char serial_ieee754_required[(FLT_RADIX==2&&FLT_MANT_DIG==24&&FLT_MAX_EXP==128)?1:-1];
static uint32_t load32(const uint8_t *p) { return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24); }
static void store32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i)); }
void serial_frame_init(serial_frame_t *f) { memset(f,0,sizeof(*f));f->start_flag=SERIAL_START_FLAG;f->end_flag=SERIAL_END_FLAG; }
void serial_frame_encode(const serial_frame_t *f,uint8_t out[48]) {
 store32(out,SERIAL_START_FLAG);store32(out+4,f->data1);store32(out+8,f->data2);
 for(unsigned i=0;i<8;i++){uint32_t word;memcpy(&word,(const uint8_t*)f+12+4*i,4);store32(out+12+4*i,word);}
 store32(out+44,SERIAL_END_FLAG);
}
bool serial_frame_decode(const uint8_t in[48],serial_frame_t *f) {
 if(load32(in)!=SERIAL_START_FLAG||load32(in+44)!=SERIAL_END_FLAG)return false;
 serial_frame_init(f);f->data1=load32(in+4);f->data2=load32(in+8);
 for(unsigned i=0;i<8;i++){uint32_t word=load32(in+12+4*i);memcpy((uint8_t*)f+12+4*i,&word,4);}
 return true;
}
static void discard(serial_parser_t *p) { memmove(p->bytes,p->bytes+1,--p->used);p->discarded++; }
void serial_parser_feed(serial_parser_t *p,const uint8_t *data,size_t n,serial_frame_callback_t cb,void *ctx) {
 for(size_t i=0;i<n;i++) {
  p->bytes[p->used++]=data[i];
  while(p->used>=4&&load32(p->bytes)!=SERIAL_START_FLAG)discard(p);
  if(p->used==48) {
   serial_frame_t f;
   if(serial_frame_decode(p->bytes,&f)){p->used=0;if(cb)cb(&f,ctx);}
   else {p->bad_tail++;discard(p);while(p->used>=4&&load32(p->bytes)!=SERIAL_START_FLAG)discard(p);}
  }
 }
}
bool serial_value_parse(serial_frame_t *f,unsigned index,const char *text) {
 if(index>=10||!text||!*text)return false;
 char *end;
 if(index<2) {
  for(const char *p=text;*p;p++)if(*p<'0'||*p>'9')return false;
  uint32_t n=0;
  for(const char *p=text;*p;p++){unsigned d=(unsigned)(*p-'0');if(n>(UINT32_MAX-d)/10)return false;n=n*10+d;}
  if(index==0)f->data1=(uint32_t)n;else f->data2=(uint32_t)n;
 } else {
  for(const char *p=text;*p;p++)if(!strchr("0123456789.+-eE",*p))return false;
  float n=strtof(text,&end);if(end==text||*end||!isfinite(n))return false;
  memcpy((uint8_t*)f+12+4*(index-2),&n,4);
 }
 return true;
}
/* Display only: at most four fractional digits, no redundant zeroes.
 * Keep full float32 round-trip precision in the editor and on the wire. */
void serial_float_format(float value,char *text,size_t capacity) {
 char buffer[96];snprintf(buffer,sizeof(buffer),"%.4f",(double)value);
 if(strlen(buffer)>12)snprintf(buffer,sizeof(buffer),"%.4e",(double)value);
 char *exp=strchr(buffer,'e'),*dot=strchr(buffer,'.');
 if(dot) {
  char *end=exp?exp:buffer+strlen(buffer);
  while(end>dot+1&&end[-1]=='0')end--;
  if(end>dot&&end[-1]=='.')end--;
  if(exp)memmove(end,exp,strlen(exp)+1);else *end=0;
 }
 exp=strchr(buffer,'e');
 if(exp){char *digits=exp+1;if(*digits=='+'||*digits=='-')digits++;while(digits[0]=='0'&&digits[1])memmove(digits,digits+1,strlen(digits));}
 if(!strcmp(buffer,"-0"))strcpy(buffer,"0");
 snprintf(text,capacity,"%s",buffer);
}
void serial_value_format(const serial_frame_t *f,unsigned index,char *text,size_t capacity,bool editing) {
 if(index<2)snprintf(text,capacity,"%lu",(unsigned long)(index?f->data2:f->data1));
 else if(index<10){float v;memcpy(&v,(const uint8_t*)f+12+4*(index-2),4);if(editing)snprintf(text,capacity,"%.9g",(double)v);else serial_float_format(v,text,capacity);}
 else if(capacity)*text=0;
}
