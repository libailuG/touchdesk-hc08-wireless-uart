from pathlib import Path
import re,subprocess,tempfile
root=Path('outputs/TouchDeskWireless');src=(root/'main/apps/serial/app_serial.c').read_text(encoding='utf-8');callbacks=src[src.index('static void continuous_tick('):src.index('static void toggle_state(')]
harness=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#define LV_STATE_CHECKED 1
#define LV_EVENT_VALUE_CHANGED 2
typedef struct {void *user_data;bool paused;} lv_timer_t;
typedef struct {bool checked;} object_t;
typedef struct {bool receive,plotting;void *editor;object_t *continuous_switch,*result;lv_timer_t *send_timer;int tx;} state_t;
typedef struct {bool active;state_t *state;} app_ctx_t;
typedef struct {int code;void *user_data;} lv_event_t;
static unsigned attempts,errors,resets;static bool accepted=true;
static bool lv_obj_has_state(object_t *o,int mask){return o->checked;}
static bool serial_transport_send(int *tx){attempts++;return accepted;}
static void lv_label_set_text(object_t *o,const char *t){errors++;}
static int lv_event_get_code(lv_event_t *e){return e->code;}
static void *lv_event_get_user_data(lv_event_t *e){return e->user_data;}
static void lv_timer_reset(lv_timer_t *t){resets++;}
static void lv_timer_resume(lv_timer_t *t){t->paused=false;}
static void lv_timer_pause(lv_timer_t *t){t->paused=true;}
static void lv_obj_clear_state(object_t *o,int mask){o->checked=false;}
'''
tests=r'''
int main(void){
 object_t sw={0},result={0};state_t s={.continuous_switch=&sw,.result=&result};app_ctx_t ctx={true,&s};lv_timer_t timer={&ctx,true};s.send_timer=&timer;lv_event_t e={LV_EVENT_VALUE_CHANGED,&ctx};
 continuous_tick(&timer);assert(attempts==0);
 sw.checked=true;continuous_changed(&e);assert(!timer.paused&&resets==1);
 for(unsigned i=0;i<1000;i++)continuous_tick(&timer);assert(attempts==1000);
 accepted=false;continuous_tick(&timer);assert(attempts==1001&&errors==1);
 s.editor=&sw;continuous_tick(&timer);s.editor=NULL;assert(attempts==1001);
 s.receive=true;continuous_tick(&timer);continuous_changed(&e);assert(!timer.paused&&sw.checked&&attempts==1002);s.receive=false;
 sw.checked=true;s.plotting=true;continuous_tick(&timer);continuous_changed(&e);assert(!timer.paused&&sw.checked&&attempts==1003);s.plotting=false;
 sw.checked=true;ctx.active=false;continuous_tick(&timer);continuous_changed(&e);assert(timer.paused&&!sw.checked&&attempts==1003);
 puts("PASS production continuous callbacks: disabled default, one attempt per tick, backpressure, editor pause, inactive guard and continued send in receive/curve pages");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(harness+callbacks+tests,encoding='utf-8');cc='C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe';subprocess.run([cc,str(p/'test.c'),'-o',str(p/'test.exe')],check=True);subprocess.run([str(p/'test.exe')],check=True)
