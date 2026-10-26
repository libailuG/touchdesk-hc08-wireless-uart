#include "app.h"
#include "ui.h"
#include "ble_mouse.h"
#include "keymouse_service.h"
#include "music_service.h"
typedef struct {lv_obj_t *status,*counts;} view_t;
static void pair(lv_event_t *e){if(lv_event_get_code(e)==LV_EVENT_CLICKED)ble_mouse_pair();}
static void forget(lv_event_t *e){if(lv_event_get_code(e)==LV_EVENT_LONG_PRESSED)ble_mouse_forget();}
static void refresh(lv_timer_t *t)
{
    app_ctx_t *ctx=t->user_data;view_t *v=ctx->state;
    ble_mouse_snapshot_t b;ble_mouse_snapshot(&b);
    keymouse_snapshot_t s;keymouse_snapshot(&s);
    lv_label_set_text_fmt(v->status,"TouchDesk KeyMouse\n%s\nUSB 鼠标：%s　键盘：%s\n蓝牙鼠标：%s　键盘：%s\n配对窗口：%s",
        music_host_mode()?"USB Host 模式":"请重新打开本应用切换 USB 模式",
        s.interfaces>s.key_interfaces?"已连接":"未连接",s.key_interfaces?"已连接":"未连接",
        b.ready?"就绪":"等待",b.keyboard_ready?"就绪":"等待",b.pairing?"已开放":"已关闭");
    lv_label_set_text_fmt(v->counts,"鼠标 / 键盘报告：%u / %u\n滚轮事件：%u　USB 错误：%u\n蓝牙发送失败：%u / %u",s.reports,s.key_reports,s.wheel_events,s.errors,b.failed,b.key_failed);
}
static esp_err_t create(app_ctx_t *ctx,lv_obj_t *root)
{
    view_t *v=ctx->state;
    v->status=ui_label(root,"",12,4,296);
    v->counts=ui_label(root,"",12,150,296);
    ui_button(root,"开放配对",12,230,142,44,pair,NULL);
    lv_obj_t *f=ui_button(root,"长按忘记",166,230,142,44,NULL,NULL);
    lv_obj_add_event_cb(f,forget,LV_EVENT_LONG_PRESSED,NULL);
    ui_label(root,"扩展坞连接键盘和鼠标\n返回主页后仍可使用",12,290,296);
    lv_timer_t *t=app_timer_create(ctx,refresh,250);if(t)refresh(t);
    return t?ESP_OK:ESP_ERR_NO_MEM;
}
const app_desc_t app_keymouse={.id="keymouse",.title="键鼠桥接",.subtitle="USB 键鼠转蓝牙",
    .caps=APP_CAP_USB_HOST_HID,.state_size=sizeof(view_t),.create=create};
