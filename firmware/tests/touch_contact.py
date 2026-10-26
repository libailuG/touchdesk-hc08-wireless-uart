"""Run the production GT1158 sample callback with mock contact frames and I2C failures."""
from pathlib import Path
import subprocess,tempfile
ROOT=Path(__file__).resolve().parents[1]
board=(ROOT/'components/board/board.c').read_text(encoding='utf-8')
sample=board[board.index('static void touch_sample('):board.index('static bool transfer_done(')]
prefix=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "input_guard.h"
#define ESP_OK 0
#define LV_INDEV_STATE_PR 1
#define LV_INDEV_STATE_REL 0
typedef int esp_err_t;
typedef int lv_indev_drv_t;
typedef struct {int x,y;} lv_point_t;
typedef struct {lv_point_t point;int state;} lv_indev_data_t;
static int64_t now,last_touch_frame,last_activity;
static bool pressed,touch_needs_release,backlight_on=true;
static unsigned touch_fault_epoch;
static lv_point_t point;
static input_guard_t input_guard;
static unsigned contacts,x=100,y=100;
static bool frame=true,fail_status,fail_xy,fail_ack;
static int64_t esp_timer_get_time(void){return now;}
static esp_err_t touch_read(uint16_t reg,uint8_t *out,size_t len) {
 if(reg==0x814e) {*out=frame?(0x80|contacts):0;return fail_status?1:0;}
 out[0]=x&255;out[1]=x>>8;out[2]=y&255;out[3]=y>>8;return fail_xy?1:0;
}
static esp_err_t touch_write(uint16_t reg,uint8_t v){return fail_ack?1:0;}
'''
suffix=r'''
static void check(unsigned count,bool expected){contacts=count;lv_indev_data_t d;touch_sample(NULL,&d);assert(d.state==expected);}
int main(void){
 check(0,false);frame=false;now=2000000;check(0,false);assert(!touch_needs_release&&touch_fault_epoch==0);
 frame=true;check(1,true);check(0,false);
 for(int i=0;i<10;i++){frame=false;now+=2000000;check(0,false);assert(!touch_needs_release);frame=true;check(1,true);check(0,false);}
 check(0,false);check(1,true);check(2,false);assert(touch_fault_epoch==1);check(1,false);check(0,false);check(1,true);
 x=400;check(1,false);x=100;check(1,false);check(0,false);check(1,true);
 fail_xy=true;check(1,false);fail_xy=false;check(1,false);check(0,false);check(1,true);
 fail_status=true;check(1,false);fail_status=false;check(1,false);check(0,false);check(1,true);
 fail_ack=true;check(1,false);fail_ack=false;check(1,false);check(0,false);check(1,true);
 frame=false;now+=150001;check(1,false);frame=true;check(1,false);check(0,false);check(1,true);
 input_guard_cancel(&input_guard);check(1,false);check(0,false);check(1,true);
 puts("PASS production touch sampling: first tap after idle (10 cycles), multi-contact, bounds, I2C status/coordinate/ack failure, stale active contact, all-up rearm, page gate");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='contact-test-') as d:
 d=Path(d);p=d/'test.c';p.write_text(prefix+sample+suffix,encoding='utf-8');exe=d/'test.exe'
 subprocess.run(['C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe','-I'+str(ROOT/'components/board'),str(p),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
