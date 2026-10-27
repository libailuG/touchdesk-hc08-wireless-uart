#include "usb_service.h"
#include "service_task.h"
#include "ble_mouse.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"
#include "esp_mac.h"

typedef struct { unsigned owner, session; uint8_t mods, key; bool held, mouse; unsigned sequence; uint16_t consumer; } click_t;
static QueueHandle_t queue;
static SemaphoreHandle_t lock;
static atomic_uint session;
static atomic_bool online, pending, running;
static unsigned owner;
static bool allowed;
static bool driver_installed;
static atomic_bool use_ble;
static unsigned ble_generation;
static bool ble_was_ready;
static bool worker_started;
static TaskHandle_t hid_task;
static atomic_bool stop_requested;
enum { REPORT_KEYBOARD=1, REPORT_MOUSE=2, REPORT_CONSUMER=3 };
static uint8_t held_mods, held_key, mouse_buttons;
static int mouse_x, mouse_y, mouse_wheel;
static bool keyboard_dirty, mouse_dirty;
static unsigned key_sequence;
static int volume_steps;
static bool backend_ready(void) { return use_ble?ble_touch_ready():tud_hid_ready(); }
static bool backend_key(uint8_t id,uint8_t mods,const uint8_t *keys) {
    if(!use_ble)return tud_hid_keyboard_report(id,mods,keys);
    uint8_t r[8]={mods};if(keys)memcpy(r+2,keys,6);return ble_touch_report(2,r);
}
static bool backend_mouse(uint8_t id,uint8_t b,int8_t x,int8_t y,int8_t w,int8_t pan) {
    if(!use_ble)return tud_hid_mouse_report(id,b,x,y,w,pan);
    int16_t r[4]={b,x,y,w};return ble_touch_report(1,r);
}
static bool consumer_report(uint16_t usage) { return use_ble?ble_touch_report(3,&usage):tud_hid_report(REPORT_CONSUMER,&usage,sizeof(usage)); }
static void clear_input(void)
{
    held_mods=held_key=mouse_buttons=0;
    mouse_x=mouse_y=mouse_wheel=0;
    keyboard_dirty=mouse_dirty=false;
    volume_steps=0;
    xQueueReset(queue);
}
static const uint8_t report_desc[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(REPORT_KEYBOARD)),
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(REPORT_MOUSE)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(REPORT_CONSUMER))
};
static const uint8_t config_desc[] = {
    TUD_CONFIG_DESCRIPTOR(1,1,0,TUD_CONFIG_DESC_LEN+TUD_HID_DESC_LEN,0,500),
    TUD_HID_DESCRIPTOR(0,4,HID_ITF_PROTOCOL_NONE,sizeof(report_desc),0x81,16,5)
};
static char serial[13];
static const char *strings[] = {(const char[]){9,4}, "TouchDesk DIY",
    "TouchDesk Keyboard Mouse", serial, "System HID"};
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { return report_desc; }
uint16_t tud_hid_get_report_cb(uint8_t i,uint8_t id,hid_report_type_t type,uint8_t *buf,uint16_t n)
{
    if(type != HID_REPORT_TYPE_INPUT) return 0;
    uint16_t size = id==REPORT_KEYBOARD ? 8 : id==REPORT_MOUSE ? 5 : id==REPORT_CONSUMER ? 2 : 0;
    uint16_t len = n < size ? n : size;
    memset(buf,0,len);
    return len;
}
void tud_hid_set_report_cb(uint8_t i,uint8_t id,hid_report_type_t type,const uint8_t *buf,uint16_t n) {}
static void connection(bool connected)
{
    /* TinyUSB invokes these callbacks in its task, not in an ISR. Serialize
     * session changes with report submission and UI ownership revocation. */
    if(lock) xSemaphoreTake(lock,portMAX_DELAY);
    atomic_store(&online,false);
    if(queue) clear_input();
    atomic_fetch_add(&session,1);
    atomic_store(&pending,true);
    atomic_store(&online,connected);
    if(lock) xSemaphoreGive(lock);
}
void tud_mount_cb(void) { connection(true); }
void tud_umount_cb(void) { connection(false); }
void tud_suspend_cb(bool remote_wakeup_en) { connection(false); }
void tud_resume_cb(void) { connection(true); }
unsigned usb_service_session(void) { return atomic_load(&session); }
bool usb_service_online(void) { return atomic_load(&running) && (use_ble?ble_touch_ready():atomic_load(&online)); }
bool usb_service_release_pending(void) { return atomic_load(&pending); }
unsigned usb_service_set_owner(bool enable)
{
    if(!lock) return 0;
    xSemaphoreTake(lock,portMAX_DELAY);
    allowed = enable;
    owner++;
    atomic_fetch_add(&session,1);
    atomic_store(&pending,true);
    clear_input();
    if(use_ble)ble_touch_cancel();
    unsigned token = owner;
    xSemaphoreGive(lock);
    return token;
}
void usb_service_cancel(void)
{
    if(!lock) return;
    xSemaphoreTake(lock,portMAX_DELAY);
    atomic_fetch_add(&session,1);
    atomic_store(&pending,true);
    clear_input();
    if(use_ble)ble_touch_cancel();
    xSemaphoreGive(lock);
}
bool usb_service_click(unsigned token,unsigned gesture,uint8_t mods,uint8_t key)
{
    if(!lock) return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool ok = allowed && token == owner && gesture == usb_service_session() && usb_service_online();
    click_t c = {.owner=token,.session=gesture,.mods=mods,.key=key};
    if(ok) ok = xQueueSend(queue,&c,0) == pdTRUE;
    xSemaphoreGive(lock);
    return ok;
}
bool usb_service_mouse_click(unsigned token,unsigned gesture)
{
    if(!lock) return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool ok=allowed && token==owner && gesture==usb_service_session() && usb_service_online();
    click_t c={.owner=token,.session=gesture,.mouse=true};
    if(ok) ok=xQueueSend(queue,&c,0)==pdTRUE;
    xSemaphoreGive(lock);
    return ok;
}
/* Desired state is independent of the bounded click queue: a release cannot
 * be dropped when the queue is full or the USB endpoint is busy. */
