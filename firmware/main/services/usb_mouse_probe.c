/* Dedicated USB-to-BLE mouse test application with hub diagnostics.
 * Device HID, Wi-Fi, audio and normal applications remain inactive. */
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "board.h"
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
static struct {
    unsigned devices,hubs,interfaces,reports,errors,disconnects,buttons,changes,down[3],up[3];
    unsigned key_reports,key_interfaces,wheel_events;
    int64_t x,y,wheel;
    size_t len;
    uint8_t raw[16];
} state;
typedef struct { hid_host_device_handle_t h; bool boot,keyboard,report; mouse_report_map_t map; } slot_t;
static slot_t slots[8];
static QueueHandle_t arrivals;
static usb_host_client_handle_t observer;

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
    while(xQueueReceive(arrivals,&h,portMAX_DELAY)==pdTRUE) {
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
static void observer_event(const usb_host_client_event_msg_t *m,void *arg)
{
    if(m->event!=USB_HOST_CLIENT_EVENT_NEW_DEV)return;
    usb_device_handle_t dev;
    esp_err_t err=usb_host_device_open(observer,m->new_dev.address,&dev);
    if(err!=ESP_OK){ESP_LOGW(TAG,"Observer open: %s",esp_err_to_name(err));return;}
    usb_device_info_t info={0};const usb_device_desc_t *d=NULL;
    err=usb_host_device_info(dev,&info);
    esp_err_t de=usb_host_get_device_descriptor(dev,&d);
    if(err==ESP_OK&&de==ESP_OK&&d) {
        const usb_config_desc_t *cfg=NULL;
        usb_host_get_active_config_descriptor(dev,&cfg);
        ESP_LOGI(TAG,"ENUM addr=%u VID:PID=%04x:%04x speed=%s class=%02x config_bytes=%u interfaces=%u",
            m->new_dev.address,d->idVendor,d->idProduct,info.speed==USB_SPEED_LOW?"LOW":"FULL",d->bDeviceClass,
            cfg?cfg->wTotalLength:0,cfg?cfg->bNumInterfaces:0);
        portENTER_CRITICAL(&lock);state.devices++;if(d->bDeviceClass==9)state.hubs++;portEXIT_CRITICAL(&lock);
    }
    usb_host_device_close(observer,dev);
}
static void observer_task(void *arg)
{
    usb_host_client_config_t c={.is_synchronous=false,.max_num_event_msg=12,
        .async={.client_event_callback=observer_event,.callback_arg=NULL}};
    ESP_ERROR_CHECK(usb_host_client_register(&c,&observer));
    for(;;)usb_host_client_handle_events(observer,pdMS_TO_TICKS(100));
}
static void host_daemon(void *arg)
{
    for(;;){uint32_t flags=0;usb_host_lib_handle_events(pdMS_TO_TICKS(100),&flags);}
}
static void pairing_button(lv_event_t *e){if(lv_event_get_code(e)==LV_EVENT_CLICKED)ble_mouse_pair();}
static void forget_button(lv_event_t *e){if(lv_event_get_code(e)==LV_EVENT_LONG_PRESSED)ble_mouse_forget();}
void usb_mouse_probe_run(void)
{
    ESP_LOGI(TAG,"USB BLE KEYMOUSE v1; official ext_hub backport 858947e; UART 921600");
    ESP_LOGI(TAG,"Hubs=ON multi-level=ON experimental low-speed-via-hub=ON control-buffer=2048");
    lv_init();board_init();board_backlight(true);
    lv_obj_set_style_bg_color(lv_scr_act(),lv_color_hex(0x101b29),0);
    lv_obj_t *label=lv_label_create(lv_scr_act());
    lv_obj_set_width(label,300);lv_obj_set_pos(label,10,12);
    lv_obj_set_style_text_color(label,lv_color_hex(0xe6f1ff),0);
    lv_obj_t *pair=lv_btn_create(lv_scr_act());lv_obj_set_pos(pair,10,358);lv_obj_set_size(pair,145,44);
    lv_obj_add_event_cb(pair,pairing_button,LV_EVENT_CLICKED,NULL);
    lv_obj_t *pl=lv_label_create(pair);lv_label_set_text(pl,"Pair (120s)");lv_obj_center(pl);
    lv_obj_t *forget=lv_btn_create(lv_scr_act());lv_obj_set_pos(forget,165,358);lv_obj_set_size(forget,145,44);
    lv_obj_add_event_cb(forget,forget_button,LV_EVENT_LONG_PRESSED,NULL);
    lv_obj_t *fl=lv_label_create(forget);lv_label_set_text(fl,"Hold to forget");lv_obj_center(fl);
    lv_obj_t *hint=lv_label_create(lv_scr_act());lv_obj_set_width(hint,300);lv_obj_set_pos(hint,10,416);
    lv_obj_set_style_text_color(hint,lv_color_hex(0xe6f1ff),0);
    lv_label_set_text(hint,"PC Bluetooth: TouchDesk KeyMouse\nKeyboard / mouse / wheel\nRe-pair after profile upgrade");
    esp_err_t ble_err=ble_mouse_init();
    if(ble_err!=ESP_OK)ESP_LOGE(TAG,"BLE init=%s; USB diagnostic remains available",esp_err_to_name(ble_err));
    arrivals=xQueueCreate(16,sizeof(hid_host_device_handle_t));
    ESP_ERROR_CHECK(arrivals?ESP_OK:ESP_ERR_NO_MEM);
    usb_host_config_t hc={.skip_phy_setup=false,.intr_flags=ESP_INTR_FLAG_LEVEL1};
    ESP_ERROR_CHECK(usb_host_install(&hc));
    ESP_ERROR_CHECK(xTaskCreate(host_daemon,"probe_host",4096,NULL,6,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(observer_task,"probe_enum",4096,NULL,5,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(xTaskCreate(hid_worker,"probe_hid",6144,NULL,5,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM);
    hid_host_driver_config_t hid={.create_background_task=true,.task_priority=5,.stack_size=6144,
        .core_id=tskNO_AFFINITY,.callback=arrived};
    ESP_ERROR_CHECK(hid_host_install(&hid));
    int64_t last_ui=-1000000,last_log=-2000000;
    for(;;) {
        int64_t now=esp_timer_get_time();
        if(now-last_ui>=100000) {
            typeof(state) snap;
            portENTER_CRITICAL(&lock);snap=state;portEXIT_CRITICAL(&lock);
            char raw[49]={0};for(size_t i=0;i<snap.len&&i<16;i++)snprintf(raw+3*i,4,"%02X ",snap.raw[i]);
            ble_mouse_snapshot_t b;ble_mouse_snapshot(&b);
            const char *link=ble_err!=ESP_OK?"INIT ERROR":b.ready?"READY":b.connected?"Securing / subscribing":b.advertising?"Advertising":"Idle - press Pair";
            lv_label_set_text_fmt(label,"USB -> BLE KEYBOARD / MOUSE\nTouchDesk KeyMouse\n\nMouse BLE: %s\nKeyboard BLE: %s\nPairing: %s   Encrypted: %s\nUSB interfaces / keyboards: %u / %u\nUSB mouse / key reports: %u / %u\nWheel total / events: %lld / %u\nMouse sent / failed: %u / %u\nKey sent / failed: %u / %u\nQueue mouse / key: %u / %u\nL/R/M: %u/%u/%u\nUSB unplug / errors: %u / %u\nInternal free: %u\n\nRelease keys after reconnect.",
                link,b.keyboard_ready?"READY":"Waiting / re-pair",b.pairing?"OPEN":"closed",b.encrypted?"yes":"no",snap.interfaces,snap.key_interfaces,
                snap.reports,snap.key_reports,(long long)snap.wheel,snap.wheel_events,b.sent,b.failed,b.key_sent,b.key_failed,b.queued,b.key_queued,
                !!(snap.buttons&1),!!(snap.buttons&2),!!(snap.buttons&4),snap.disconnects,snap.errors,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            if(now-last_log>=2000000) {
                ESP_LOGI(TAG,"STAT enum=%u hubs=%u hid=%u reports=%u x=%lld y=%lld buttons=%u changes=%u disconnects=%u errors=%u raw_len=%u raw=%s internal_free=%u internal_min=%u L=%u/%u R=%u/%u M=%u/%u",
                    snap.devices,snap.hubs,snap.interfaces,snap.reports,(long long)snap.x,(long long)snap.y,snap.buttons,snap.changes,snap.disconnects,snap.errors,(unsigned)snap.len,raw,
                    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                    snap.down[0],snap.up[0],snap.down[1],snap.up[1],snap.down[2],snap.up[2]);
                ESP_LOGI(TAG,"BLE connected=%u encrypted=%u ready=%u pair=%u advertising=%u sent=%u completed=%u failed=%u queue=%u dropped=%u resync=%u interval=%u",
                    b.connected,b.encrypted,b.ready,b.pairing,b.advertising,b.sent,b.completed,b.failed,b.queued,b.dropped,b.overflows,b.interval);
                ESP_LOGI(TAG,"KEYBOARD usb_interfaces=%u reports=%u ready=%u sent=%u completed=%u failed=%u queue=%u dropped=%u overflow=%u rollover=%u WHEEL total=%lld events=%u",
                    snap.key_interfaces,snap.key_reports,b.keyboard_ready,b.key_sent,b.key_completed,b.key_failed,b.key_queued,b.key_dropped,b.key_overflows,b.key_rollovers,(long long)snap.wheel,snap.wheel_events);
                last_log=now;
            }
            last_ui=now;
        }
        lv_timer_handler();vTaskDelay(pdMS_TO_TICKS(5));
    }
}
