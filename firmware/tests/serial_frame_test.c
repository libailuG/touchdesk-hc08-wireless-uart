#include "serial_frame.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned received_count;
static uint8_t expected[48];
static void received(const serial_frame_t *f,void *ctx){uint8_t actual[48];serial_frame_encode(f,actual);assert(!memcmp(actual,expected,48));received_count++;}
static unsigned rng=123456789;
static unsigned random32(void){rng=rng*1664525u+1013904223u;return rng;}
int main(void){
 assert(sizeof(serial_frame_t)==48&&offsetof(serial_frame_t,end_flag)==44);
 char shown[40];
 float samples[]={1.23f,1.234567f,-1.234567f,12.0f,-0.00001f,0.0001f,3.402823466e38f};
 const char *wanted[]={"1.23","1.2346","-1.2346","12","0","0.0001","3.4028e+38"};
 for(unsigned i=0;i<7;i++){serial_float_format(samples[i],shown,sizeof(shown));if(strcmp(shown,wanted[i]))printf("format %u: [%s] expected [%s]\n",i,shown,wanted[i]);assert(!strcmp(shown,wanted[i]));}
 serial_frame_t precise;serial_frame_init(&precise);precise.data3=1.234567f;
 uint8_t original[48],after[48];serial_frame_encode(&precise,original);
 serial_value_format(&precise,2,shown,sizeof(shown),false);assert(!strcmp(shown,"1.2346"));
 serial_value_format(&precise,2,shown,sizeof(shown),true);assert(strcmp(shown,"1.2346"));
 serial_frame_encode(&precise,after);assert(!memcmp(original,after,48));

 serial_frame_t f;serial_frame_init(&f);
 assert(serial_value_parse(&f,0,"4294967295")&&f.data1==UINT32_MAX);
 assert(!serial_value_parse(&f,0,"4294967296"));assert(!serial_value_parse(&f,0,"-1"));assert(!serial_value_parse(&f,0,"999999999999999999999999999999999"));
 assert(serial_value_parse(&f,1,"000000000000000000000000000000000001"));
 assert(serial_value_parse(&f,2,"-1.25"));assert(serial_value_parse(&f,9,"1.5e3"));
 assert(!serial_value_parse(&f,2,"1e99"));assert(!serial_value_parse(&f,2,"NaN"));assert(!serial_value_parse(&f,2,"1.2.3"));
 uint8_t raw[48];serial_frame_encode(&f,raw);assert(raw[0]==0xaa&&raw[3]==0xaa&&raw[44]==0xee&&raw[47]==0xee);
 assert(raw[4]==0xff&&raw[7]==0xff&&raw[8]==1&&raw[9]==0);assert(raw[12]==0&&raw[13]==0&&raw[14]==0xa0&&raw[15]==0xbf);
 serial_parser_t parser={0};
 for(unsigned round=0;round<10000;round++){
  serial_frame_init(&f);f.data1=random32();f.data2=random32();
  for(unsigned i=0;i<8;i++){uint32_t word=random32();memcpy((uint8_t*)&f+12+4*i,&word,4);}
  /* Marker and NaN/Inf bit patterns inside payload are legal and must survive. */
  if(round%5==0){uint32_t bits[]={SERIAL_START_FLAG,SERIAL_END_FLAG,0x7fc12345,0x7f800000};memcpy((uint8_t*)&f+12,bits,sizeof(bits));}
  serial_frame_encode(&f,expected);
  const uint8_t noise[]="system home\napp ui keysim\r\n";
  serial_parser_feed(&parser,noise,sizeof(noise)-1,received,NULL);
  uint8_t bad[48];memcpy(bad,expected,48);memset(bad+44,0,4);
  /* Keep the malformed frame payload free of extra candidate headers. */
  memset(bad+4,0x12,40);serial_parser_feed(&parser,bad,48,received,NULL);
  unsigned offset=0;
  while(offset<48){unsigned chunk=random32()%13+1;if(chunk>48-offset)chunk=48-offset;serial_parser_feed(&parser,expected+offset,chunk,received,NULL);offset+=chunk;}
 }
 assert(received_count==10000&&parser.bad_tail==10000);
 serial_frame_init(&f);serial_frame_encode(&f,expected);uint8_t burst[480];for(unsigned i=0;i<10;i++)memcpy(burst+i*48,expected,48);
 serial_parser_feed(&parser,burst,sizeof(burst),received,NULL);assert(received_count==10010&&parser.used==0);
 puts("PASS 48-byte LE wire layout, uint32/float32 validation, 10000 fragmented/noisy/malformed streams, embedded markers, NaN/Inf preservation and 10-frame burst");
}
