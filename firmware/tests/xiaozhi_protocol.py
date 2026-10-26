"""Run the actual protocol, message assembly, UTF-8 and FIR code on a host CPU."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
source=r'''#include "xiaozhi_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static int16_t input[48000],a[16000],b[16000];
static double tone(unsigned hz) {
 xz_decimator_t d;xz_decimator_reset(&d);
 for(int i=0;i<48000;i++)input[i]=(int16_t)(12000*sin(2*3.141592653589793*hz*i/48000));
 assert(xz_decimate(&d,input,48000,a,16000)==16000);
 double e=0;for(int i=100;i<16000;i++)e+=(double)a[i]*a[i];return sqrt(e/15900);
}
int main(void) {
 xz_decimator_t d;xz_decimator_reset(&d);size_t used=0;
 for(int i=0;i<48000;i++)input[i]=(i*7919)%60001-30000;
 assert(xz_decimate(&d,input,48000,a,16000)==16000);
 xz_decimator_reset(&d);
 for(int pos=0;pos<48000;) {int n=(pos*17+11)%961+1;if(n>48000-pos)n=48000-pos;used+=xz_decimate(&d,input+pos,n,b+used,16000-used);pos+=n;}
 assert(used==16000&&!memcmp(a,b,sizeof(a)));
 double low=tone(1000),high=tone(12000);assert(low>8300&&low<8600&&high/low<0.003);
 uint8_t hex[16];assert(xz_hex16("000102030405060708090a0b0c0d0e0f",hex)&&hex[15]==15);
 assert(!xz_hex16("00",hex)&&!xz_hex16("zz0102030405060708090a0b0c0d0e0f",hex));
 uint8_t packet[24]={1,0,0,8};packet[15]=1;uint32_t seq;
 assert(xz_udp_header(packet,24,0,&seq)&&seq==1);assert(!xz_udp_header(packet,24,1,&seq));
 assert(!xz_udp_header(packet,23,0,&seq));assert(!xz_udp_header(packet,15,0,&seq));packet[0]=2;assert(!xz_udp_header(packet,24,0,&seq));
 const uint8_t *data=packet;size_t n=24;assert(xz_ws_audio(1,&data,&n)&&n==24);
 uint8_t v2[17]={0,2,0,0};v2[15]=1;v2[16]=42;data=v2;n=17;assert(xz_ws_audio(2,&data,&n)&&n==1&&*data==42);
 v2[3]=1;data=v2;n=17;assert(!xz_ws_audio(2,&data,&n));
 uint8_t v3[5]={0,0,0,1,42};data=v3;n=5;assert(xz_ws_audio(3,&data,&n)&&n==1);data=v3;n=4;assert(!xz_ws_audio(3,&data,&n));
 xz_assembly_t s={0};
 assert(xz_assemble_ws(&s,1,false,3,0,"ab",2)==0);
 assert(xz_assemble_ws(&s,1,false,3,2,"c",1)==0);
 assert(xz_assemble_ws(&s,9,true,1,0,"!",1)==0);
 assert(xz_assemble_ws(&s,0,true,3,0,"def",3)==1&&s.used==6&&!memcmp(s.data,"abcdef",6));
 assert(xz_assemble_ws(&s,2,true,1,0,"x",1)==1&&s.type==2&&s.used==1);
 memset(&s,0,sizeof(s));assert(xz_assemble_ws(&s,0,true,2,0,"xx",2)==-1);
 memset(&s,0,sizeof(s));assert(xz_assemble_ws(&s,1,true,3,0,"a",1)==0);assert(xz_assemble_ws(&s,1,true,3,2,"b",1)==-1);
 memset(&s,0,sizeof(s));assert(xz_assemble_ws(&s,1,false,3,0,"abc",3)==0);assert(xz_assemble_ws(&s,1,true,1,0,"x",1)==-1);
 char huge[4097];memset(huge,'z',sizeof(huge));memset(&s,0,sizeof(s));assert(xz_assemble_ws(&s,2,true,4096,0,huge,4096)==1&&s.used==4096&&s.data[4096]==0);
 assert(xz_assemble_ws(&s,2,true,4097,0,huge,4097)==-1);
 memset(&s,0,sizeof(s));assert(xz_assemble_mqtt(&s,6,0,"ab",2)==0);assert(xz_assemble_mqtt(&s,6,2,"cdef",4)==1&&!memcmp(s.data,"abcdef",6));
 assert(xz_assemble_mqtt(&s,1,0,"x",1)==1);assert(xz_assemble_mqtt(&s,4097,0,huge,4097)==-1);
 memset(&s,0,sizeof(s));assert(xz_assemble_mqtt(&s,3,0,"a",1)==0);assert(xz_assemble_mqtt(&s,3,0,"a",1)==-1);
 for(int total=-2;total<20;total++)for(int offset=-2;offset<22;offset++)for(int len=-2;len<22;len++){memset(&s,0,sizeof(s));xz_assemble_ws(&s,1,true,total,offset,huge,len);memset(&s,0,sizeof(s));xz_assemble_mqtt(&s,total,offset,huge,len);}
 char text[8];xz_display_text(text,8,"你好世界");assert(!strcmp(text,"你好"));xz_display_text(text,1,"你好");assert(!text[0]);
 cJSON *o=cJSON_Parse("{\"session_id\":\"abc\"}");assert(xz_valid_session(o,"abc")&&!xz_valid_session(o,"def"));cJSON_Delete(o);
 o=cJSON_Parse("{\"session_id\":5}");assert(!xz_valid_session(o,"abc"));cJSON_Delete(o);
 printf("PASS production XiaoZhi protocol: FIR continuity/anti-alias %.6f, WS v1-v3, UDP lengths/replay, fragmented WS/MQTT, malformed boundary sweep, UTF-8, session checks\n",high/low);
 return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='xiaozhi-protocol-') as d:
 d=Path(d);(d/'test.c').write_text(source,encoding='utf-8');exe=d/'test.exe'
 json=Path('C:/play/esp32s3touchscreen/toolchains/idf-5.4.1-cn/components/json/cJSON')
 cmd=['C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe','-I'+str(r/'main/services'),'-I'+str(json),str(d/'test.c'),str(r/'main/services/xiaozhi_protocol.c'),str(json/'cJSON.c'),'-o',str(exe)]
 subprocess.run(cmd,check=True);subprocess.run([str(exe)],check=True)
