"""Render and drive the production LVGL serial app without hardware."""
from pathlib import Path
import argparse,subprocess,tempfile
from host_regression import HEADERS,ROOT
from touch_ui import HOST as OLD_HOST,ppm_to_png

pre=OLD_HOST[:OLD_HOST.index('#include "@ROOT@/main/apps/keysim')]
helpers=OLD_HOST[OLD_HOST.index('static void read_pointer'):OLD_HOST.index('static void select_view')].replace('tick(','advance(').replace('a.x2<320','a.x2<480').replace('a.y2<480','a.y2<320').replace('P6\\n320 480','P6\\n480 320').replace('pt=(lv_point_t){x,y}','pt=(lv_point_t){y,479-x}')
helpers=helpers.replace('320 480','480 320').replace('for(int i=0;i<320*480;i++) {lv_color32_t c;c.full=lv_color_to32(frame[i]);','for(int i=0;i<480*320;i++) {int x=i%480,y=i/480;lv_color32_t c;c.full=lv_color_to32(frame[(479-x)*320+y]);')

board_source=(ROOT/'components/board/board.c').read_text(encoding='utf8')
production_flush=board_source[board_source.index('static void flush('):board_source.index('static void tick(')]
staging=r"""
static lv_color_t rotation_stage[LV_DISP_ROT_MAX_BUF/sizeof(lv_color_t)];
static lv_color_t *rotation_dma=rotation_stage;static void *panel;static unsigned staged_flushes;
static bool esp_ptr_external_ram(const void *p){uintptr_t n=(uintptr_t)p;return !((n>=(uintptr_t)draw&&n<(uintptr_t)(draw+320*40))||(n>=(uintptr_t)rotation_stage&&n<(uintptr_t)(rotation_stage+LV_DISP_ROT_MAX_BUF/sizeof(lv_color_t))));}
static int dma_panel_draw(void *unused,int x1,int y1,int x2,int y2,lv_color_t *pixels){
 assert(!esp_ptr_external_ram(pixels));assert(x1>=0&&x2<=320&&y1>=0&&y2<=480);assert(((x2-x1)*(y2-y1)*2)%4==0);
 if(pixels==rotation_stage)staged_flushes++;
 for(int y=y1;y<y2;y++)for(int x=x1;x<x2;x++)frame[y*320+x]=*pixels++;
 lv_disp_flush_ready(lv_disp_get_default()->driver);return ESP_OK;
}
#define esp_lcd_panel_draw_bitmap dma_panel_draw
#define CHECK(expr) assert((expr)==ESP_OK)
"""
helpers=helpers[:helpers.index('static void flush(')]+staging+production_flush+helpers[helpers.index('static void advance('):]

