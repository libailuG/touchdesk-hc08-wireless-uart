"""Real LVGL rendering and pointer event verification. USB is a stub, never PC input."""
from pathlib import Path
import argparse, subprocess, tempfile
from host_regression import HEADERS, ROOT
from ui_simulator import EXTRA_HEADERS

HOST=r'''
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "lvgl.h"
#include "app.h"
#include "ui.h"
#include "resource_store.h"
#include "weather_style.h"
static const weather_palette_t palette={0xedf3f9,0x173a5b,0x526b83,0xcedce9,0xe0eaf4,0x2569b7};
const weather_palette_t *weather_palette(unsigned t) { return &palette; }
static bool online=true,down,blocked;
static unsigned session=1,clicks,changes,mouse_changes,mouse_clicks;
static int volume_delta;static unsigned mute_clicks;
static unsigned fault_epoch;
unsigned board_input_fault_epoch(void) {return fault_epoch;}
static uint8_t key,mods,buttons;
static int dx,dy,wheel;
static lv_point_t pt;
static lv_color_t frame[320*480],draw[320*40];
void board_input_cancel(void) { blocked=down; lv_indev_reset(NULL,NULL); }
unsigned usb_service_session(void) { return session; }
bool usb_service_online(void) { return online; }
void usb_service_cancel(void) { session++; key=mods=buttons=0; }
bool usb_service_key(unsigned o,unsigned g,uint8_t m,uint8_t k) {
    if(!online||g!=session) return false; changes++; mods=m; key=k; return true;
}
bool usb_service_mouse(unsigned o,unsigned g,uint8_t b,int x,int y,int w) {
    if(!online||g!=session) return false; mouse_changes++; buttons=b; dx+=x;dy+=y;wheel+=w;return true;
}
bool usb_service_mouse_click(unsigned o,unsigned g) { if(!online||g!=session)return false;mouse_clicks++;return true; }
bool usb_service_volume(unsigned o,unsigned g,int n){if(!online||g!=session)return false;volume_delta+=n;return true;}
bool usb_service_mute(unsigned o,unsigned g){if(!online||g!=session)return false;mute_clicks++;return true;}
bool usb_service_click(unsigned o,unsigned g,uint8_t m,uint8_t k) {
    if(g!=session||!online)return false;clicks++;return true;
}
static bool transport_ble;static unsigned pairs,forgets;
bool usb_service_is_ble(void){return transport_ble;}
unsigned usb_service_set_owner(bool on){session++;return 1;}
esp_err_t usb_service_transport(bool ble){transport_ble=ble;session++;return ESP_OK;}
const char *usb_service_connection_text(void){return transport_ble?"BLE: pairing (120s)":"USB: connected";}
void usb_service_pair(bool forget){if(forget)forgets++;else pairs++;}
lv_timer_t *app_timer_create(app_ctx_t *ctx,lv_timer_cb_t cb,uint32_t ms) {
    return ctx->timers[0]=lv_timer_create(cb,ms,ctx);
}
#include "@ROOT@/main/apps/keysim/app_keysim.c"
static void read_pointer(lv_indev_drv_t *drv,lv_indev_data_t *d) {
    if(!down)blocked=false;
    d->point=pt; d->state=down&&!blocked?LV_INDEV_STATE_PR:LV_INDEV_STATE_REL;
}
static void flush(lv_disp_drv_t *drv,const lv_area_t *a,lv_color_t *p) {
    for(int y=a->y1;y<=a->y2;y++)for(int x=a->x1;x<=a->x2;x++)frame[y*320+x]=*p++;
    lv_disp_flush_ready(drv);
}
static void tick(unsigned ms) { for(unsigned i=0;i<ms/5;i++){lv_tick_inc(5);lv_timer_handler();} }
static void bounds(lv_obj_t *o) {
    lv_area_t a,p;lv_obj_get_coords(o,&a);
    assert(a.x1>=0&&a.x2<320&&a.y1>=0&&a.y2<480);
    if(lv_obj_check_type(o,&lv_label_class)) {
        lv_obj_get_coords(lv_obj_get_parent(o),&p);
        if(!(a.x1>=p.x1&&a.x2<=p.x2&&a.y1>=p.y1&&a.y2<=p.y2)) {
            fprintf(stderr,"clipped: %s: %d %d %d %d parent %d %d %d %d\n",lv_label_get_text(o),a.x1,a.y1,a.x2,a.y2,p.x1,p.y1,p.x2,p.y2);abort();
        }
    }
    for(unsigned i=0;i<lv_obj_get_child_cnt(o);i++)bounds(lv_obj_get_child(o,i));
}
static void snapshot(const char *name) {
    tick(80); lv_refr_now(NULL); char path[100];snprintf(path,sizeof(path),"%s.ppm",name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n320 480\n255\n");
    for(int i=0;i<320*480;i++) {lv_color32_t c;c.full=lv_color_to32(frame[i]);fputc(c.ch.red,f);fputc(c.ch.green,f);fputc(c.ch.blue,f);}fclose(f);
}
static void pointer(int x,int y,bool pressed) {pt=(lv_point_t){x,y};down=pressed;tick(45);}
static void tap(int x,int y) {pointer(x,y,true);pointer(x,y,false);tick(35);}
static void select_view(app_ctx_t *ctx,unsigned v) {
    state_t *s=ctx->state;cancel(ctx);s->view=v;render(ctx);tick(60);bounds(s->body);
}
const uint8_t *resource_store_data(size_t offset,size_t len) {
    static uint8_t data[700000]; static bool loaded;
    if(!loaded){FILE*f=fopen("@ROOT@/assets/resource-data/font_ui__bitmap.bin","rb");assert(f);fread(data,1,sizeof(data),f);fclose(f);loaded=true;}
    return data;
}
int main(void) {
    lv_init();assert(font_ui_bind_resources());
    static lv_disp_draw_buf_t db;lv_disp_draw_buf_init(&db,draw,NULL,320*40);
    static lv_disp_drv_t drv;lv_disp_drv_init(&drv);drv.hor_res=320;drv.ver_res=480;drv.draw_buf=&db;drv.flush_cb=flush;assert(lv_disp_drv_register(&drv));
    static lv_indev_drv_t in;lv_indev_drv_init(&in);in.type=LV_INDEV_TYPE_POINTER;in.read_cb=read_pointer;assert(lv_indev_drv_register(&in));
    lv_obj_t *screen=lv_scr_act();lv_obj_set_style_text_font(screen,&font_ui,0);lv_obj_set_style_bg_color(screen,ui_background(),0);lv_obj_set_style_text_color(screen,ui_foreground(),0);
    ui_label(screen,"快捷键",12,8,170);ui_label(screen,"HID",266,8,50);
    ui_button(screen,"返回",10,430,140,42,NULL,NULL);ui_button(screen,"主页",170,430,140,42,NULL,NULL);
    lv_obj_t *root=lv_obj_create(screen);lv_obj_remove_style_all(root);lv_obj_set_pos(root,0,62);lv_obj_set_size(root,320,360);lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);
    state_t state={0};app_ctx_t ctx={.active=true,.state=&state,.hid_owner=1,.root=root};
    assert(create(&ctx,root)==ESP_OK);tick(80);bounds(state.body);snapshot("touch-shortcuts");
    tap(55,320);assert(volume_delta==-1);tap(265,320);assert(volume_delta==0);tap(155,320);assert(mute_clicks==1);
    tap(60,115);assert(clicks==1);
    pointer(60,115,true);fault_epoch++;pointer(60,115,false);assert(clicks==1);
    select_view(&ctx,1);snapshot("touch-keyboard");
    tap(40,106);assert(state.mods==SHIFT);
    pointer(16,147,true);assert(key==20 && mods==SHIFT);
    tick(1000);assert(key==20);pointer(16,147,false);assert(key==0&&state.mods==0);
    pointer(16,147,true);assert(key==20);pointer(310,360,true);assert(key==0);pointer(310,360,false);
    pointer(16,147,true);assert(key==20);fault_epoch++;pointer(16,147,false);assert(key==0);
    for(unsigned i=0;i<3;i++){state.keypage=i;cancel(&ctx);render(&ctx);tick(60);bounds(state.body);}
    state.keypage=1;render(&ctx);snapshot("touch-symbols");state.keypage=2;render(&ctx);snapshot("touch-functions");
    select_view(&ctx,2);snapshot("touch-mouse");
    tap(100,360);assert(state.dragging&&buttons==1);
    pointer(80,150,true);pointer(100,165,true);pointer(100,165,false);assert(buttons==1&&dx==40&&dy==30);
    tap(100,360);assert(!state.dragging&&buttons==0);
    pointer(290,160,true);pointer(290,190,true);pointer(290,190,false);assert(wheel<0);
    pointer(50,310,true);assert(buttons==1);pointer(50,310,false);assert(buttons==0);
    pointer(240,310,true);assert(buttons==2);pointer(240,310,false);assert(buttons==0);
    unsigned old_clicks=mouse_clicks;pointer(80,150,true);pointer(80,150,false);assert(mouse_clicks==old_clicks+1&&buttons==0);
    tap(100,360);assert(buttons==1);tap(110,400);assert(state.view==1&&buttons==0);
    select_view(&ctx,2);pointer(80,150,true);select_view(&ctx,1);pointer(30,147,true);assert(key==0);pointer(30,147,false);
    state.keypage=0;select_view(&ctx,1);pointer(16,147,true);assert(key==20);
    online=false;session++;tick(40);assert(key==20); // hardware service separately clears reports; UI must not replay
    key=mods=buttons=0;online=true;session++;tick(40);pointer(60,147,true);assert(key==0);pointer(60,147,false);
    snapshot("touch-reconnected");
    lv_mem_monitor_t before,after;select_view(&ctx,0);lv_mem_monitor(&before);
    for(unsigned i=0;i<300;i++)select_view(&ctx,i%3);
    select_view(&ctx,0);lv_mem_monitor(&after);assert(after.free_size>=before.free_size);assert(lv_mem_test()==LV_RES_OK);
    select_view(&ctx,3);snapshot("touch-connection-usb");
    tap(155,174);assert(transport_ble);tick(80);bounds(state.body);snapshot("touch-connection-ble");
    tap(155,230);assert(pairs==1);tap(155,280);assert(forgets==1);
    tap(155,120);assert(!transport_ble);
    select_view(&ctx,0);tap(280,400);assert(state.view==3);
    destroy(&ctx);assert(key==0&&buttons==0);
    printf("PASS real LVGL: layout/labels, shortcuts, one-shot modifier, hold/release, slide cancellation, mouse buttons/move/drag/wheel/tap, page/reconnect held-touch isolation, 300 page switches heap %u -> %u\n",before.free_size,after.free_size);
    return 0;
}
'''
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--output',type=Path,required=True);a=ap.parse_args();a.output.mkdir(parents=True,exist_ok=True)
    cc='C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe'
    with tempfile.TemporaryDirectory(prefix='touch-ui-') as d:
        d=Path(d);headers={k:v for k,v in HEADERS.items() if k not in ('lvgl.h','board.h','shell.h')}
        headers['class/hid/hid_device.h']=EXTRA_HEADERS['class/hid/hid_device.h']
        headers['board.h']='void board_input_cancel(void);\nunsigned board_input_fault_epoch(void);'
        headers['shell.h']='#pragma once\nstatic void shell_notice(const char *s) {}\n'
        headers['resource_manifest.h']='#define RESOURCE_FONT_UI__BITMAP_OFFSET 0\n#define RESOURCE_FONT_UI__BITMAP_SIZE 593829\n'
        for n,t in headers.items():
            p=d/n;p.parent.mkdir(parents=True,exist_ok=True)
            guard='TOUCH_UI_'+n.upper().replace('/','_').replace('.','_')
            p.write_text(f'#ifndef {guard}\n#define {guard}\n{t}\n#endif\n',encoding='utf-8')
        p=d/'sim.c';p.write_text(HOST.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8')
        sources=[p,*list((ROOT/'components/lvgl/src').rglob('*.c')),ROOT/'main/shell/ui.c',ROOT/'main/shell/font_ui.c']
        cmd=[cc,'-DLV_CONF_SKIP','-DLV_MEM_SIZE=2097152']+['-I'+str(p) for p in [d,ROOT/'components/lvgl',ROOT/'main/platform',ROOT/'main/shell',ROOT/'main/services',ROOT/'main/apps/weather']]
        objs=[]
        for i,p in enumerate(sources):
            o=d/f'{i}.o';subprocess.run([*cmd,'-c',str(p),'-o',str(o)],check=True,stdout=subprocess.DEVNULL);objs.append(o)
        exe=d/'sim.exe';subprocess.run([cc,*map(str,objs),'-o',str(exe)],check=True);subprocess.run([str(exe)],cwd=a.output,check=True)
    for p in a.output.glob('*.ppm'): ppm_to_png(p);p.unlink()

def ppm_to_png(p):
    import struct,zlib
    header,pixels=p.read_bytes().split(b'\n255\n',1)
    w,h=map(int,header.splitlines()[1].split())
    def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
    raw=b''.join(b'\0'+pixels[y*w*3:(y+1)*w*3] for y in range(h))
    p.with_suffix('.png').write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(raw))+chunk(b'IEND',b''))
if __name__=='__main__':main()
