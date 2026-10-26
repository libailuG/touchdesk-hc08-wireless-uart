#include "xiaozhi_protocol.h"
#include <string.h>
#include <math.h>
int xz_assemble_ws(xz_assembly_t *s,unsigned opcode,bool fin,int total,int offset,const void *data,int length) {
 if(opcode>2)return 0;
 if(s->complete)memset(s,0,sizeof(*s));
 if(total<0||offset<0||length<0||offset>total||length>total-offset||(length&&!data))return -1;
 if(!offset){
  if(opcode){if(s->used||s->type)return -1;s->type=opcode;}
  else if(!s->type)return -1;
  s->frame_offset=0;s->frame_size=total;
 }
 if(!s->type||s->frame_size!=(size_t)total||s->frame_offset!=(size_t)offset||s->used+(size_t)length>XZ_PACKET_MAX)return -1;
 if(length)memcpy(s->data+s->used,data,length);
 s->used+=length;s->frame_offset+=length;
 if(s->frame_offset==(size_t)total&&fin){s->data[s->used]=0;s->complete=true;return 1;}
 return 0;
}
int xz_assemble_mqtt(xz_assembly_t *s,int total,int offset,const void *data,int length) {
 if(!offset){if(s->used&&!s->complete)return -1;memset(s,0,sizeof(*s));s->frame_size=total;}
 if(total<=0||total>XZ_PACKET_MAX||length<0||offset<0||offset>total||length>total-offset||s->frame_size!=(size_t)total||s->used!=(size_t)offset||(length&&!data))return -1;
 if(length){memcpy(s->data+s->used,data,length);}
 s->used+=length;
 if(s->used==(size_t)total){s->data[s->used]=0;s->complete=true;return 1;}return 0;
}
static const float taps[63]={-0.0006643934f,0.0000000000f,0.0007938078f,0.0010927515f,0.0004271493f,-0.0009913698f,-0.0020678352f,-0.0014883835f,0.0009537735f,0.0035552328f,0.0036371479f,-0.0000000000f,-0.0051522953f,-0.0071409385f,-0.0027207442f,0.0060405670f,0.0119505201f,0.0081454566f,-0.0049569971f,-0.0176487451f,-0.0173799432f,0.0000000000f,0.0235016540f,0.0324189109f,0.0124893404f,-0.0286046813f,-0.0599669856f,-0.0450192582f,0.0320878499f,0.1499018245f,0.2568448306f,0.2999235078f,0.2568448306f,0.1499018245f,0.0320878499f,-0.0450192582f,-0.0599669856f,-0.0286046813f,0.0124893404f,0.0324189109f,0.0235016540f,0.0000000000f,-0.0173799432f,-0.0176487451f,-0.0049569971f,0.0081454566f,0.0119505201f,0.0060405670f,-0.0027207442f,-0.0071409385f,-0.0051522953f,-0.0000000000f,0.0036371479f,0.0035552328f,0.0009537735f,-0.0014883835f,-0.0020678352f,-0.0009913698f,0.0004271493f,0.0010927515f,0.0007938078f,0.0000000000f,-0.0006643934f};
void xz_decimator_reset(xz_decimator_t *s) { memset(s,0,sizeof(*s)); }
size_t xz_decimate(xz_decimator_t *s,const int16_t *in,size_t n,int16_t *out,size_t cap) {
 size_t used=0;
 for(size_t i=0;i<n;i++) {
  s->history[s->pos]=in[i];s->pos=(s->pos+1)%63;
  if(++s->phase!=3) continue;
  s->phase=0;float y=0;unsigned p=s->pos;
  for(unsigned j=0;j<63;j++) {p=p?p-1:62;y+=taps[j]*s->history[p];}
  if(used<cap) out[used++]=(int16_t)(y>32767?32767:y< -32768?-32768:y);
 }
 return used;
}
bool xz_copy(char *dst,size_t cap,const char *src) {
 if(!dst||!cap||!src||strlen(src)>=cap) return false;
 memcpy(dst,src,strlen(src)+1);return true;
}
bool xz_hex16(const char *p,uint8_t out[16]) {
 if(!p||strlen(p)!=32) return false;
 for(unsigned i=0;i<16;i++) {
  unsigned v=0;
  for(unsigned j=0;j<2;j++) {unsigned c=(unsigned char)*p++;unsigned d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:99;if(d>15)return false;v=v*16+d;}
  out[i]=v;
 }
 return true;
}
static uint32_t be32(const uint8_t *p) {return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
bool xz_udp_header(const uint8_t *p,size_t n,uint32_t prev,uint32_t *seq) {
 if(!p||!seq||n<17||p[0]!=1||n>XZ_PACKET_MAX+16||n!=(size_t)16+((unsigned)p[2]<<8|p[3])) return false;
 *seq=be32(p+12);return *seq>prev;
}
bool xz_ws_audio(unsigned v,const uint8_t **data,size_t *n) {
 if(!data||!*data||!n||!*n) return false;
 const uint8_t *p=*data;
 if(v==2) {if(*n<16||p[0]!=0||p[1]!=2||p[2]||p[3]||be32(p+12)!=*n-16)return false;*data+=16;*n-=16;}
 else if(v==3) {if(*n<4||p[0]||((unsigned)p[2]<<8|p[3])!=*n-4)return false;*data+=4;*n-=4;}
 else if(v!=1) return false;
 return *n>0&&*n<=XZ_PACKET_MAX;
}
const char *xz_string(const cJSON *o,const char *key) {const cJSON *v=cJSON_GetObjectItemCaseSensitive(o,key);return cJSON_IsString(v)&&v->valuestring?v->valuestring:"";}
bool xz_valid_session(const cJSON *o,const char *session) {
 const cJSON *id=cJSON_GetObjectItemCaseSensitive(o,"session_id");
 return !id||(cJSON_IsString(id)&&id->valuestring&&!strcmp(id->valuestring,session));
}
void xz_display_text(char *out,size_t cap,const char *s) {
 if(!out||!cap)return;
 if(!s)s="";
 size_t n=strlen(s);if(n>=cap)n=cap-1;
 while(n&&((unsigned char)s[n]&0xc0)==0x80)n--;
 memcpy(out,s,n);out[n]=0;
}
