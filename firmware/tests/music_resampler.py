from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'main/services/music_service.c').read_text(encoding='utf-8')
fn=s[s.index('static bool send_audio(unsigned frames,unsigned volume) {'):s.index('/* Returns 1 for audio')]
head=r'''
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#define portMAX_DELAY 0
#define xSemaphoreTake(a,b) ((void)0)
#define xSemaphoreGive(a) ((void)0)
static int guard;
static struct { unsigned volume,seconds; } state;
static unsigned rate,channels,output_rate,output_channels,resample_phase;
static uint64_t samples,emitted;
static bool stream,have_previous;
static int16_t previous_l,previous_r,pcm[4096];
static struct { int16_t left[8],data[4096],right[8]; } storage;
static int16_t *converted=storage.data;
static bool start_output(void) { stream=true; return true; }
#define atomic_load(p) false
#define pdMS_TO_TICKS(n) (n)
#define ESP_OK 0
static bool disconnected;
static int speaker;
static void status(const char *s) { assert(!"unexpected output error"); }
static int uac_host_device_write(int handle,uint8_t *data,unsigned bytes,unsigned timeout) {
 assert(data==(uint8_t*)converted&&bytes%(output_channels*2)==0);
 unsigned n=bytes/(output_channels*2);assert(n<=2048);emitted+=n;
 for(unsigned i=0;i<8;i++) assert(storage.left[i]==12345&&storage.right[i]==12345);
 if(channels==1&&output_channels==2) for(unsigned i=0;i<n;i++) assert(converted[2*i]==converted[2*i+1]);
 return ESP_OK;
}

'''
tail=r'''
int main(void) {
 unsigned rates[]={8000,16000,22050,24000,32000,44100,48000};
 for(unsigned a=0;a<7;a++) for(unsigned b=0;b<7;b++) for(unsigned ch=1;ch<=2;ch++) for(unsigned och=1;och<=2;och++) {
  rate=rates[a];output_rate=rates[b];channels=ch;output_channels=och;state.volume=20;
  emitted=samples=0;have_previous=false;resample_phase=rate;stream=true;
  for(unsigned i=0;i<8;i++) storage.left[i]=storage.right[i]=12345;
  for(unsigned block=0;block<50;block++) {
   unsigned n=block%2?1152:777;
   for(unsigned j=0;j<n*channels;j++) pcm[j]=(int16_t)((j*293+block*29)%65536-32768);
   assert(output(n));
  }
  uint64_t expected=1+(samples-1)*output_rate/rate;
  assert(emitted==expected); assert(state.seconds==samples/rate);
 }
 output_channels=2;channels=2;
 converted[0]=10000;converted[1]=-10000;assert(send_audio(1,25));assert(converted[0]==2500&&converted[1]==-2500);
 converted[0]=32767;converted[1]=-32768;assert(send_audio(1,0));assert(converted[0]==0&&converted[1]==0);
 puts("PASS production resampler: 196 rate/channel combinations, streaming block boundaries, exact output frame count, buffer canaries, mono duplication, software volume");
}
'''
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'test.c').write_text(head+fn+tail,encoding='utf-8');exe=d/'test.exe'
 subprocess.run(['C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe',str(d/'test.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