resource=OLD_HOST[OLD_HOST.index('const uint8_t *resource_store_data'):OLD_HOST.index('int main(void)')]
mocks=r'''
#include "serial_uart.h"
static serial_uart_snapshot_t simulated;
static uint8_t sent[48];
static unsigned home_requests;
static serial_history_t sim_history;
static void rotation_rounder(lv_disp_drv_t *drv,lv_area_t *area) {
 (void)drv;area->x1&=~1;area->x2|=1;
}

esp_err_t board_display_landscape(bool enabled){board_input_cancel();lv_disp_t *d=lv_disp_get_default();d->driver->sw_rotate=1;d->driver->rounder_cb=enabled?rotation_rounder:NULL;lv_disp_set_rotation(d,enabled?LV_DISP_ROT_90:LV_DISP_ROT_NONE);return ESP_OK;}
size_t serial_uart_history(uint32_t *cursor,serial_sample_t *out,size_t cap,uint32_t *lost){return serial_history_read(&sim_history,cursor,out,cap,lost);}
static void inject(serial_frame_t *f){serial_history_push_timed(&sim_history,f,(int64_t)lv_tick_get()*1000);simulated.last=*f;simulated.rx++;simulated.has_frame=true;}

void app_request(const app_desc_t *next){if(!next)home_requests++;}
bool serial_uart_active(void){return true;}
bool serial_uart_send(const serial_frame_t *f){serial_frame_encode(f,sent);simulated.tx++;return true;}
void serial_uart_snapshot(serial_uart_snapshot_t *out){*out=simulated;}
bool serial_ble_active(void){return false;}
bool serial_ble_send(const serial_frame_t *f){return false;}
void serial_ble_snapshot(serial_uart_snapshot_t *out){memset(out,0,sizeof(*out));}
size_t serial_ble_history(uint32_t *c,serial_sample_t *o,size_t n,uint32_t *l){return 0;}
const char *serial_ble_status(void){return "BLE test";}
#include "@ROOT@/main/apps/serial/app_serial.c"
'''
main=r'''
static void confirm(state_t *s){lv_event_send(s->confirm,LV_EVENT_CLICKED,NULL);advance(30);}
int main(void){
 lv_init();assert(font_ui_bind_resources());
 static lv_disp_draw_buf_t db;lv_disp_draw_buf_init(&db,draw,NULL,320*40);
 static lv_disp_drv_t drv;lv_disp_drv_init(&drv);drv.hor_res=320;drv.ver_res=480;drv.draw_buf=&db;drv.flush_cb=flush;assert(lv_disp_drv_register(&drv));
 static lv_indev_drv_t in;lv_indev_drv_init(&in);in.type=LV_INDEV_TYPE_POINTER;in.read_cb=read_pointer;assert(lv_indev_drv_register(&in));
 lv_obj_t *root=lv_obj_create(lv_scr_act());lv_obj_remove_style_all(root);lv_obj_set_size(root,320,480);lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_text_font(root,&font_ui,0);
 state_t state={0};app_ctx_t ctx={.active=true,.state=&state,.root=root,.desc=&app_serial};
 assert(create(&ctx,root)==ESP_OK);advance(120);bounds(root);snapshot("serial-send-default");
 assert(simulated.tx==0);
 for(unsigned i=0;i<10;i++){
  assert(lv_obj_get_child_cnt(state.fields[i])==1);
  assert(lv_obj_get_style_text_font(state.values[i],0)==&lv_font_montserrat_26);
  for(unsigned j=0;j<i;j++)assert(lv_obj_get_style_bg_color(state.fields[i],0).full!=lv_obj_get_style_bg_color(state.fields[j],0).full);
 }
 const char *samples[]={"4294967295","-3.40282e+38","0.0001","1e-30","-12345.6"};
 for(unsigned j=0;j<5;j++){lv_point_t measured;const lv_font_t *f=value_font(samples[j]);lv_txt_get_size(&measured,samples[j],f,0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);assert(measured.x<=100);}

 /* Independent halves: a request never changes the received half. */
 for(unsigned requested=0;requested<16;requested++)for(unsigned received=0;received<16;received++) {
  state.tx.data1=requested;simulated.last.data1=received;simulated.has_frame=true;refresh(&state);
  for(unsigned bit=0;bit<4;bit++) {
   lv_color_t left=(requested&(1u<<bit))?lv_color_hex(0x20844b):ui_surface();
   lv_color_t right=(received&(1u<<bit))?lv_color_hex(0x20844b):ui_surface();
   assert(lv_obj_get_style_bg_color(state.state_halves[bit][0],0).full==left.full);
   assert(lv_obj_get_style_bg_color(state.state_halves[bit][1],0).full==right.full);
  }
 }
 state.tx.data1=1;simulated.last.data1=0;refresh(&state);advance(120);snapshot("split-set-on-received-off");
 simulated.last.data1=1;refresh(&state);advance(120);snapshot("split-set-on-received-on");
 simulated.has_frame=false;state.tx.data1=0;refresh(&state);advance(120);snapshot("split-received-unknown");
 for(unsigned bit=0;bit<4;bit++)assert(!strcmp(lv_label_get_text(state.state_labels[bit][1]),"?"));

 for(unsigned pattern=0;pattern<16;pattern++) {
  state.tx.data1=UINT32_C(0xabcdef00)|pattern;refresh(&state);
  for(unsigned bit=0;bit<4;bit++) {
   uint32_t before=state.tx.data1;tap(124+232*(bit%2),68+50*(bit/2));
   assert(state.tx.data1==(before^(UINT32_C(1)<<bit)));
   assert((state.tx.data1&UINT32_C(0xfffffff0))==UINT32_C(0xabcdef00));
   assert(simulated.tx==pattern*4+bit+1);
   serial_frame_t wire;assert(serial_frame_decode(sent,&wire)&&wire.data1==state.tx.data1);
  }
 }
 simulated.tx=0;state.tx.data1=0;refresh(&state);
 pointer(124,68,true);advance(1200);assert(simulated.tx==0);
 pointer(124,68,false);assert(simulated.tx==1&&state.tx.data1==1);
 pointer(124,68,true);advance(1200);assert(simulated.tx==1);
 pointer(124,68,false);assert(simulated.tx==2&&state.tx.data1==0);
 pointer(124,68,true);pointer(478,240,true);pointer(478,240,false);assert(simulated.tx==2);
 simulated.tx=0;state.tx.data1=0;refresh(&state);
 tap(322,20);assert(state.receive);
 for(unsigned bit=0;bit<4;bit++)assert(strchr(lv_label_get_text(state.state_received[bit]),'?'));
 snapshot("serial-receive-unknown");tap(224,20);
 tap(120,280);assert(simulated.tx==1);
 pointer(120,280,true);advance(1200);assert(simulated.tx==1);pointer(120,280,false);assert(simulated.tx==2);
 pointer(120,280,true);pointer(478,240,true);pointer(478,240,false);assert(simulated.tx==2);
 tap(60,164);assert(state.editor&&state.editing==0);lv_textarea_set_text(state.input,"4294967296");confirm(&state);assert(state.editor&&state.tx.data1==0);
 lv_textarea_set_text(state.input,"4294967295");confirm(&state);assert(!state.editor&&state.tx.data1==UINT32_MAX);bounds(root);
 tap(292,164);assert(state.editor&&state.editing==2);lv_textarea_set_text(state.input,"-12.5");confirm(&state);assert(!state.editor);
 tap(176,240);assert(state.editor&&state.editing==9);lv_textarea_set_text(state.input,"3.25e1");snapshot("serial-editor");confirm(&state);
 tap(120,280);assert(simulated.tx==3);serial_frame_t decoded;assert(serial_frame_decode(sent,&decoded));assert(decoded.data1==UINT32_MAX&&decoded.data3==-12.5f&&decoded.data10==32.5f);
 snapshot("serial-send-edited");
 serial_frame_init(&simulated.last);simulated.last.data1=0xaabbccdd;simulated.last.data2=123;simulated.last.data3=1.5;simulated.last.data10=-3.25;simulated.rx=12;simulated.has_frame=true;
 tap(322,20);assert(state.receive&&!strcmp(lv_label_get_text(state.values[0]),"2864434397"));assert(!strcmp(lv_label_get_text(state.values[2]),"1.5"));
 tap(60,164);assert(!state.editor);assert(lv_obj_has_flag(state.send,LV_OBJ_FLAG_HIDDEN));bounds(root);snapshot("serial-receive");
 for(unsigned pattern=0;pattern<16;pattern++) {
  simulated.last.data1=UINT32_C(0xffff0000)|pattern;refresh(&state);
  for(unsigned bit=0;bit<4;bit++) {
   char expected[20];snprintf(expected,sizeof(expected),"%u %s",bit+1,(pattern&(1u<<bit))?"开":"关");
   assert(!strcmp(lv_label_get_text(state.state_received[bit]),expected));
   assert(lv_obj_has_flag(state.state_halves[bit][0],LV_OBJ_FLAG_HIDDEN));
   assert(lv_obj_has_flag(state.state_halves[bit][1],LV_OBJ_FLAG_HIDDEN));
   assert(!lv_obj_has_flag(state.state_received[bit],LV_OBJ_FLAG_HIDDEN));
   assert(lv_obj_get_style_bg_color(state.states[bit],0).full==((pattern&(1u<<bit))?lv_color_hex(0x20844b):ui_surface()).full);
   assert(!lv_obj_has_flag(state.states[bit],LV_OBJ_FLAG_CLICKABLE));
   lv_event_send(state.states[bit],LV_EVENT_CLICKED,NULL);assert(state.tx.data1==UINT32_MAX);
  }
 }
 tap(224,20);assert(!state.receive&&state.tx.data1==UINT32_MAX);assert(!strcmp(lv_label_get_text(state.values[2]),"-12.5"));
 lv_event_send(state.tabs[2],LV_EVENT_CLICKED,NULL);assert(state.plotting);serial_frame_t wave;serial_frame_init(&wave);
 simulated.rx=0;memset(&sim_history,0,sizeof(sim_history));state.cursor=0;
 for(unsigned n=0;n<1000;n++){wave.data3=(float)sin(n*.015707963);wave.data4=2*wave.data3;inject(&wave);advance(5);}
 advance(120);
 assert(state.history->count==1000&&state.cursor==1000&&state.lost==0&&staged_flushes>0);assert(lv_disp_get_hor_res(NULL)==480&&lv_disp_get_ver_res(NULL)==320);bounds(root);snapshot("serial-curve-live");
 unsigned windows[]={250,1000,2000,5000,10000};
 for(unsigned i=0;i<5;i++){lv_dropdown_set_selected(state.timescale,i);lv_event_send(state.timescale,LV_EVENT_VALUE_CHANGED,NULL);assert(state.window_ms==windows[i]);assert(state.view.count==1000);advance(30);bounds(root);}
 lv_dropdown_set_selected(state.timescale,3);lv_event_send(state.timescale,LV_EVENT_VALUE_CHANGED,NULL);
 assert(state.window_ms==5000&&state.history->count==1000);

 tap(412,213);assert(state.paused);char frozen[100];snprintf(frozen,sizeof(frozen),"%s",lv_label_get_text(state.latest));
 wave.data3=42;inject(&wave);advance(120);assert(state.cursor==1001&&!strcmp(frozen,lv_label_get_text(state.latest)));
 tap(412,213);assert(!state.paused&&!strcmp(lv_label_get_text(state.latest),"42"));
 lv_dropdown_set_selected(state.selector,1);lv_event_send(state.selector,LV_EVENT_VALUE_CHANGED,NULL);assert(state.channel==1);
 wave.data4=NAN;inject(&wave);advance(120);assert(!isfinite(serial_plot_at(state.history,state.history->count-1)->values[1]));assert(state.series->y_points[state.view.count-1]==LV_CHART_POINT_NONE);
 unsigned tx_before=simulated.tx;tap(412,262);assert(state.history->count==0&&simulated.rx==1002&&simulated.tx==tx_before);snapshot("serial-curve-empty");
 for(unsigned n=0;n<1000;n++)inject(&wave);advance(120);assert(state.lost==488&&state.history->count==512);
 lv_event_send(state.tabs[0],LV_EVENT_CLICKED,NULL);assert(!state.plotting&&state.tx.data1==UINT32_MAX);
 simulated.rx=0;memset(&sim_history,0,sizeof(sim_history));state.cursor=0;
 lv_mem_monitor_t before,after;lv_mem_monitor(&before);
 for(unsigned n=0;n<300;n++){
  if(ctx.timers[0]){lv_timer_del(ctx.timers[0]);ctx.timers[0]=NULL;}
  destroy(&ctx);assert(lv_disp_get_rotation(NULL)==LV_DISP_ROT_NONE);lv_obj_clean(root);memset(&state,0,sizeof(state));assert(create(&ctx,root)==ESP_OK);
  lv_event_send(state.fields[n%10],LV_EVENT_CLICKED,NULL);assert(state.editor);close_editor(&state);
  lv_event_send(state.tabs[1],LV_EVENT_CLICKED,NULL);lv_event_send(state.tabs[0],LV_EVENT_CLICKED,NULL);
 }
 advance(100);lv_mem_monitor(&after);assert(after.free_size>=before.free_size);assert(lv_mem_test()==LV_RES_OK);assert(simulated.tx==3);bounds(root);
 /* The numeric range is the plotted data extrema, excluding 10% axis padding. */
 state.history->count=state.history->head=0;state.cursor=simulated.rx;
 serial_frame_t limits;serial_frame_init(&limits);limits.data3=-2.5f;inject(&limits);advance(50);
 limits.data3=7.25f;inject(&limits);advance(120);lv_event_send(state.tabs[2],LV_EVENT_CLICKED,NULL);advance(120);
 assert(!strcmp(lv_label_get_text(state.range),"Y -2.5 ~ 7.25"));
 assert(state.view.minimum==-2.5f&&state.view.maximum==7.25f);
 snapshot("curve-actual-min-max");
 state.history->count=state.history->head=0;state.cursor=simulated.rx;
 limits.data3=3.125f;inject(&limits);advance(120);draw_plot(&state);
 assert(!strcmp(lv_label_get_text(state.range),"Y 3.125 ~ 3.125"));
 state.history->count=state.history->head=0;state.cursor=simulated.rx;
 limits.data3=NAN;inject(&limits);advance(120);draw_plot(&state);assert(!strcmp(lv_label_get_text(state.range),"Y --"));
 tap(22,22);assert(home_requests==1);if(ctx.timers[0])lv_timer_del(ctx.timers[0]);destroy(&ctx);assert(allocations==0);
 /* Exercise the actual '<' callback, then the portrait shell's page geometry. */
 lv_obj_clean(root);lv_obj_set_pos(root,0,36);lv_obj_set_size(root,320,444);ui_reset_page(root);
 ui_button(root,"desktop",12,120,296,44,NULL,NULL);advance(180);lv_refr_now(NULL);
 lv_area_t returned;lv_obj_get_coords(root,&returned);
 assert(lv_disp_get_rotation(NULL)==LV_DISP_ROT_NONE);
 assert(returned.x1==0&&returned.y1==36&&returned.x2==319&&returned.y2==479);
 assert(frame[479*320].full==ui_background().full);
 printf("PASS production DMA staging/aligned native rectangles + landscape software rotation/touch, 1000 wave frames, 5 time windows, 1000 uniform-time points, bounded history/overrun, pause/resume/clear/NaN/channel; all 16 status patterns, upper 28 bits preserved, unknown/read-only RX, one TX per toggle; real LVGL 320x480, touch send once/hold/slide, integer bounds, float editing, RX read-only, TX persistence, 300 modal/tab/app cycles, heap %u -> %u\n",before.free_size,after.free_size);return 0;
}
'''
def run():
 ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);args=ap.parse_args();args.output.mkdir(parents=True,exist_ok=True)
 with tempfile.TemporaryDirectory(prefix='serial-ui-') as temp:
  temp=Path(temp);headers={k:v for k,v in HEADERS.items() if k not in ('lvgl.h','board.h','shell.h')}
  headers['stdatomic.h']+='\nstatic bool test_exchange(bool *p,bool v){bool old=*p;*p=v;return old;}\n#define atomic_exchange(p,v) test_exchange(p,v)\n'
  headers['esp_timer.h']='#include "lvgl.h"\nstatic long long esp_timer_get_time(void){return (long long)lv_tick_get()*1000;}\n'
  headers['board.h']='esp_err_t board_display_landscape(bool enabled);\nvoid board_input_cancel(void);\nunsigned board_input_fault_epoch(void);'
  headers['shell.h']='#pragma once\nstatic void shell_notice(const char *s){}\n'
  headers['resource_manifest.h']='#define RESOURCE_FONT_UI__BITMAP_OFFSET 0\n#define RESOURCE_FONT_UI__BITMAP_SIZE 593829\n'
  headers['float_compat.h']='#include <stdlib.h>\nstatic float host_strtof(const char *s,char **end){return (float)strtod(s,end);}\n#define strtof host_strtof\n'
  for n,t in headers.items():
   p=temp/n;p.parent.mkdir(parents=True,exist_ok=True);guard='SERIAL_UI_'+n.upper().replace('/','_').replace('.','_');p.write_text(f'#ifndef {guard}\n#define {guard}\n{t}\n#endif\n',encoding='utf-8')
  source=temp/'sim.c';source.write_text((pre+mocks+helpers+resource+main).replace('@ROOT@',ROOT.as_posix()),encoding='utf-8')
  sources=[source,*list((ROOT/'components/lvgl/src').rglob('*.c')),ROOT/'main/shell/ui.c',ROOT/'main/shell/font_ui.c',ROOT/'main/services/serial_frame.c',ROOT/'main/services/serial_history.c',ROOT/'main/services/serial_plot.c']
  cc='C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe'
  cmd=[cc,'-DLV_CONF_SKIP','-DLV_MEM_SIZE=2097152','-DLV_FONT_MONTSERRAT_20=1','-DLV_FONT_MONTSERRAT_18=1','-DLV_FONT_MONTSERRAT_26=1','-include',str(temp/'float_compat.h')]+['-I'+str(p) for p in [temp,ROOT/'components/lvgl',ROOT/'main/platform',ROOT/'main/shell',ROOT/'main/services',ROOT/'main/apps/weather',ROOT/'main/apps/serial']]
  objects=[]
  for i,p in enumerate(sources):
   obj=temp/f'{i}.o';subprocess.run([*cmd,'-c',str(p),'-o',str(obj)],check=True,stdout=subprocess.DEVNULL);objects.append(obj)
  exe=temp/'sim.exe';subprocess.run([cc,*map(str,objects),'-o',str(exe)],check=True);subprocess.run([str(exe)],cwd=args.output,check=True)
 for p in args.output.glob('*.ppm'):ppm_to_png(p);p.unlink()
if __name__=='__main__':run()
