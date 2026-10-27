"""Compile production touch admission and HID flow queues; no radio/PC input."""
from pathlib import Path
import subprocess,tempfile
from host_regression import ROOT,HEADERS

source=(ROOT/'main/services/ble_mouse.c').read_text(encoding='utf-8')
admission=source[source.index('bool ble_touch_ready(void)'):]
test=r'''
#include <assert.h>
#include <stdio.h>
#include "freertos/semphr.h"
#define portENTER_CRITICAL(p) ((void)0)
#define portEXIT_CRITICAL(p) ((void)0)
#include "@ROOT@/main/services/mouse_flow.c"
#include "@ROOT@/main/services/keyboard_flow.c"
static int mux;
static SemaphoreHandle_t touch_lock;
static mouse_flow_t flow;
static keyboard_flow_t keys,media;
@ADMISSION@
static void drain(void) {
 mouse_packet_t m;keyboard_packet_t k;
 while(mouse_flow_peek(&flow,&m))mouse_flow_commit(&flow,&m);
 while(keyboard_flow_peek(&keys,&k))keyboard_flow_commit(&keys,&k);
 while(keyboard_flow_peek(&media,&k))keyboard_flow_commit(&media,&k);
}
int main(void) {
 touch_lock=xSemaphoreCreateMutex();
 uint8_t key[8]={2,0,4};int16_t mouse[4]={1,127,-127,1};uint16_t volume=0xe9;
 assert(!ble_touch_ready()&&!ble_touch_report(2,key));
 mouse_flow_ready(&flow,true);keyboard_flow_ready(&keys,true);keyboard_flow_ready(&media,true);
 assert(ble_touch_ready()&&!ble_touch_report(2,key)); // initial neutral barrier
 drain();assert(ble_touch_report(2,key)&&ble_touch_report(1,mouse)&&ble_touch_report(3,&volume));
 assert(keys.count==1&&flow.count==1&&media.count==1);
 keyboard_packet_t p;assert(keyboard_flow_peek(&keys,&p)&&p.data[0]==2&&p.data[2]==4);
 ble_touch_cancel();assert(keys.count==0&&flow.count==0&&media.count==0);
 assert(!ble_touch_report(2,key));drain();assert(ble_touch_report(2,key));
 drain();key[2]=0;key[0]=0;assert(ble_touch_report(2,key));drain();
 for(unsigned i=0;i<KEYBOARD_FLOW_CAP;i++){key[2]=(i%2)?4:5;assert(ble_touch_report(2,key));}
 assert(!ble_touch_report(2,key));ble_touch_cancel();drain();
 assert(keys.armed&&media.armed&&flow.armed);
 mouse_flow_ready(&flow,false);keyboard_flow_ready(&keys,false);keyboard_flow_ready(&media,false);
 assert(!ble_touch_ready()&&!ble_touch_report(1,mouse));
 mouse_flow_ready(&flow,true);keyboard_flow_ready(&keys,true);keyboard_flow_ready(&media,true);
 ble_touch_cancel();drain();assert(keys.count==0&&flow.count==0&&media.count==0);
 puts("PASS production BLE admission/flows: initial neutral barrier, keyboard/mouse/consumer, cancel flush, queue backpressure, disconnect/reconnect");
}
'''
with tempfile.TemporaryDirectory() as d:
 d=Path(d)
 for n in ('freertos/FreeRTOS.h','freertos/semphr.h'):
  p=d/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(HEADERS[n],encoding='utf-8')
 p=d/'test.c';p.write_text(test.replace('@ADMISSION@',admission).replace('@ROOT@',ROOT.as_posix()),encoding='utf-8')
 exe=d/'test.exe'
 subprocess.run(['C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe','-I'+str(d),'-I'+str(ROOT/'main/services'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
