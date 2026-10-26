/* HID class client sharing the application USB Host with UAC audio. */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdatomic.h>
#include "keymouse_service.h"
#include "service_task.h"
#include "ble_mouse.h"
#include "mouse_report.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "usb/usb_host.h"
#include "usb/hid_host.h"

static const char *TAG="mouse_probe";
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static keymouse_snapshot_t state;
typedef struct { hid_host_device_handle_t h; bool boot,keyboard,report; mouse_report_map_t map; } slot_t;
static slot_t slots[8];
static QueueHandle_t arrivals;
static bool initialized,installed;
static esp_err_t init_result=ESP_ERR_INVALID_STATE;
static StaticTask_t worker_control;
static TaskHandle_t worker_task;
static StackType_t *worker_stack;
static atomic_bool shutdown;
static atomic_bool stopping,worker_idle;

static void report_event(hid_host_device_handle_t h,hid_host_interface_event_t e,void *arg)
{
    slot_t *slot=arg;
    if(e==HID_HOST_INTERFACE_EVENT_INPUT_REPORT) {
        uint8_t data[64]; size_t n=0;
        esp_err_t err=hid_host_device_get_raw_input_report_data(h,data,sizeof(data),&n);
        if(slot->keyboard) {
            portENTER_CRITICAL(&lock);if(err==ESP_OK&&n==8)state.key_reports++;else state.errors++;portEXIT_CRITICAL(&lock);
            if(err==ESP_OK&&n==8)ble_keyboard_input(data);
            return; /* Never print typed keys or store them in mouse RAW diagnostics. */
        }
        uint8_t previous;
        portENTER_CRITICAL(&lock);previous=state.buttons;portEXIT_CRITICAL(&lock);
        mouse_decoded_t m={0};bool decoded=false;
        if(err==ESP_OK) {
            if(slot->report)decoded=mouse_report_decode(&slot->map,data,n,previous,&m);
            else if(slot->boot&&n>=3){m=(mouse_decoded_t){.buttons=data[0]&7,.x=(int8_t)data[1],.y=(int8_t)data[2]};decoded=true;}
        }
        portENTER_CRITICAL(&lock);
        if(err==ESP_OK) {
            state.reports++;state.len=n;
            memset(state.raw,0,sizeof(state.raw));
            memcpy(state.raw,data,n<sizeof(state.raw)?n:sizeof(state.raw));
            if(decoded) {
                state.x+=m.x;state.y+=m.y;state.wheel+=m.wheel;if(m.wheel)state.wheel_events++;
                if(state.buttons!=m.buttons)state.changes++;
                for(unsigned i=0;i<3;i++) {
                    unsigned bit=1u<<i;
                    if(!(state.buttons&bit)&&(m.buttons&bit))state.down[i]++;
                    if((state.buttons&bit)&&!(m.buttons&bit))state.up[i]++;
                }
                state.buttons=m.buttons;
            }
        } else state.errors++;
        portEXIT_CRITICAL(&lock);
        if(decoded)ble_mouse_wheel_input(m.buttons,m.x,m.y,m.wheel);
    } else if(e==HID_HOST_INTERFACE_EVENT_DISCONNECTED) {
        if(slot->keyboard)ble_keyboard_usb_gone();else ble_mouse_usb_gone();
        esp_err_t err=hid_host_device_close(h);
        portENTER_CRITICAL(&lock);
        state.disconnects++;if(!slot->keyboard)state.buttons=0;
        if(slot->keyboard&&state.key_interfaces)state.key_interfaces--;
        if(state.interfaces)state.interfaces--;
        slot->h=NULL;slot->boot=slot->report=false;
        if(err!=ESP_OK)state.errors++;
        portEXIT_CRITICAL(&lock);
        ESP_LOGI(TAG,"HID disconnected, close=%s",esp_err_to_name(err));
    } else if(e==HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR) {
        portENTER_CRITICAL(&lock);state.errors++;portEXIT_CRITICAL(&lock);
        ESP_LOGW(TAG,"HID transfer error");
    }
}
static void arrived(hid_host_device_handle_t h,hid_host_driver_event_t e,void *arg)
{
    if(e==HID_HOST_DRIVER_EVENT_CONNECTED&&xQueueSend(arrivals,&h,0)!=pdTRUE) {
        portENTER_CRITICAL(&lock);state.errors++;portEXIT_CRITICAL(&lock);
        ESP_LOGE(TAG,"HID arrivals queue full; reconnect mouse");
    }
}
static void hid_worker(void *arg)
{
    hid_host_device_handle_t h;
    for(;;) {
        if(atomic_load(&shutdown)) { for(;;)vTaskSuspend(NULL); }
        if(xQueueReceive(arrivals,&h,pdMS_TO_TICKS(30))!=pdTRUE)continue;
        if(!h){atomic_store(&worker_idle,true);continue;}
        if(atomic_load(&stopping))continue;
        hid_host_dev_params_t p;
        esp_err_t err=hid_host_device_get_params(h,&p);
        if(err!=ESP_OK){ESP_LOGW(TAG,"Device gone before open");continue;}
        ESP_LOGI(TAG,"HID addr=%u interface=%u subclass=%u protocol=%u",p.addr,p.iface_num,p.sub_class,p.proto);
        bool keyboard=p.proto==HID_PROTOCOL_KEYBOARD;
        if((p.proto!=HID_PROTOCOL_MOUSE&&!keyboard)||p.sub_class!=HID_SUBCLASS_BOOT_INTERFACE){ESP_LOGI(TAG,"Non-BOOT keyboard/mouse interface skipped (media/vendor not supported)");continue;}
        bool occupied=false;
        portENTER_CRITICAL(&lock);for(unsigned i=0;i<8;i++)if(slots[i].h&&slots[i].keyboard==keyboard)occupied=true;portEXIT_CRITICAL(&lock);
        if(occupied){ESP_LOGW(TAG,"Only one keyboard and one mouse supported");continue;}
        slot_t *s=NULL;
        portENTER_CRITICAL(&lock);
        for(unsigned i=0;i<8;i++)if(!slots[i].h){s=&slots[i];s->h=h;s->boot=s->report=false;s->keyboard=keyboard;break;}
        portEXIT_CRITICAL(&lock);
        if(!s){ESP_LOGE(TAG,"No free HID slots");continue;}
        hid_host_device_config_t cfg={.callback=report_event,.callback_arg=s};
        err=hid_host_device_open(h,&cfg);
        if(err!=ESP_OK){s->h=NULL;ESP_LOGW(TAG,"open=%s",esp_err_to_name(err));continue;}
        size_t desc_len=0;
        uint8_t *desc=hid_host_get_report_descriptor(h,&desc_len);
        ESP_LOGI(TAG,"Report descriptor bytes=%u",(unsigned)desc_len);
        if(desc&&desc_len)ESP_LOG_BUFFER_HEX_LEVEL(TAG,desc,desc_len,ESP_LOG_INFO);
        if(!keyboard&&desc&&mouse_report_parse(&s->map,desc,desc_len)) {
            err=hid_class_request_set_protocol(h,HID_REPORT_PROTOCOL_REPORT);s->report=err==ESP_OK;
            ESP_LOGI(TAG,"Set REPORT mouse=%s; parsed fields=%u",esp_err_to_name(err),s->map.count);
        }
        if(!s->report) {
            err=hid_class_request_set_protocol(h,HID_REPORT_PROTOCOL_BOOT);
            s->boot=(err==ESP_OK);
            ESP_LOGI(TAG,"Set BOOT %s=%s",keyboard?"keyboard":"mouse",esp_err_to_name(err));
        }
        if(!s->boot&&!s->report){hid_host_device_close(h);s->h=NULL;ESP_LOGW(TAG,"Supported protocol required; input disabled");continue;}
        portENTER_CRITICAL(&lock);state.interfaces++;if(keyboard)state.key_interfaces++;portEXIT_CRITICAL(&lock);
        err=hid_host_device_start(h);
        ESP_LOGI(TAG,"Input start=%s; decoding=%s",esp_err_to_name(err),keyboard?"BOOT keyboard":s->report?"REPORT mouse + wheel":"BOOT mouse");
        if(err!=ESP_OK) {
            hid_host_device_close(h);
            portENTER_CRITICAL(&lock);state.errors++;if(state.interfaces)state.interfaces--;if(keyboard&&state.key_interfaces)state.key_interfaces--;s->h=NULL;portEXIT_CRITICAL(&lock);
        }
    }
}