bool usb_service_key(unsigned token,unsigned gesture,uint8_t mods,uint8_t key)
{
    if(!lock) return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool ok=allowed && token==owner && gesture==usb_service_session() && usb_service_online();
    if(ok && key) {
        click_t c={.owner=token,.session=gesture,.mods=mods,.key=key,.held=true,.sequence=key_sequence+1};
        ok=xQueueSend(queue,&c,0)==pdTRUE;
        if(ok) { key_sequence++; held_mods=mods; held_key=key; }
    } else if(ok) {
        held_mods=held_key=0; keyboard_dirty=true;
    }
    xSemaphoreGive(lock);
    return ok;
}
bool usb_service_volume(unsigned token,unsigned gesture,int steps)
{
    if(!lock) return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool ok=allowed && token==owner && gesture==usb_service_session() && usb_service_online();
    if(ok) {
        if(steps>20) steps=20;
        if(steps< -20) steps=-20;
        volume_steps+=steps;
        if(volume_steps>20) volume_steps=20;
        if(volume_steps< -20) volume_steps=-20;
    }
    xSemaphoreGive(lock);
    return ok;
}
bool usb_service_mute(unsigned token,unsigned gesture)
{
    if(!lock) return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool ok=allowed && token==owner && gesture==usb_service_session() && usb_service_online();
    click_t c={.owner=token,.session=gesture,.consumer=HID_USAGE_CONSUMER_MUTE};
    if(ok) ok=xQueueSend(queue,&c,0)==pdTRUE;
    xSemaphoreGive(lock);
    return ok;
}
static int bounded(int n) { return n>2048 ? 2048 : n< -2048 ? -2048 : n; }
bool usb_service_mouse(unsigned token,unsigned gesture,uint8_t buttons,int x,int y,int wheel)
{
    if(!lock) return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool ok=allowed && token==owner && gesture==usb_service_session() && usb_service_online();
    if(ok) {
        mouse_buttons=buttons&7;
        mouse_x=bounded(mouse_x+bounded(x)); mouse_y=bounded(mouse_y+bounded(y));
        mouse_wheel=bounded(mouse_wheel+bounded(wheel)); mouse_dirty=true;
    }
    xSemaphoreGive(lock);
    return ok;
}

