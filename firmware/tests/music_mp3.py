from pathlib import Path
import tempfile,subprocess
root=Path(__file__).resolve().parents[1]
source=r'''
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
 /* MPEG-1 Layer III 128kbit/s, 44100Hz, stereo; zero-length granules encode silence. */
 unsigned char stream[417*8]={0};
 for(int n=0;n<8;n++) { unsigned char *h=stream+n*417;h[0]=0xff;h[1]=0xfb;h[2]=0x90;h[3]=0; }
 mp3dec_t dec;mp3dec_init(&dec);mp3d_sample_t pcm[MINIMP3_MAX_SAMPLES_PER_FRAME];unsigned pos=0,total=0;
 for(int n=0;n<8;n++) {
  mp3dec_frame_info_t info;int frames=mp3dec_decode_frame(&dec,stream+pos,sizeof(stream)-pos,pcm,&info);
  assert(frames==1152&&info.channels==2&&info.hz==44100&&info.frame_bytes==417);
  for(int i=0;i<frames*2;i++)assert(pcm[i]==0);
  pos+=info.frame_bytes;total+=frames;
 }
 assert(total==9216);puts("PASS actual minimp3: eight valid MPEG-1 Layer III frames, 44100Hz stereo, exact PCM sample count and silence output");
}
'''
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'test.c').write_text(source,encoding='utf-8');exe=d/'test.exe';subprocess.run(['C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe','-I'+str(root/'components/music_codec'),str(d/'test.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
