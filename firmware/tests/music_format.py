from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1];s=(root/'main/services/music_service.c').read_text(encoding='utf-8')
fn=s[s.index('static uint16_t le16('):s.index('static void choose(')]
head=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
typedef unsigned UINT;
#define FR_OK 0
static uint8_t data[512];static unsigned size,pos,rate,channels,wav_remaining;static int file;
static unsigned f_size(int *f) { return size; }
static unsigned f_tell(int *f) { return pos; }
static int f_lseek(int *f,unsigned p) { if(p>size)return 1;pos=p;return FR_OK; }
static int f_read(int *f,void *out,unsigned n,unsigned *got) { *got=n<size-pos?n:size-pos;memcpy(out,data+pos,*got);pos+=*got;return FR_OK; }
'''
tail=r'''
static void fixture(void) {
 memset(data,0,sizeof(data));memcpy(data,"RIFF",4);memcpy(data+8,"WAVEfmt ",8);data[16]=16;data[20]=1;data[22]=2;
 data[24]=0x80;data[25]=0xbb;data[28]=0;data[29]=0xee;data[30]=2;data[32]=4;data[34]=16;memcpy(data+36,"data",4);data[40]=8;size=52;pos=0;
}
int main(void) {
 fixture();assert(wav_header()&&channels==2&&rate==48000&&wav_remaining==8&&pos==44);
 fixture();data[34]=24;assert(!wav_header());fixture();data[20]=3;assert(!wav_header());
 fixture();data[22]=0;assert(!wav_header());fixture();data[32]=3;assert(!wav_header());
 fixture();size=40;assert(!wav_header());fixture();data[40]=255;assert(!wav_header());
 fixture();data[40]=7;assert(!wav_header());fixture();memcpy(data+8,"XXXX",4);assert(!wav_header());
 fixture();memmove(data+48,data+36,16);memcpy(data+36,"JUNK",4);data[40]=3;memset(data+41,0,7);size+=12;assert(wav_header()&&pos==56);
 puts("PASS production WAV parser: valid PCM, unknown odd-size chunk/padding, truncation, unsupported encoding/bits, invalid block alignment/channel count, unaligned data size, container signature");
}
'''
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'test.c').write_text(head+fn+tail,encoding='utf-8');exe=d/'test.exe';subprocess.run(['C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe',str(d/'test.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