typedef struct {
    unsigned seen;
    bool releasing, release_mouse, release_consumer, tapping, consuming;
    TickType_t release_at, tap_release_at, consumer_release_at;
} worker_state_t;
/* One bounded non-blocking HID iteration, also used by the host regression tests. */
static void worker_step(worker_state_t *s)
{
        xSemaphoreTake(lock,portMAX_DELAY);
        if(use_ble) {
            ble_mouse_snapshot_t snap;ble_mouse_snapshot(&snap);
            bool ready=ble_touch_ready();
            if(ready!=ble_was_ready || snap.reconnections!=ble_generation) {
                ble_was_ready=ready;ble_generation=snap.reconnections;
                clear_input();ble_touch_cancel();atomic_fetch_add(&session,1);atomic_store(&pending,true);
            }
        }
        unsigned now = usb_service_session();
        if(s->seen != now) {
            s->seen = now;
            s->releasing = true;
            s->release_mouse = true;
            s->release_consumer = true;
            s->tapping = false;
            s->consuming = false;
            s->release_at = xTaskGetTickCount();
        }
        if(usb_service_online() && backend_ready()) {
            if(s->releasing) {
                if((int32_t)(xTaskGetTickCount()-s->release_at) >= 0 && backend_key(REPORT_KEYBOARD,0,NULL)) {
                    s->releasing = false;
                    /* A concurrent connection callback must not be acknowledged. */
                    if(!s->release_mouse && !s->release_consumer && s->seen == usb_service_session()) atomic_store(&pending,false);
                }
            }
            if(!s->releasing && s->release_mouse && backend_ready()) {
                if(backend_mouse(REPORT_MOUSE,0,0,0,0,0)) {
                    s->release_mouse=false;
                    if(backend_ready() && consumer_report(0)) {
                        s->release_consumer=false;
                        if(s->seen==usb_service_session()) atomic_store(&pending,false);
                    }
                }
            } else if(!s->releasing && !s->release_mouse) {
                if(s->release_consumer) {
                    if(consumer_report(0)) {
                        s->release_consumer=false;
                        if(s->seen==usb_service_session()) atomic_store(&pending,false);
                    }
                } else if(s->consuming) {
                    if((int32_t)(xTaskGetTickCount()-s->consumer_release_at)>=0 && consumer_report(0)) {
                        s->consuming=false;
                        if(s->seen==usb_service_session()) atomic_store(&pending,false);
                    }
                } else if(s->tapping) {
                    if((int32_t)(xTaskGetTickCount()-s->tap_release_at)>=0 &&
                       backend_mouse(REPORT_MOUSE,mouse_buttons,0,0,0,0)) s->tapping=false;
                } else if(keyboard_dirty) {
                    if(backend_key(REPORT_KEYBOARD,0,NULL)) keyboard_dirty=false;
                } else if(mouse_dirty) {
                    int x=mouse_x>127?127:mouse_x< -127?-127:mouse_x;
                    int y=mouse_y>127?127:mouse_y< -127?-127:mouse_y;
                    int w=mouse_wheel>127?127:mouse_wheel< -127?-127:mouse_wheel;
                    if(backend_mouse(REPORT_MOUSE,mouse_buttons,x,y,w,0)) {
                        mouse_x-=x; mouse_y-=y; mouse_wheel-=w;
                        mouse_dirty=mouse_x||mouse_y||mouse_wheel;
                    }
                } else {
                click_t c;
                if(xQueuePeek(queue,&c,0) == pdTRUE) {
                    if(!allowed || c.owner != owner || c.session != now) xQueueReceive(queue,&c,0);
                    else if(c.consumer) {
                        if(consumer_report(c.consumer)) {
                            xQueueReceive(queue,&c,0); s->consuming=true;
                            s->consumer_release_at=xTaskGetTickCount()+pdMS_TO_TICKS(40);
                            atomic_store(&pending,true);
                        }
                    } else if(c.mouse) {
                        if(backend_mouse(REPORT_MOUSE,mouse_buttons|1,0,0,0,0)) {
                            xQueueReceive(queue,&c,0); s->tapping=true;
                            s->tap_release_at=xTaskGetTickCount()+pdMS_TO_TICKS(40);
                        }
                    } else if(backend_key(REPORT_KEYBOARD,c.mods,(uint8_t[6]){c.key})) {
                        xQueueReceive(queue,&c,0);
                        /* Preserve a short tap even if it was already lifted
                         * while the endpoint was busy. An actively held key
                         * instead waits for the explicit, priority release. */
                        if(!c.held || c.sequence!=key_sequence || !held_key) {
                            s->releasing = true;
                            atomic_store(&pending,true);
                            s->release_at = xTaskGetTickCount()+pdMS_TO_TICKS(40);
                        }
                    }
                } else if(volume_steps) {
                    int direction=volume_steps>0?1:-1;
                    if(consumer_report(direction>0?HID_USAGE_CONSUMER_VOLUME_INCREMENT:HID_USAGE_CONSUMER_VOLUME_DECREMENT)) {
                        volume_steps-=direction; s->consuming=true;
                        s->consumer_release_at=xTaskGetTickCount()+pdMS_TO_TICKS(40);
                        atomic_store(&pending,true);
                    }
                }
                }
            }
        }
        xSemaphoreGive(lock);
}
static void worker(void *arg)
{
    worker_state_t state={.seen=~0u,.releasing=true};
    for(;;) {
        if(atomic_load(&stop_requested)) { for(;;)vTaskSuspend(NULL); }
        worker_step(&state);
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
static esp_err_t install_driver(void)
{
    tinyusb_config_t cfg = {.device_descriptor=NULL,.string_descriptor=strings,.string_descriptor_count=5,
        .external_phy=false,.configuration_descriptor=config_desc};
    esp_err_t e=tinyusb_driver_install(&cfg);
    if(e==ESP_OK)driver_installed=true;
    return e;
}
esp_err_t usb_service_stop(void)
{
    if(use_ble) {
        usb_service_set_owner(false);ble_touch_cancel();
        vTaskDelay(pdMS_TO_TICKS(50));
        atomic_store(&running,false);
        esp_err_t e=ble_mouse_deinit();if(e!=ESP_OK)return e;
        use_ble=false;atomic_store(&running,false);return ESP_OK;
    }
    if(!driver_installed) return ESP_OK;
    usb_service_set_owner(false);
    /* Give the worker a bounded chance to release a pressed key before detach. */
    for(unsigned i=0;i<25 && usb_service_online() && atomic_load(&pending);i++)
        vTaskDelay(pdMS_TO_TICKS(2));
    xSemaphoreTake(lock,portMAX_DELAY);
    atomic_store(&running,false);
    atomic_store(&online,false);
    atomic_fetch_add(&session,1);
    clear_input();
    xSemaphoreGive(lock);
    esp_err_t e=tinyusb_driver_uninstall();
    if(e==ESP_OK)driver_installed=false;
    return e;
}
esp_err_t usb_service_start(void)
{
    if(atomic_load(&running)) return ESP_OK;
    if(driver_installed)return ESP_ERR_INVALID_STATE;
    if(!worker_started) return usb_service_init();
    atomic_store(&online,false);
    atomic_store(&pending,true);
    esp_err_t err=install_driver();
    if(err==ESP_OK) atomic_store(&running,true);
    return err;
}
esp_err_t usb_service_init(void)
{
    if(worker_started)return usb_service_start();
    if(lock||queue||driver_installed)return ESP_ERR_INVALID_STATE;
    atomic_store(&stop_requested,false);
    lock = xSemaphoreCreateMutex();
    queue = xQueueCreate(8,sizeof(click_t));
    if(!lock || !queue) {
        if(queue) vQueueDelete(queue);
        if(lock) vSemaphoreDelete(lock);
        queue=NULL; lock=NULL;
        return ESP_ERR_NO_MEM;
    }
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac,ESP_MAC_WIFI_STA);
    if(err != ESP_OK) goto failed;
    snprintf(serial,sizeof(serial),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    atomic_store(&pending,true);
    err = install_driver();
    if(err != ESP_OK) goto failed;
    atomic_store(&running,true);
    if(xTaskCreate(worker,"system_hid",4096,NULL,5,&hid_task) == pdPASS) { worker_started=true; return ESP_OK; }
    atomic_store(&running,false);
    if(tinyusb_driver_uninstall()==ESP_OK)driver_installed=false;
    err=ESP_ERR_NO_MEM;
failed:
    if(driver_installed)return err; /* Retain callback resources until uninstall succeeds. */
    vQueueDelete(queue); vSemaphoreDelete(lock);
    queue=NULL; lock=NULL;
    return err;
}

esp_err_t usb_service_deinit(void) {
 esp_err_t e=usb_service_stop();if(e!=ESP_OK)return e;
 atomic_store(&stop_requested,true);e=service_task_join(&hid_task,3000);if(e!=ESP_OK)return e;
 worker_started=false;
 if(queue) { vQueueDelete(queue); }
 queue=NULL;
 if(lock) { vSemaphoreDelete(lock); }
 lock=NULL;return ESP_OK;
}

bool usb_service_is_ble(void) { return use_ble; }
esp_err_t usb_service_transport(bool ble) {
    if(ble==use_ble && atomic_load(&running))return ESP_OK;
    esp_err_t e=usb_service_stop();if(e!=ESP_OK)return e;
    if(ble) {
        e=ble_mouse_init();
        if(e==ESP_OK){use_ble=true;ble_was_ready=false;ble_generation=0;atomic_store(&running,true);}
        else {
            esp_err_t cleanup=ble_mouse_deinit();
            if(cleanup==ESP_OK)usb_service_start();
            else {use_ble=true;atomic_store(&running,false);}
        }
    } else e=usb_service_start();
    return e;
}
void usb_service_pair(bool forget) {
    if(!use_ble)return;
    usb_service_cancel();if(forget)ble_mouse_forget();else ble_mouse_pair();
}
const char *usb_service_connection_text(void) {
    if(!use_ble)return usb_service_online()?"USB: connected":"USB: disconnected";
    ble_mouse_snapshot_t s;ble_mouse_snapshot(&s);
    if(ble_touch_ready())return "BLE: connected";
    if(s.connected)return s.encrypted?"BLE: enabling HID":"BLE: encrypting";
    return s.pairing?"BLE: pairing (120s)":s.advertising?"BLE: reconnecting":"BLE: disconnected";
}
