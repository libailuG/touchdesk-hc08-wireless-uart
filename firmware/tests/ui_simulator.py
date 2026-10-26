"""Render the real LVGL 8 UI off-screen and stress real object lifecycles.
Hardware and services are stubs; no USB or serial access. Requires a host C compiler.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile
from host_regression import HEADERS, ROOT

EXTRA_HEADERS = {
'board.h': '''#ifndef SIM_BOARD_H
#define SIM_BOARD_H
#include "lvgl.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
static void board_input_cancel(void) { lv_indev_reset(NULL,NULL); }
extern bool sim_touch,sim_backlight;
extern int64_t sim_idle;
static bool board_touch_down(void) { return sim_touch; }
static int64_t board_idle_us(void) { return sim_idle; }
static esp_err_t board_backlight(bool e) { sim_backlight=e; return ESP_OK; }
static const char *board_storage_summary(void) { return "TF: 29.72 GiB FAT32"; }
static const char *board_touch_id(void) { return "1158"; }
#endif
''',
'esp_timer.h': '''#pragma once
#include <stdint.h>
#include "lvgl.h"
static int64_t esp_timer_get_time(void) { return (int64_t)lv_tick_get()*1000; }
''',
'esp_app_desc.h': '''#pragma once
typedef struct { const char *version; } esp_app_desc_t;
static const esp_app_desc_t *esp_app_get_description(void) { static esp_app_desc_t d={"0.2.0"}; return &d; }
''',
'esp_psram.h': '''#pragma once
static unsigned esp_psram_get_size(void) { return 8*1024*1024; }
''',
'esp_system.h': '''#pragma once
static const char *esp_get_idf_version(void) { return "5.4.1 (host mock)"; }
static int esp_reset_reason(void) { return 1; }
''',
'class/hid/hid_device.h': '''#pragma once
#define KEYBOARD_MODIFIER_LEFTCTRL 1
#define KEYBOARD_MODIFIER_LEFTSHIFT 2
#define KEYBOARD_MODIFIER_LEFTALT 4
#define KEYBOARD_MODIFIER_LEFTGUI 8
#define HID_KEY_D 7
#define HID_KEY_ARROW_LEFT 80
#define HID_KEY_ARROW_RIGHT 79
''' + '\n'.join(f'#define HID_KEY_{c} {ord(c)-ord("A")+4}' for c in 'ACFNOPSTVWXYZ') + '''
#define HID_KEY_TAB 43
#define HID_KEY_ESCAPE 41
''',
}

HOST = r'''
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "lvgl.h"
#include "app.h"
#include "ui.h"
#include "shell.h"
#include "settings.h"
#include "usb_service.h"
#include "wifi_service.h"
extern const app_desc_t app_minimal;
static lv_color_t frame[320*480];
static lv_color_t draw[320*40];
static unsigned owner,session=1,keys;
static uint8_t last_mods,last_code;
static bool hid,autosleep=true;
bool sim_touch=false,sim_backlight=true;
int64_t sim_idle;
static bool save_error;
static uint32_t sleep_seconds=120;
static int count;
static wifi_status_t net={.available=true};
static wifi_network_t access_points[4];
static size_t ap_count;
static int wifi_connects;
static void mock_scan(void) {
    const char *names[]={"Home WiFi","家庭网络","Guest","Office"};
    unsigned security[]={NET_WPA2,NET_WPA3,NET_OPEN,NET_UNSUPPORTED};
    ap_count=4;
    for(unsigned i=0;i<ap_count;i++) {
        access_points[i]=(wifi_network_t){0};
        access_points[i].ssid_len=strlen(names[i]);
        memcpy(access_points[i].ssid,names[i],access_points[i].ssid_len);
        strcpy(access_points[i].name,names[i]); access_points[i].security=security[i]; access_points[i].rssi=-40-i*9;
    }
    net.scan_revision++;
}
void wifi_service_status(wifi_status_t *out) { *out=net; }
size_t wifi_service_networks(wifi_network_t *out,size_t capacity,unsigned *revision) {
    size_t n=ap_count<capacity?ap_count:capacity;
    if(out) memcpy(out,access_points,n*sizeof(*out)); if(revision) *revision=net.scan_revision; return n;
}
bool wifi_service_enable(bool enabled) { net.enabled=enabled; net.state=enabled?NET_IDLE:NET_OFF; return true; }
bool wifi_service_scan(void) { mock_scan(); return true; }
bool wifi_service_connect(const wifi_network_t *n,const char *password) {
    wifi_profile_t p={0}; memcpy(p.ssid,n->ssid,32); p.ssid_len=n->ssid_len; p.security=n->security;
    assert(strlen(password)<64); strcpy(p.password,password); assert(wifi_profile_valid(&p));
    wifi_secret_clear(&p,sizeof(p));
    wifi_connects++; net.state=NET_GETTING_IP; strcpy(net.name,n->name); return true;
}
size_t wifi_service_saved_networks(wifi_network_t *out,size_t cap,unsigned *revision) {
    if(revision) *revision=net.saved_revision;
    if(!net.has_saved||!cap) return 0;
    if(out) { *out=(wifi_network_t){0}; out->ssid_len=8; memcpy(out->ssid,"HomeWiFi",8); strcpy(out->name,"HomeWiFi"); out->security=NET_WPA2; out->remembered=true; }
    return 1;
}
bool wifi_service_connect_known(const wifi_network_t *n) { return wifi_service_connect(n,n->security==NET_OPEN?"":"test pass+1"); }
bool wifi_service_forget_known(const wifi_network_t *n) { net.has_saved=false; net.saved_revision++; return true; }
bool wifi_service_connect_saved(void) { net.state=NET_CONNECTING; return true; }
bool wifi_service_disconnect(void) { net.state=NET_IDLE; net.ip[0]=0; return true; }
bool wifi_service_forget(void) { net.has_saved=false; net.saved_name[0]=0; net.name[0]=0; net.state=NET_IDLE; net.ip[0]=0; return true; }
unsigned usb_service_set_owner(bool enabled) { hid=enabled; session++; return ++owner; }
void usb_service_cancel(void) { session++; }
unsigned usb_service_session(void) { return session; }
bool usb_service_online(void) { return true; }
bool usb_service_release_pending(void) { return false; }
bool usb_service_click(unsigned token,unsigned gesture,uint8_t mods,uint8_t key) {
    if(!hid || token!=owner || gesture!=session) return false; keys++; last_mods=mods;last_code=key;return true;
}
bool settings_available(void) { return true; }
bool settings_autosleep(void) { return autosleep; }
uint32_t settings_sleep_seconds(void) { return sleep_seconds; }
esp_err_t settings_set_sleep_seconds(uint32_t s) {
    if(save_error) return ESP_ERR_INVALID_STATE;
    sleep_seconds=s; autosleep=s!=0; return ESP_OK;
}
esp_err_t settings_set_autosleep(bool e) { autosleep=e; return ESP_OK; }
esp_err_t settings_counter_load(int *v) { *v=count; return ESP_OK; }
esp_err_t settings_counter_save(int v) { count=v; return ESP_OK; }
static void flush(lv_disp_drv_t *drv,const lv_area_t *a,lv_color_t *p) {
    for(int y=a->y1;y<=a->y2;y++) for(int x=a->x1;x<=a->x2;x++) frame[y*320+x]=*p++;
    lv_disp_flush_ready(drv);
}
static void settle(void) {
    for(int i=0;i<20;i++) { lv_tick_inc(20); lv_timer_handler(); shell_poll(); app_process(); }
    lv_refr_now(NULL);
}
static lv_obj_t *content(void) { return lv_obj_get_child(lv_scr_act(),3); }
static void snapshot(const char *name) {
    settle();
    char path[512]; snprintf(path,sizeof(path),"%s.ppm",name);
    FILE *f=fopen(path,"wb"); assert(f);
    fprintf(f,"P6\n320 480\n255\n");
    for(int i=0;i<320*480;i++) {
        lv_color32_t c; c.full=lv_color_to32(frame[i]);
        fputc(c.ch.red,f); fputc(c.ch.green,f); fputc(c.ch.blue,f);
    }
    fclose(f);
}
static void click(lv_obj_t *obj) {
    lv_event_send(obj,LV_EVENT_PRESSED,NULL);
    lv_event_send(obj,LV_EVENT_RELEASED,NULL);
    lv_event_send(obj,LV_EVENT_CLICKED,NULL);
    app_process(); settle();
}
static void check_bounds_inner(lv_obj_t *obj,bool scrollable) {
    lv_area_t a; lv_obj_get_coords(obj,&a);
    if(!scrollable) assert(a.x1>=0 && a.y1>=0 && a.x2<320 && a.y2<480);
    if(lv_obj_check_type(obj,&lv_label_class)) {
        /* Detect unexpectedly wrapped/clipped labels against their parent. */
        lv_area_t parent; lv_obj_get_coords(lv_obj_get_parent(obj),&parent);
        if(!lv_obj_check_type(lv_obj_get_parent(obj),&lv_textarea_class))
            assert(a.x1>=parent.x1 && a.x2<=parent.x2 && a.y1>=parent.y1 && a.y2<=parent.y2);
    }
    bool scrolling=scrollable || lv_obj_has_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    for(uint32_t i=0;i<lv_obj_get_child_cnt(obj);i++) check_bounds_inner(lv_obj_get_child(obj,i),scrolling);
}
static void check_bounds(lv_obj_t *obj) { check_bounds_inner(obj,false); }
static void keyboard_key(lv_obj_t *kb,const char *key) {
    for(unsigned i=0;i<50;i++) {
        const char *text=lv_btnmatrix_get_btn_text(kb,i);
        if(!text) break;
        if(!strcmp(text,key)) {
            lv_btnmatrix_set_selected_btn(kb,i); lv_event_send(kb,LV_EVENT_VALUE_CHANGED,NULL); settle(); return;
        }
    }
    assert(!"keyboard key not found");
}
int main(void) {
    lv_init();
    static lv_disp_draw_buf_t buf;
    static lv_disp_drv_t drv;
    lv_disp_draw_buf_init(&buf,draw,NULL,320*40);
    lv_disp_drv_init(&drv); drv.hor_res=320; drv.ver_res=480; drv.flush_cb=flush; drv.draw_buf=&buf;
    assert(lv_disp_drv_register(&drv));
    shell_init(); app_process(); snapshot("desktop"); check_bounds(lv_scr_act());
    assert(keys==0);
    click(lv_obj_get_child(lv_obj_get_child(content(),2),0)); assert(app_current()==app_registry[0]);
    snapshot("keysim-1"); check_bounds(lv_scr_act()); assert(keys==0);
    click(lv_obj_get_child(content(),1)); assert(keys==1);
    click(lv_obj_get_child(content(),9)); snapshot("keysim-2"); check_bounds(lv_scr_act()); assert(keys==1);
    /* Press in one USB session, release after invalidation: no shortcut. */
    lv_obj_t *key=lv_obj_get_child(content(),1);
    lv_event_send(key,LV_EVENT_PRESSED,NULL); usb_service_cancel();
    lv_event_send(key,LV_EVENT_CLICKED,NULL); assert(keys==1);
    app_request(app_registry[1]); app_process(); snapshot("counter"); check_bounds(lv_scr_act());
    click(lv_obj_get_child(content(),4)); settle();
    app_request(NULL); app_process(); assert(count==1);
    app_request(app_registry[2]); app_process(); snapshot("settings"); check_bounds(lv_scr_act());
    lv_obj_t *duration=lv_obj_get_child(content(),2);
    assert(lv_dropdown_get_selected(duration)==2);
    const uint32_t durations[]={30,60,120,300,600,0};
    lv_dropdown_open(duration); snapshot("settings-durations"); check_bounds(lv_scr_act());
    lv_dropdown_close(duration);
    for(unsigned i=0;i<6;i++) {
        lv_dropdown_set_selected(duration,i);
        lv_event_send(duration,LV_EVENT_VALUE_CHANGED,NULL);
        assert(settings_sleep_seconds()==durations[i]);
        if(durations[i]) {
            sim_idle=(int64_t)durations[i]*1000000-1; shell_poll(); assert(sim_backlight);
            sim_idle+=2; shell_poll(); assert(!sim_backlight);
            sim_touch=true; sim_idle=0; shell_poll(); assert(sim_backlight); sim_touch=false;
        } else {
            sim_idle=86400000000LL; shell_poll(); assert(sim_backlight); sim_idle=0;
            shell_sleep(); assert(!sim_backlight);
            sim_touch=true; shell_poll(); assert(sim_backlight); sim_touch=false;
        }
    }
    save_error=true; lv_dropdown_set_selected(duration,0);
    lv_event_send(duration,LV_EVENT_VALUE_CHANGED,NULL);
    assert(lv_dropdown_get_selected(duration)==5 && settings_sleep_seconds()==0);
    save_error=false;
    app_request(NULL); app_process(); app_request(app_registry[2]); app_process();
    assert(lv_dropdown_get_selected(lv_obj_get_child(content(),2))==5);
    settings_set_sleep_seconds(120);
    app_request(app_registry[3]); app_process(); snapshot("system-info"); check_bounds(lv_scr_act());
    app_request(app_registry[4]); app_process(); snapshot("wifi-off"); check_bounds(lv_scr_act());
    lv_obj_t *toggle=lv_obj_get_child(content(),1);
    lv_obj_add_state(toggle,LV_STATE_CHECKED); lv_event_send(toggle,LV_EVENT_VALUE_CHANGED,NULL); settle();
    click(lv_obj_get_child(content(),3)); snapshot("wifi-networks"); check_bounds(lv_scr_act());
    lv_obj_t *list=lv_obj_get_child(content(),6);
    assert(lv_obj_has_state(lv_obj_get_child(list,3),LV_STATE_DISABLED));
    click(lv_obj_get_child(list,0)); settle(); snapshot("wifi-password"); check_bounds(lv_scr_act());
    lv_obj_t *password=lv_obj_get_child(content(),1);
    lv_obj_t *kb=lv_obj_get_child(content(),5);
    bool ascii_keys[128]={0};
    const char *modes[]={"ABC","123","符号","abc"};
    for(unsigned mode=0;mode<4;mode++) {
        keyboard_key(kb,modes[mode]);
        for(unsigned i=0;i<50;i++) {
            const char *text=lv_btnmatrix_get_btn_text(kb,i); if(!text) break;
            if(strlen(text)==1 && (unsigned char)text[0]<128) ascii_keys[(unsigned char)text[0]]=true;
        }
    }
    for(unsigned i=33;i<127;i++) assert(ascii_keys[i]);
    keyboard_key(kb,"空格"); assert(!strcmp(lv_textarea_get_text(password)," "));
    keyboard_key(kb,"退格"); assert(!strcmp(lv_textarea_get_text(password),""));
    click(lv_obj_get_child(content(),3)); assert(wifi_connects==0); /* Reject a short password. */
    lv_obj_t *show=lv_obj_get_child(content(),2);
    lv_obj_add_state(show,LV_STATE_CHECKED); lv_event_send(show,LV_EVENT_VALUE_CHANGED,NULL);
    assert(!lv_textarea_get_password_mode(password));
    lv_obj_clear_state(show,LV_STATE_CHECKED); lv_event_send(show,LV_EVENT_VALUE_CHANGED,NULL);
    keyboard_key(kb,"t"); keyboard_key(kb,"e"); keyboard_key(kb,"s"); keyboard_key(kb,"t");
    keyboard_key(kb,"123"); keyboard_key(kb,"1"); keyboard_key(kb,"2"); keyboard_key(kb,"3"); keyboard_key(kb,"4");
    assert(!strcmp(lv_textarea_get_text(password),"test1234"));
    char too_long[80]; memset(too_long,'a',79); too_long[79]=0;
    /* Exercise the input path: LVGL 8.4's programmatic password setter does not
     * enforce max_length on its private pwd_tmp copy; the service also validates. */
    lv_textarea_set_text(password,""); lv_textarea_add_text(password,too_long);
    assert(strlen(lv_textarea_get_text(password))==63);
    lv_textarea_set_text(password,"test1234");
    assert(lv_textarea_get_password_mode(password));
    snapshot("wifi-password-entered"); assert(keys==1);
    click(lv_obj_get_child(content(),3)); snapshot("wifi-connecting"); check_bounds(lv_scr_act());
    assert(wifi_connects==1);
    net.state=NET_CONNECTED; net.has_saved=true; net.rssi=-43; strcpy(net.ip,"192.168.1.99"); strcpy(net.saved_name,net.name);
    snapshot("wifi-connected"); check_bounds(lv_scr_act());
    click(lv_obj_get_child(content(),3)); snapshot("wifi-forget"); check_bounds(lv_scr_act());
    click(lv_obj_get_child(content(),2)); assert(!net.has_saved); settle();
    list=lv_obj_get_child(content(),6); click(lv_obj_get_child(list,2)); snapshot("wifi-open"); check_bounds(lv_scr_act());
    assert(wifi_connects==1); /* Open networks require an explicit connect action. */
    click(lv_obj_get_child(content(),2)); assert(wifi_connects==2 && keys==1);
    app_request_back(); app_process(); settle(); assert(app_current()==app_registry[4]);
    app_request(NULL); app_process(); settle();
    /* Compile and exercise the shipped template without altering the firmware registry. */
    app_request(&app_minimal); app_process(); settle();
    click(lv_obj_get_child(content(),1));
    assert(!strcmp(lv_label_get_text(lv_obj_get_child(content(),0)),"点击次数: 1"));
    app_request(NULL); app_process(); settle();
    /* A fifth launcher entry must scroll into view without moving system navigation. */
    lv_obj_t *grid=lv_obj_get_child(content(),2);
    lv_obj_t *extra=ui_button(grid,"示例应用",166,232,142,102,NULL,NULL);
    settle(); assert(lv_obj_get_scroll_bottom(grid)>0);
    lv_obj_scroll_to_view(extra,LV_ANIM_OFF); settle();
    lv_area_t tile,viewport;
    lv_obj_get_coords(extra,&tile); lv_obj_get_coords(grid,&viewport);
    assert(tile.y2<=viewport.y2 && tile.y1>=viewport.y1);
    lv_obj_del(extra); lv_obj_scroll_to_y(grid,0,LV_ANIM_OFF); settle();
    /* Compare fresh desktop instances, rather than a scrolled-and-edited grid's capacity. */
    app_request(NULL); app_process(); settle();
    lv_mem_monitor_t before,after; lv_mem_monitor(&before);
    for(int i=0;i<500;i++) {
        app_request(app_registry[i%app_registry_count]); app_process(); settle();
        app_request(NULL); app_process(); settle();
    }
    lv_mem_monitor(&after);
    assert(keys==1);
    assert(after.free_size==before.free_size);
    assert(lv_mem_test()==LV_RES_OK);
    printf("PASS REAL LVGL: Wi-Fi off/list/password/keyboard/connecting/connected/forget/open, no HID from typing, original apps, template, launcher scroll, 500 switches, heap free %u -> %u\n",
           (unsigned)before.free_size,(unsigned)after.free_size);
    return 0;
}
'''

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='touchdesk-ui-') as tmp:
        tmp = Path(tmp)
        headers = {k: v for k, v in HEADERS.items() if k not in ('lvgl.h','board.h','shell.h')}
        headers.update(EXTRA_HEADERS)
        for name, text in headers.items():
            p = tmp/name
            p.parent.mkdir(parents=True, exist_ok=True)
            guard = 'TD_SIM_'+name.upper().replace('/','_').replace('.','_')
            p.write_text(f'#ifndef {guard}\n#define {guard}\n{text}\n#endif\n', encoding='utf-8')
        host = tmp/'sim.c'
        host.write_text(HOST, encoding='utf-8')
        sources = list((ROOT/'components/lvgl/src').rglob('*.c'))
        sources += [ROOT/'main/app_registry.c', ROOT/'main/platform/app.c']
        sources += list((ROOT/'main/shell').glob('*.c'))
        sources += list((ROOT/'main/apps').rglob('*.c'))
        sources += [ROOT/'templates/minimal/app_minimal.c']
        sources += [ROOT/'main/services/wifi_policy.c']
        command = [args.cc, '-DLV_CONF_SKIP', '-DLV_MEM_SIZE=2097152', '-DLV_FONT_MONTSERRAT_20=1']
        command += ['-I'+str(p) for p in [tmp, ROOT/'components/lvgl', ROOT/'main/platform',
                                         ROOT/'main/shell', ROOT/'main/services']]
        # Compile separately to keep Windows command lines below the process limit.
        objects=[]
        for i, source in enumerate([host, *sources]):
            obj=tmp/f'{i}.o'
            subprocess.run([*command,'-c',str(source),'-o',str(obj)],check=True,
                           stdout=subprocess.DEVNULL)
            objects.append(obj)
        exe=tmp/'sim.exe'
        subprocess.run([args.cc,*map(str,objects),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],cwd=args.output,check=True)
    from PIL import Image
    for ppm in args.output.glob('*.ppm'):
        Image.open(ppm).save(ppm.with_suffix('.png'))
        ppm.unlink()

if __name__=='__main__':
    main()