esp_err_t keymouse_init(void)
{
    if(initialized)return init_result;
    atomic_store(&shutdown,false);
    atomic_store(&stopping,false);atomic_store(&worker_idle,false);
    arrivals=xQueueCreate(16,sizeof(hid_host_device_handle_t));
    if(!arrivals)return init_result=ESP_ERR_NO_MEM;
    worker_stack=heap_caps_malloc(6144,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!worker_stack){vQueueDelete(arrivals);arrivals=NULL;return init_result=ESP_ERR_NO_MEM;}
    worker_task=xTaskCreateStaticPinnedToCore(hid_worker,"keymouse_hid",6144,NULL,5,worker_stack,&worker_control,tskNO_AFFINITY);
    if(!worker_task) { heap_caps_free(worker_stack);worker_stack=NULL;vQueueDelete(arrivals);arrivals=NULL;return init_result=ESP_ERR_NO_MEM; }
    initialized=true;
    init_result=ble_mouse_init();
    return init_result;
}
esp_err_t keymouse_host_start(void)
{
    if(init_result!=ESP_OK)return init_result;
    atomic_store(&stopping,false);
    if(installed)return ESP_OK;
    hid_host_driver_config_t cfg={.create_background_task=true,.task_priority=5,.stack_size=6144,
        .core_id=tskNO_AFFINITY,.callback=arrived};
    esp_err_t err=hid_host_install(&cfg);
    if(err==ESP_OK)installed=true;
    ESP_LOGI(TAG,"Shared HOST HID start=%s",esp_err_to_name(err));
    return err;
}
/* Quiesce opens, then stop/release claimed endpoints while the port is still
 * present. Close invokes DISCONNECTED synchronously; our callback performs the
 * driver's second close and clears the slot. The later port-gone event only
 * has unclaimed interfaces and device contexts left to clean up. */
