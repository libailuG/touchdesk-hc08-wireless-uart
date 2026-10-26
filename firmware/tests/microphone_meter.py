from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
c=r'''
#include "mic_meter.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
int main(void) {
 int16_t p[2048]={0};mic_meter_result_t m;
 mic_meter_process(p,2048,&m);assert(m.dbfs==-96&&m.level==0&&m.clipped==0);
 for(int i=0;i<2048;i++)p[i]=10000;
 mic_meter_process(p,2048,&m);assert(m.dbfs==-96&&m.level==0);
 for(int i=0;i<2048;i++)p[i]=i%2?-16384:16384;
 mic_meter_process(p,2048,&m);assert(fabs(m.dbfs+6.0206)<.01&&m.level==89&&m.clipped==0);
 for(int i=0;i<2048;i++)p[i]=i%2?-32768:32767;
 mic_meter_process(p,2048,&m);assert(m.level>=99&&m.peak_level==100&&m.clipped==2048);
 mic_meter_process(p,0,&m);assert(m.dbfs==-96&&m.level==0&&m.peak_level==0);
 puts("PASS production microphone meter: silence, DC rejection, calibrated amplitude/dBFS, signed rails, clipping, empty block");
}
'''
with tempfile.TemporaryDirectory() as d:
 d=Path(d);(d/'test.c').write_text(c,encoding='utf-8');exe=d/'test.exe'
 subprocess.run(['C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe','-I'+str(r/'main/services'),str(d/'test.c'),str(r/'main/services/mic_meter.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
