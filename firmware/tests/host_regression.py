"""Run production C logic with hardware/RTOS mocks. Never opens USB or serial.

Usage: python tests/host_regression.py --cc C:/path/to/tcc.exe
TinyCC's lack of C11 atomics is mocked: tests exercise deterministic interleavings,
not CPU memory ordering. GCC/Clang may also compile these tests.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HEADERS = {
'esp_err.h': '''#pragma once
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NOT_SUPPORTED 3
#define ESP_ERR_NVS_NOT_FOUND 4
#define ESP_ERR_INVALID_ARG 5
#define ESP_ERR_TIMEOUT 6
static const char *esp_err_to_name(int e) { return "mock error"; }
''',
'stdatomic.h': '''#pragma once
#include <stdbool.h>
typedef unsigned atomic_uint;
typedef bool atomic_bool;
#define atomic_load(p) (*(p))
#define atomic_store(p,v) (*(p)=(v))
#define atomic_fetch_add(p,v) ((*(p))+=(v))
''',
'freertos/FreeRTOS.h': '''#pragma once
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
typedef uint32_t TickType_t;
#define portMAX_DELAY 0xffffffffu
#define pdTRUE 1
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
''',
'freertos/task.h': '''#pragma once
#include "FreeRTOS.h"
static TickType_t clock_ms;
static bool fail_task;
typedef void *TaskHandle_t;
enum { eRunning, eSuspended, eBlocked };
static int task_state=eSuspended,deleted_tasks;
static int eTaskGetState(TaskHandle_t task) { return task_state; }
static void vTaskDelete(TaskHandle_t task) { deleted_tasks++; }
static void vTaskSuspend(TaskHandle_t task) { task_state=eSuspended; }
static void vTaskResume(TaskHandle_t task) { task_state=eRunning; }
static TickType_t xTaskGetTickCount(void) { return clock_ms; }
static void vTaskDelay(TickType_t n) { clock_ms+=n; }
static int xTaskCreate(void (*f)(void*),const char*n,int stack,void*a,int priority,void*b) { if(fail_task)return 0; if(b)*(TaskHandle_t*)b=(TaskHandle_t)1; return pdPASS; }
''',
'freertos/queue.h': '''#pragma once
#include "FreeRTOS.h"
typedef struct { unsigned count,size; unsigned char data[8][64]; } mock_queue_t;
typedef mock_queue_t *QueueHandle_t;
static bool fail_queue;
static QueueHandle_t xQueueCreate(unsigned n,unsigned size) {
    if(fail_queue) return NULL;
    QueueHandle_t q=calloc(1,sizeof(*q)); q->size=size; return q;
}
static int xQueueReset(QueueHandle_t q) { q->count=0; return pdTRUE; }
static void vQueueDelete(QueueHandle_t q) { free(q); }
static int xQueueSend(QueueHandle_t q,const void*p,unsigned t) {
    if(q->count==8) return 0; memcpy(q->data[q->count++],p,q->size); return pdTRUE;
}
static int xQueuePeek(QueueHandle_t q,void*p,unsigned t) {
    if(!q->count) return 0; memcpy(p,q->data[0],q->size); return pdTRUE;
}
static int xQueueReceive(QueueHandle_t q,void*p,unsigned t) {
    if(!xQueuePeek(q,p,t)) return 0; --q->count;
    memmove(q->data,q->data+1,q->count*sizeof(q->data[0])); return pdTRUE;
}
''',
'freertos/semphr.h': '''#pragma once
#include "FreeRTOS.h"
typedef int *SemaphoreHandle_t;
static bool fail_mutex;
static SemaphoreHandle_t xSemaphoreCreateMutex(void) { return fail_mutex ? NULL : calloc(1,sizeof(int)); }
static void xSemaphoreTake(SemaphoreHandle_t p,unsigned wait) { assert(!*p); *p=1; }
static void xSemaphoreGive(SemaphoreHandle_t p) { assert(*p); *p=0; }
static void vSemaphoreDelete(SemaphoreHandle_t p) { free(p); }
''',
'tinyusb.h': '''#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct { const void *device_descriptor; const char **string_descriptor;
int string_descriptor_count; bool external_phy; const uint8_t *configuration_descriptor; } tinyusb_config_t;
static bool fail_install,fail_uninstall;
static int tinyusb_driver_install(const tinyusb_config_t *cfg) { return fail_install ? 7 : 0; }
static int tinyusb_driver_uninstall(void) { return fail_uninstall?8:0; }
#define TUD_HID_REPORT_DESC_KEYBOARD(...) 0
#define TUD_HID_REPORT_DESC_MOUSE(...) 0
#define TUD_HID_REPORT_DESC_CONSUMER(...) 0
#define HID_REPORT_ID(n) n
#define TUD_CONFIG_DESC_LEN 9
#define TUD_HID_DESC_LEN 25
#define TUD_CONFIG_DESCRIPTOR(...) 0
#define TUD_HID_DESCRIPTOR(...) 0
#define HID_ITF_PROTOCOL_KEYBOARD 1
#define HID_ITF_PROTOCOL_NONE 0
''',
'class/hid/hid_device.h': '''#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef int hid_report_type_t;
#define HID_REPORT_TYPE_INPUT 1
#define HID_USAGE_CONSUMER_VOLUME_INCREMENT 0xE9
#define HID_USAGE_CONSUMER_VOLUME_DECREMENT 0xEA
#define HID_USAGE_CONSUMER_MUTE 0xE2
static bool endpoint_ready=true,accept_report=true;
static unsigned consumer_reports;static uint16_t last_consumer;
static bool tud_hid_report(uint8_t id,const void *data,uint16_t len) {
 if(!accept_report)return false;
 assert(id==3 && len==2);memcpy(&last_consumer,data,2);consumer_reports++;return true;
}
static int reports,presses,releases;
static uint8_t last_key;
static int mouse_reports, mouse_total_x, mouse_total_y, mouse_total_wheel;
static uint8_t last_buttons;
static bool tud_hid_mouse_report(uint8_t id,uint8_t buttons,int8_t x,int8_t y,int8_t w,int8_t pan) {
    if(!accept_report) return false;
    assert(id==2); mouse_reports++; last_buttons=buttons;
    mouse_total_x+=x; mouse_total_y+=y; mouse_total_wheel+=w;
    return true;
}
static bool tud_hid_ready(void) { return endpoint_ready; }
static bool tud_hid_keyboard_report(uint8_t id,uint8_t mods,const uint8_t *keys) {
    if(!accept_report) return false;
    reports++; last_key=keys ? keys[0] : 0;
    if(last_key || mods) presses++; else releases++;
    return true;
}
''',
'esp_mac.h': '''#pragma once
#include <string.h>
#define ESP_MAC_WIFI_STA 0
static int esp_read_mac(unsigned char *mac,int type) { memset(mac,1,6); return 0; }
''',
'lvgl.h': '''#pragma once
#include <stdint.h>
typedef struct { int unused; } lv_obj_t;
typedef struct lv_timer_t { void *user_data; } lv_timer_t;
typedef void (*lv_timer_cb_t)(lv_timer_t*);
static int live_timers,cleaned;
static lv_timer_t *lv_timer_create(lv_timer_cb_t cb,uint32_t period,void *ctx) {
    live_timers++; lv_timer_t *t=malloc(sizeof(*t)); t->user_data=ctx; return t;
}
static void lv_timer_del(lv_timer_t *t) { live_timers--; free(t); }
static void lv_obj_clean(lv_obj_t *o) { cleaned++; }
''',
'board.h': '''#pragma once
static int gates;
static void board_input_cancel(void) { gates++; }
''',
'shell.h': '''#pragma once
#include "lvgl.h"
static int notices,desktops;
static void shell_title(const char *s) {}
static void shell_fullscreen(bool enabled) {}
static void shell_notice(const char *s) { notices++; }
static void shell_desktop(lv_obj_t *r) { desktops++; }
''',
'esp_heap_caps.h': '''#pragma once
#include <stdlib.h>
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_8BIT 4
static int allocations;
static int memory_low;
static size_t heap_caps_get_free_size(int caps) { return memory_low ? 1024 : 1024*1024; }
static size_t heap_caps_get_largest_free_block(int caps) { return memory_low ? 1024 : 1024*1024; }
static void *heap_caps_calloc(size_t n,size_t size,int caps) { allocations++; return calloc(n,size); }
static void heap_caps_free(void *p) { if(p) allocations--; free(p); }
''',
'esp_log.h': '''#pragma once
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
''',
'nvs.h': '''#pragma once
#include <stdint.h>
typedef unsigned nvs_handle_t;
#define NVS_READWRITE 1
int nvs_open(const char *,int,nvs_handle_t *);
void nvs_close(nvs_handle_t);
int nvs_commit(nvs_handle_t);
int nvs_get_u32(nvs_handle_t,const char *,uint32_t *);
int nvs_set_u32(nvs_handle_t,const char *,uint32_t);
int nvs_get_u8(nvs_handle_t,const char *,uint8_t *);
int nvs_set_u8(nvs_handle_t,const char *,uint8_t);
int nvs_get_i32(nvs_handle_t,const char *,int32_t *);
int nvs_set_i32(nvs_handle_t,const char *,int32_t);
''',
'nvs_flash.h': '''#pragma once
int nvs_flash_init(void);
''',
}

HEADERS['tinyusb.h'] += r'''

static bool mock_ble_ready,mock_ble_accept=true;
static unsigned mock_ble_generation,mock_ble_reports,mock_ble_cancels,mock_pairs,mock_forgets;
static uint8_t mock_ble_key,mock_ble_mods,mock_ble_buttons;
static uint16_t mock_ble_consumer;
esp_err_t ble_mouse_init(void){return ESP_OK;}
esp_err_t ble_mouse_deinit(void){mock_ble_ready=false;return ESP_OK;}
bool ble_touch_ready(void){return mock_ble_ready;}
void ble_mouse_snapshot(ble_mouse_snapshot_t *s){memset(s,0,sizeof(*s));s->reconnections=mock_ble_generation;}
void ble_touch_cancel(void){mock_ble_cancels++;mock_ble_key=mock_ble_mods=mock_ble_buttons=0;mock_ble_consumer=0;}
void ble_mouse_pair(void){mock_pairs++;}
void ble_mouse_forget(void){mock_forgets++;}
bool ble_touch_report(unsigned id,const void *data){
 if(!mock_ble_ready||!mock_ble_accept)return false;
 mock_ble_reports++;
 if(id==2){const uint8_t*r=data;mock_ble_mods=r[0];mock_ble_key=r[2];}
 else if(id==1)mock_ble_buttons=((const int16_t*)data)[0];
 else mock_ble_consumer=*(const uint16_t*)data;
 return true;
}
'''

USB_TEST = r'''
#include <assert.h>
#include <stdio.h>
#include "@ROOT@/main/services/usb_service.c"
int main(void) {
    fail_queue=true;
    assert(usb_service_init()==ESP_ERR_NO_MEM && !queue && !lock);
    assert(usb_service_set_owner(false)==0);
    usb_service_cancel(); assert(!usb_service_click(0,0,0,4));
    fail_queue=false; fail_mutex=true;
    assert(usb_service_init()==ESP_ERR_NO_MEM && !queue && !lock);
    fail_mutex=false; fail_task=true;
    assert(usb_service_init()==ESP_ERR_NO_MEM && !queue && !lock && !usb_service_online());
    fail_task=false; fail_install=true;
    assert(usb_service_init()==7 && !queue && !lock);
    fail_install=false;
    assert(usb_service_init()==ESP_OK);
    worker_state_t s={.seen=~0u,.releasing=true};
    worker_step(&s); assert(reports==0);
    tud_mount_cb(); worker_step(&s);
    assert(releases==1 && presses==0); /* Startup/reconnect only releases. */
    unsigned owner1=usb_service_set_owner(true);
    worker_step(&s); assert(presses==0);
    unsigned gesture=usb_service_session();
    assert(usb_service_click(owner1,gesture,1,4));
    worker_step(&s); assert(presses==1 && last_key==4);
    clock_ms+=39; worker_step(&s); assert(releases==2);
    clock_ms++; endpoint_ready=false; worker_step(&s); assert(releases==2);
    endpoint_ready=true; accept_report=false; worker_step(&s); assert(releases==2);
    accept_report=true; worker_step(&s); assert(releases==3);
    assert(usb_service_click(owner1,gesture,1,5));
    unsigned desktop=usb_service_set_owner(false);
    assert(!usb_service_click(owner1,gesture,1,5));
    assert(!usb_service_click(desktop,usb_service_session(),1,5));
    for(int i=0;i<10;i++) worker_step(&s);
    assert(presses==1 && queue->count==0); /* Queued old-app click is gone. */
    unsigned owner2=usb_service_set_owner(true); worker_step(&s);
    gesture=usb_service_session();
    for(int i=0;i<8;i++) assert(usb_service_click(owner2,gesture,0,6));
    assert(!usb_service_click(owner2,gesture,0,6));
    usb_service_cancel(); worker_step(&s);
    assert(queue->count==0 && presses==1);
    assert(!usb_service_click(owner2,gesture,0,6)); /* Old gesture after page/sleep cancel. */
    gesture=usb_service_session();
    assert(usb_service_click(owner2,gesture,1,7)); worker_step(&s);
    assert(presses==2);
    assert(usb_service_click(owner2,gesture,1,8));
    tud_suspend_cb(false); clock_ms+=100; worker_step(&s);
    assert(!usb_service_online());
    int before=releases;
    tud_resume_cb(); worker_step(&s); assert(releases==before+1);
    for(int i=0;i<10;i++) worker_step(&s);
    assert(presses==2 && queue->count==0); /* No replay after resume. */
    gesture=usb_service_session();
    assert(usb_service_click(owner2,gesture,0,9));
    tud_umount_cb(); worker_step(&s); tud_mount_cb(); worker_step(&s);
    for(int i=0;i<10;i++) worker_step(&s);
    assert(presses==2);
    /* Revoking a key already down requests release immediately, before 40 ms. */
    gesture=usb_service_session();
    assert(usb_service_click(owner2,gesture,1,10)); worker_step(&s);
    before=releases; usb_service_set_owner(false); worker_step(&s);
    assert(releases==before+1);
    QueueHandle_t original_queue=queue;
    SemaphoreHandle_t original_lock=lock;
    for(unsigned cycle=0;cycle<100;cycle++) {
        unsigned token=usb_service_set_owner(true);
        assert(usb_service_click(token,usb_service_session(),1,12));
        int old_presses=presses;
        assert(usb_service_stop()==ESP_OK && !usb_service_online() && queue->count==0);
        worker_step(&s); assert(presses==old_presses);
        assert(usb_service_start()==ESP_OK && !usb_service_online());
        assert(queue==original_queue && lock==original_lock && worker_started);
        tud_mount_cb(); worker_step(&s); assert(presses==old_presses);
    }
    assert(usb_service_stop()==ESP_OK);
    fail_install=true; assert(usb_service_start()==7 && !usb_service_online());
    fail_install=false; assert(usb_service_start()==ESP_OK);
    tud_mount_cb();worker_step(&s);
    fail_uninstall=true;
    assert(usb_service_deinit()==8 && driver_installed && queue==original_queue && lock==original_lock && worker_started);
    assert(usb_service_start()==ESP_ERR_INVALID_STATE);
    fail_uninstall=false;task_state=eRunning;
    assert(usb_service_deinit()==ESP_ERR_TIMEOUT && !driver_installed && queue && lock && hid_task);
    task_state=eSuspended;
    assert(usb_service_deinit()==ESP_OK && !queue && !lock && !hid_task && !worker_started);
    for(unsigned cycle=0;cycle<100;cycle++) {
        assert(usb_service_init()==ESP_OK);tud_mount_cb();
        assert(usb_service_deinit()==ESP_OK&&!queue&&!lock&&!hid_task);
    }
    puts("PASS USB full deinit: 100 create/destroy cycles, uninstall failure retains callbacks, live worker timeout preserves resources, safe retry");
    puts("PASS USB: init failures, startup, owner revoke, queue full, 40ms, endpoint busy/failure, cancel, suspend/resume, reconnect");
    puts("PASS HID runtime lifecycle: 100 stop/start cycles, retained worker resources, stale input discarded, reinstall failure and recovery");
    return 0;
}
'''

APP_TEST = r'''
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "@ROOT@/main/platform/app.c"
esp_err_t app_services_prepare(const app_desc_t *next) { return ESP_OK; }
static esp_err_t mode_error;
static bool mode_pending,mode_start;
esp_err_t usb_app_mode_prepare(const app_desc_t *next) { if(mode_start && !mode_error) mode_pending=true; return mode_error; }
bool usb_app_mode_pending(void) { return mode_pending; }
static int grants,revokes,destroyed,resources,created;
unsigned usb_service_set_owner(bool enabled) { if(enabled) grants++; else revokes++; return grants; }
bool usb_service_click(unsigned o,unsigned g,uint8_t m,uint8_t k) { return true; }
static void resource_free(void *p) { resources--; free(p); }
static void timer_cb(lv_timer_t *t) {}
static esp_err_t create(app_ctx_t *ctx,lv_obj_t *root) {
    created++;
    assert(ctx->active && ctx->state);
    for(int i=0;i<APP_MAX_TIMERS;i++) assert(app_timer_create(ctx,timer_cb,100));
    assert(!app_timer_create(ctx,timer_cb,100));
    for(int i=0;i<APP_MAX_CLEANUPS;i++) {
        void *p=malloc(4); resources++;
        assert(app_track(ctx,resource_free,p));
    }
    assert(!app_track(ctx,resource_free,NULL));
    return ESP_OK;
}
static void destroy(app_ctx_t *ctx) {
    assert(!ctx->active); assert(live_timers==0); assert(ctx->state); destroyed++;
}
static esp_err_t fail(app_ctx_t *ctx,lv_obj_t *root) { create(ctx,root); return ESP_ERR_NO_MEM; }
static bool back_handled(app_ctx_t *ctx) { return true; }
static const app_desc_t sample={.id="sample",.title="Sample",.caps=APP_CAP_HID,
    .state_size=128,.create=create,.destroy=destroy};
static const app_desc_t broken={.id="broken",.title="Broken",.state_size=128,.create=fail,.destroy=destroy};
static const app_desc_t subpage={.id="subpage",.title="Subpage",.state_size=128,
    .create=create,.destroy=destroy,.back=back_handled};
int main(void) {
    lv_obj_t host={0}; app_platform_init(&host);
    app_request(NULL); app_process(); assert(desktops==1);
    for(int i=0;i<500;i++) {
        app_request(&sample); assert(!app_current()); /* Navigation is deferred. */
        app_process(); assert(app_current()==&sample);
        assert(app_hid_click(&current,0,1,4));
        app_request_back(); app_process();
        assert(!app_current());
        assert(allocations==0 && live_timers==0 && resources==0);
        assert(!app_hid_click(&current,0,1,4));
    }
    assert(destroyed==500 && grants==500);
    app_request(&broken); app_process();
    assert(!app_current() && notices==1 && allocations==0 && live_timers==0 && resources==0);
    memory_low=1; app_request(&sample); app_process(); memory_low=0;
    assert(!app_current() && notices==2 && allocations==0);
    app_request(&subpage); app_process(); app_request_back(); app_process();
    assert(app_current()==&subpage);
    app_request(NULL); app_process();
    assert(allocations==0 && live_timers==0 && resources==0);
    app_request(&sample); app_process();
    int previous_destroyed=destroyed,previous_created=created;
    mode_error=ESP_ERR_INVALID_STATE;
    app_request(&subpage); app_process();
    assert(!app_current() && destroyed==previous_destroyed+1 && created==previous_created);
    mode_error=ESP_OK;mode_start=true;
    app_request(&subpage);app_process();
    assert(!app_current() && mode_pending && destroyed==previous_destroyed+1);
    assert(allocations==0 && live_timers==0 && resources==0);
    app_request(&sample);app_request_back();app_process();
    assert(created==previous_created && !app_current());
    puts("PASS APP: 500 lifecycle cycles, deferred navigation, HID focus, timer/resource limits, partial-create failure, low memory, back routing");
    puts("PASS APP role transition: preparation failure releases old app and returns to desktop, pending transition closes resources and blocks navigation");
    return 0;
}
'''

INPUT_TEST = r'''
#include <assert.h>
#include <stdio.h>
#include "@ROOT@/components/board/input_guard.h"
int main(void) {
    input_guard_t g={0};
    assert(input_guard_filter(&g,true,true));
    input_guard_cancel(&g); /* Switch application while a finger is held. */
    for(int i=0;i<100;i++) assert(!input_guard_filter(&g,true,true));
    assert(!input_guard_filter(&g,false,true));
    assert(input_guard_filter(&g,true,true));
    input_guard_cancel(&g); /* Sleep. */
    assert(!input_guard_filter(&g,false,false));
    assert(!input_guard_filter(&g,true,false)); /* First touch remains invisible. */
    input_guard_cancel(&g); /* Wake before backlight is turned on. */
    for(int i=0;i<100;i++) assert(!input_guard_filter(&g,true,true));
    assert(!input_guard_filter(&g,false,true));
    assert(input_guard_filter(&g,true,true));
    puts("PASS INPUT: held-touch switch, asleep touch suppression, wake-touch consumption, rearm after release");
    return 0;
}
'''

NVS_TEST = r'''
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "@ROOT@/main/services/settings.c"
static int init_error,commit_error,version_present,auto_present,counter_present,writes;
static uint32_t version_value;
static uint32_t timeout_value;
static int timeout_present;
static uint8_t auto_value;
static int32_t counter_value;
int nvs_flash_init(void) { return init_error; }
int nvs_open(const char *name,int mode,nvs_handle_t *h) {
    assert(!strcmp(name,"system") || !strcmp(name,"app_counter"));
    *h=!strcmp(name,"system") ? 1 : 2; return ESP_OK;
}
void nvs_close(nvs_handle_t h) {}
int nvs_commit(nvs_handle_t h) { return commit_error; }
int nvs_get_u32(nvs_handle_t h,const char *key,uint32_t *v) {
    if(!strcmp(key,"sleep_seconds")) {
        if(timeout_present) *v=timeout_value;
        return timeout_present ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
    }
    assert(h==1 && !strcmp(key,"schema")); *v=version_value;
    return version_present ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
int nvs_set_u32(nvs_handle_t h,const char *key,uint32_t v) {
    if(!strcmp(key,"sleep_seconds")) { timeout_present=1; timeout_value=v; writes++; return ESP_OK; }
    assert(h==1 && !strcmp(key,"schema")); version_present=1; version_value=v; writes++; return ESP_OK;
}
int nvs_get_u8(nvs_handle_t h,const char *key,uint8_t *v) {
    assert(h==1 && !strcmp(key,"autosleep"));
    if(auto_present) *v=auto_value;
    return auto_present ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
int nvs_set_u8(nvs_handle_t h,const char *key,uint8_t v) {
    assert(h==1 && !strcmp(key,"autosleep")); auto_present=1; auto_value=v; writes++; return ESP_OK;
}
int nvs_get_i32(nvs_handle_t h,const char *key,int32_t *v) {
    assert(h==2 && !strcmp(key,"value_v1"));
    if(counter_present) *v=counter_value;
    return counter_present ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
int nvs_set_i32(nvs_handle_t h,const char *key,int32_t v) {
    assert(h==2 && !strcmp(key,"value_v1")); counter_present=1; counter_value=v; writes++; return ESP_OK;
}
static void reboot(void) { ready=false; autosleep=true; }
int main(void) {
    assert(settings_init()==ESP_OK && settings_available() && settings_autosleep());
    assert(version_value==1 && writes==1);
    assert(settings_set_autosleep(false)==ESP_OK && !settings_autosleep());
    reboot(); assert(settings_init()==ESP_OK && !settings_autosleep());
    timeout_present=0; auto_present=1; auto_value=0;
    reboot(); assert(settings_init()==ESP_OK && settings_sleep_seconds()==0);
    auto_value=1; reboot(); assert(settings_init()==ESP_OK && settings_sleep_seconds()==120);
    auto_value=9; reboot(); assert(settings_init()==ESP_OK && settings_autosleep());
    const uint32_t choices[]={30,60,120,300,600,0};
    for(unsigned i=0;i<6;i++) {
        assert(settings_set_sleep_seconds(choices[i])==ESP_OK);
        reboot(); assert(settings_init()==ESP_OK && settings_sleep_seconds()==choices[i]);
    }
    assert(settings_set_sleep_seconds(31)==ESP_ERR_INVALID_ARG);
    timeout_value=999; reboot(); assert(settings_init()==ESP_OK && settings_sleep_seconds()==120);
    commit_error=7;
    assert(settings_set_autosleep(false)==7 && settings_autosleep());
    assert(settings_sleep_seconds()==120);
    commit_error=0;
    int v=-1;
    assert(settings_counter_load(&v)==ESP_OK && v==0);
    assert(settings_counter_save(9999)==ESP_OK);
    assert(settings_counter_load(&v)==ESP_OK && v==9999);
    assert(settings_counter_save(-1)==ESP_ERR_INVALID_STATE);
    assert(settings_counter_save(10000)==ESP_ERR_INVALID_STATE);
    counter_value=10000; assert(settings_counter_load(&v)==ESP_OK && v==0);
    version_value=2; reboot(); int before=writes;
    assert(settings_init()==ESP_ERR_NOT_SUPPORTED && !settings_available() && writes==before);
    init_error=8; reboot();
    assert(settings_init()==8 && !settings_available());
    assert(settings_set_autosleep(false)==ESP_ERR_INVALID_STATE);
    puts("PASS NVS: defaults, versioning, invalid values, persistence, commit failure, future schema preservation, init failure");
    return 0;
}
'''

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--cc', default='cc')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='touchdesk-test-') as temp:
        temp = Path(temp)
        for name, text in HEADERS.items():
            path = temp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            guard = 'TD_TEST_' + name.upper().replace('/', '_').replace('.', '_')
            path.write_text(f'#ifndef {guard}\n#define {guard}\n{text}\n#endif\n', encoding='utf-8')
        for name, text in [('usb', USB_TEST), ('app', APP_TEST), ('input', INPUT_TEST), ('nvs', NVS_TEST)]:
            src = temp / f'{name}.c'
            src.write_text(text.replace('@ROOT@', ROOT.as_posix()), encoding='utf-8')
            exe = temp / f'{name}.exe'
            command = [args.cc, '-I'+str(temp), '-I'+str(ROOT/'main/services'),
                       '-I'+str(ROOT/'main/platform'), str(src), '-o', str(exe)]
            subprocess.run(command, check=True)
            subprocess.run([str(exe)], check=True)

if __name__ == '__main__':
    main()