esp_err_t keymouse_host_prepare_stop(void)
{
    if(!installed)return ESP_OK;
    atomic_store(&stopping,true);atomic_store(&worker_idle,false);
    hid_host_device_handle_t barrier=NULL;
    if(xQueueSend(arrivals,&barrier,pdMS_TO_TICKS(100))!=pdTRUE)return ESP_ERR_TIMEOUT;
    for(unsigned i=0;i<500&&!atomic_load(&worker_idle);i++)vTaskDelay(pdMS_TO_TICKS(10));
    if(!atomic_load(&worker_idle))return ESP_ERR_TIMEOUT;
    ble_mouse_usb_gone();ble_keyboard_usb_gone();
    for(unsigned i=0;i<8;i++) {
        portENTER_CRITICAL(&lock);hid_host_device_handle_t h=slots[i].h;portEXIT_CRITICAL(&lock);
        if(!h)continue;
        esp_err_t err=hid_host_device_close(h);
        if(err!=ESP_OK&&err!=ESP_ERR_INVALID_ARG)return err;
    }
    ESP_LOGI(TAG,"HID interfaces gracefully closed before port stop");
    return ESP_OK;
}
/* Caller disables the root port after prepare_stop(). Normal disconnects
 * release unclaimed interfaces/device contexts before driver uninstall. */
esp_err_t keymouse_host_stop(void)
{
    if(!installed)return ESP_OK;
    ble_mouse_usb_gone();ble_keyboard_usb_gone();
    esp_err_t err=ESP_ERR_INVALID_STATE;
    for(unsigned i=0;i<500;i++) {
        err=hid_host_uninstall();
        if(err==ESP_OK){installed=false;break;}
        if(err!=ESP_ERR_INVALID_STATE)break;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    ESP_LOGI(TAG,"Shared HOST HID stop=%s",esp_err_to_name(err));
    return err;
}
void keymouse_snapshot(keymouse_snapshot_t *out)
{
    portENTER_CRITICAL(&lock);*out=state;portEXIT_CRITICAL(&lock);
}
void keymouse_poll(void)
{
    static int64_t last;
    int64_t now=esp_timer_get_time();if(now-last<2000000)return;last=now;
    if(!initialized)return;
    keymouse_snapshot_t s;keymouse_snapshot(&s);
    ble_mouse_snapshot_t b;ble_mouse_snapshot(&b);
    ESP_LOGI("keymouse","USB interfaces=%u keyboards=%u mouse_reports=%u key_reports=%u wheel_events=%u errors=%u disconnects=%u; BLE encrypted=%u mouse_ready=%u key_ready=%u sent=%u/%u failed=%u/%u queue=%u/%u dropped=%u/%u internal=%u",
        s.interfaces,s.key_interfaces,s.reports,s.key_reports,s.wheel_events,s.errors,s.disconnects,
        b.encrypted,b.ready,b.keyboard_ready,b.sent,b.key_sent,b.failed,b.key_failed,b.queued,b.key_queued,b.dropped,b.key_dropped,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

esp_err_t keymouse_deinit(void) {
 if(installed)return ESP_ERR_INVALID_STATE; /* Shared Host must finish teardown first. */
 if(!initialized)return ESP_OK;
 atomic_store(&shutdown,true);esp_err_t e=service_task_join(&worker_task,5000);if(e!=ESP_OK)return e;
 e=ble_mouse_deinit();if(e!=ESP_OK)return e;
 heap_caps_free(worker_stack);worker_stack=NULL;vQueueDelete(arrivals);arrivals=NULL;
 initialized=false;init_result=ESP_ERR_INVALID_STATE;memset(slots,0,sizeof(slots));memset(&state,0,sizeof(state));return ESP_OK;
}
