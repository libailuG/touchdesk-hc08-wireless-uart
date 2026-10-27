#include "app.h"
#include "ui.h"
#include "shell.h"
#include "usb_service.h"
#include "class/hid/hid_device.h"
#include <string.h>
#include <stdio.h>
#include "board.h"
#include "volume_font.h"

typedef struct { const char *label; uint8_t mods,key; } shortcut_t;
#define CTRL KEYBOARD_MODIFIER_LEFTCTRL
#define SHIFT KEYBOARD_MODIFIER_LEFTSHIFT
#define ALT KEYBOARD_MODIFIER_LEFTALT
#define WIN KEYBOARD_MODIFIER_LEFTGUI
#define KEYS_PER_PAGE 8u
#define FIRST_PAGE_KEYS 6u
static const shortcut_t keys[]={
    {"桌面向左\nWin+Ctrl+Left",WIN|CTRL,HID_KEY_ARROW_LEFT},
    {"桌面向右\nWin+Ctrl+Right",WIN|CTRL,HID_KEY_ARROW_RIGHT},
    {"显示桌面\nWin+D",WIN,HID_KEY_D},{"任务视图\nWin+Tab",WIN,HID_KEY_TAB},
    {"复制\nCtrl+C",CTRL,HID_KEY_C},{"粘贴\nCtrl+V",CTRL,HID_KEY_V},
    {"剪切\nCtrl+X",CTRL,HID_KEY_X},{"全选\nCtrl+A",CTRL,HID_KEY_A},
    {"重做\nCtrl+Y",CTRL,HID_KEY_Y},{"查找\nCtrl+F",CTRL,HID_KEY_F},
    {"新建\nCtrl+N",CTRL,HID_KEY_N},{"打开\nCtrl+O",CTRL,HID_KEY_O},
    {"打印\nCtrl+P",CTRL,HID_KEY_P},{"关闭标签\nCtrl+W",CTRL,HID_KEY_W},
    {"新建标签\nCtrl+T",CTRL,HID_KEY_T},{"恢复标签\nCtrl+Shift+T",CTRL|SHIFT,HID_KEY_T},
    {"切换窗口\nAlt+Tab",ALT,HID_KEY_TAB},{"取消\nEsc",0,HID_KEY_ESCAPE},
    {"撤销\nCtrl+Z",CTRL,HID_KEY_Z},{"保存\nCtrl+S",CTRL,HID_KEY_S}
};
#define KEY_COUNT (sizeof(keys)/sizeof(keys[0]))
#define PAGE_COUNT (1+(KEY_COUNT-FIRST_PAGE_KEYS+KEYS_PER_PAGE-1)/KEYS_PER_PAGE)
typedef enum { KEY, MOD, SHORTCUT, NAV, PAD, WHEEL, MOUSE_BUTTON, DRAG, RELEASE, VOLUME_DOWN, VOLUME_UP, MUTE, TRANSPORT, PAIR, FORGET } action_t;
typedef struct {
    app_ctx_t *ctx;
    action_t action;
    unsigned value, session;
    uint8_t mods;
    bool armed;
    lv_obj_t *obj;
} binding_t;
typedef struct {
    unsigned view, keypage, page, seen, count, fault_seen;
    uint8_t mods, buttons;
    bool rebuild, dragging, tracking, moved;
    lv_point_t last, origin;
    uint32_t started;
    int scroll;
    lv_obj_t *body, *status;
    binding_t bindings[64];
} state_t;
static void render(app_ctx_t *ctx);
static void indicators(state_t *s)
{
    for(unsigned i=0;i<s->count;i++) {
        binding_t *b=&s->bindings[i];
        bool checked=(b->action==MOD && (s->mods&b->value)) || (b->action==DRAG && s->dragging) ||
            (b->action==NAV && (b->value<3 || b->value==5) && (b->value==5?3:b->value)==s->view);
        if(checked) lv_obj_add_state(b->obj,LV_STATE_CHECKED);
        else lv_obj_clear_state(b->obj,LV_STATE_CHECKED);
        if(b->action==KEY) {
            bool shift=(s->mods|b->mods)&SHIFT;
            char label[2]={0};
            if(b->value>=4 && b->value<=29) label[0]=(shift?'A':'a')+b->value-4;
            else if(b->value>=30 && b->value<=39) label[0]=(shift?"!@#$%^&*()":"1234567890")[b->value-30];
            else if(b->value>=45 && b->value<=56 && b->value!=50)
                label[0]=(shift?"_+{}|~:\"~<>?":"-=[]\\~;'`,./")[b->value-45];
            if(label[0]) { lv_label_set_text(lv_obj_get_child(b->obj,0),label); lv_obj_center(lv_obj_get_child(b->obj,0)); }
        }
    }
}
static void local_reset(state_t *s)
{
    s->mods=s->buttons=0; s->dragging=s->tracking=false; s->scroll=0;
    for(unsigned i=0;i<s->count;i++) s->bindings[i].armed=false;
    indicators(s);
}
static void cancel(app_ctx_t *ctx)
{
    state_t *s=ctx->state;
    usb_service_cancel(); s->seen=usb_service_session(); s->fault_seen=board_input_fault_epoch();
    local_reset(s); board_input_cancel();
}
static bool valid(binding_t *b)
{
    return b->armed && b->ctx->active && b->session==usb_service_session() && usb_service_online();
}
static void mouse(app_ctx_t *ctx,int x,int y,int wheel)
{
    state_t *s=ctx->state;
    if(!usb_service_mouse(ctx->hid_owner,s->seen,s->buttons,x,y,wheel)) cancel(ctx);
}
static void event(lv_event_t *e)
{
    binding_t *b=lv_event_get_user_data(e);
    app_ctx_t *ctx=b->ctx;
    state_t *s=ctx->state;
    lv_event_code_t code=lv_event_get_code(e);
    /* A fault-induced LVGL release must never become a shortcut or tap. */
    if(s->fault_seen!=board_input_fault_epoch()) { cancel(ctx); return; }
    if(code==LV_EVENT_PRESSED) {
        /* A new session can arrive between UI polling ticks. Gate that touch. */
        if(s->seen!=usb_service_session()) { s->seen=usb_service_session(); local_reset(s); board_input_cancel(); return; }
        b->session=s->seen; b->armed=true;
        if(b->action==KEY && valid(b)) {
            if(!usb_service_key(ctx->hid_owner,b->session,s->mods|b->mods,b->value)) cancel(ctx);
        } else if(b->action==MOUSE_BUTTON && valid(b)) {
            s->buttons|=b->value; mouse(ctx,0,0,0);
        } else if((b->action==PAD || b->action==WHEEL) && valid(b)) {
            lv_indev_get_point(lv_indev_get_act(),&s->last); s->origin=s->last;
            s->tracking=true; s->moved=false; s->started=lv_tick_get(); s->scroll=0;
        }
    } else if(code==LV_EVENT_PRESSING && valid(b) && s->tracking && (b->action==PAD || b->action==WHEEL)) {
        lv_point_t pt; lv_indev_get_point(lv_indev_get_act(),&pt);
        int dx=pt.x-s->last.x,dy=pt.y-s->last.y;
        int ox=pt.x-s->origin.x,oy=pt.y-s->origin.y;
        if(ox*ox+oy*oy>36) s->moved=true;
        s->last=pt;
        /* Reject implausible jumps rather than flinging the pointer. */
        if(dx>60||dx< -60||dy>60||dy< -60) { s->moved=true; return; }
        if(b->action==PAD && (dx||dy)) mouse(ctx,dx*2,dy*2,0);
        else if(b->action==WHEEL) {
            s->scroll-=dy;
            int steps=s->scroll/12;
            if(steps) { s->scroll-=steps*12; mouse(ctx,0,0,steps); }
        }
    } else if(code==LV_EVENT_PRESS_LOST) {
        if(valid(b) && (b->action==KEY || b->action==MOUSE_BUTTON || b->action==PAD || b->action==WHEEL)) cancel(ctx);
        b->armed=false; s->tracking=false;
    } else if(code==LV_EVENT_RELEASED) {
        if(b->action==KEY && valid(b)) {
            usb_service_key(ctx->hid_owner,b->session,0,0);
            s->mods=0; indicators(s);
        } else if(b->action==MOUSE_BUTTON && valid(b)) {
            s->buttons&=~b->value; if(s->dragging) s->buttons|=1;
            mouse(ctx,0,0,0);
        } else if(b->action==PAD || b->action==WHEEL) {
            if(valid(b) && b->action==PAD && s->tracking && !s->moved && !s->dragging && lv_tick_elaps(s->started)<250) {
                if(!usb_service_mouse_click(ctx->hid_owner,b->session)) cancel(ctx);
            }
            s->tracking=false;
        }
    } else if(code==LV_EVENT_CLICKED && b->armed) {
        b->armed=false;
        if(b->action==TRANSPORT || b->action==PAIR || b->action==FORGET) {
            cancel(ctx);
            if(b->action==TRANSPORT) {
                esp_err_t err=usb_service_transport(b->value!=0);
                ctx->hid_owner=usb_service_set_owner(true);
                if(err!=ESP_OK)shell_notice("连接切换失败，请重试");
            } else usb_service_pair(b->action==FORGET);
            s->seen=usb_service_session();s->rebuild=true;
        } else if(b->action==NAV || b->action==RELEASE) {
            cancel(ctx);
            if(b->action==NAV) {
                if(b->value<3 || b->value==5) s->view=b->value==5?3:b->value;
                else if(b->value==3) s->page=(s->page+1)%PAGE_COUNT;
                else s->keypage=(s->keypage+1)%3;
                s->rebuild=true;
            }
        } else if(ctx->active && b->session==usb_service_session() && usb_service_online()) {
            if(b->action==MOD) { s->mods^=b->value; indicators(s); }
            else if(b->action==VOLUME_DOWN || b->action==VOLUME_UP || b->action==MUTE) {
                bool ok=b->action==MUTE?usb_service_mute(ctx->hid_owner,b->session):
                    usb_service_volume(ctx->hid_owner,b->session,b->action==VOLUME_UP?1:-1);
                if(!ok) shell_notice("连接不可用或队列已满");
            }
            else if(b->action==SHORTCUT) {
                const shortcut_t *k=&keys[b->value];
                if(!usb_service_click(ctx->hid_owner,b->session,k->mods,k->key)) shell_notice("连接不可用或队列已满");
            } else if(b->action==DRAG) {
                s->dragging=!s->dragging; s->buttons=s->dragging?1:0;
                mouse(ctx,0,0,0); indicators(s);
            }
        }
    }
}
static lv_obj_t *button(app_ctx_t *ctx,const char *label,int x,int y,int w,int h,action_t action,unsigned value,uint8_t mods,bool ascii)
{
    state_t *s=ctx->state;
    if(s->count>=64) return NULL;
    binding_t *b=&s->bindings[s->count++];
    *b=(binding_t){.ctx=ctx,.action=action,.value=value,.mods=mods};
    lv_obj_t *o=ui_button(s->body,label,x,y,w,h,event,b); b->obj=o;
    lv_obj_set_style_radius(o,6,0);
    lv_obj_set_style_bg_color(o,lv_color_hex(0x30778c),LV_STATE_CHECKED);
    lv_obj_set_style_text_color(o,lv_color_hex(0xffffff),LV_STATE_CHECKED);
    lv_obj_t *l=lv_obj_get_child(o,0);
    lv_obj_set_width(l,w-2);
    if(ascii) lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0);
    lv_obj_center(l);
    if(action==TRANSPORT && (value!=0)==usb_service_is_ble())lv_obj_add_state(o,LV_STATE_CHECKED);
    return o;
}
static void keyrow(app_ctx_t *ctx,const char *const *labels,const uint8_t *codes,const uint8_t *mods,unsigned n,int y)
{
    int width=(316-(n-1)*3)/n, total=n*width+(n-1)*3;
    for(unsigned i=0;i<n;i++) button(ctx,labels[i],(320-total)/2+i*(width+3),y,width,40,KEY,codes[i],mods?mods[i]:0,true);
}
static void keyboard(app_ctx_t *ctx)
{
    state_t *s=ctx->state;
    const char *ml[]={"Shift","Ctrl","Alt","Win"};
    const uint8_t mv[]={SHIFT,CTRL,ALT,WIN};
    for(unsigned i=0;i<4;i++) button(ctx,ml[i],4+i*79,29,75,30,MOD,mv[i],0,true);
    if(s->keypage==0) {
        const char *r1[]={"q","w","e","r","t","y","u","i","o","p"};
        const uint8_t c1[]={20,26,8,21,23,28,24,12,18,19};
        const char *r2[]={"a","s","d","f","g","h","j","k","l"};
        const uint8_t c2[]={4,22,7,9,10,11,13,14,15};
        const char *r3[]={"z","x","c","v","b","n","m",",","."};
        const uint8_t c3[]={29,27,6,25,5,17,16,54,55};
        keyrow(ctx,r1,c1,NULL,10,65); keyrow(ctx,r2,c2,NULL,9,110); keyrow(ctx,r3,c3,NULL,9,155);
    } else if(s->keypage==1) {
        const char *r1[]={"1","2","3","4","5","6","7","8","9","0"};
        const uint8_t c1[]={30,31,32,33,34,35,36,37,38,39};
        const char *r2[]={"!","@","#","$","%","^","&","*","(",")"};
        const uint8_t sm[]={SHIFT,SHIFT,SHIFT,SHIFT,SHIFT,SHIFT,SHIFT,SHIFT,SHIFT,SHIFT};
        const char *r3[]={"-","=","[","]","\\",";","'","/","`"};
        const uint8_t c3[]={45,46,47,48,49,51,52,56,53};
        keyrow(ctx,r1,c1,NULL,10,65); keyrow(ctx,r2,c1,sm,10,110); keyrow(ctx,r3,c3,NULL,9,155);
    } else {
        const char *r1[]={"F1","F2","F3","F4","F5","F6"};
        const uint8_t c1[]={58,59,60,61,62,63};
        const char *r2[]={"F7","F8","F9","F10","F11","F12"};
        const uint8_t c2[]={64,65,66,67,68,69};
        const char *r3[]={"Home","End","PgUp","PgDn","Ins","Del"};
        const uint8_t c3[]={74,77,75,78,73,76};
        keyrow(ctx,r1,c1,NULL,6,65); keyrow(ctx,r2,c2,NULL,6,110); keyrow(ctx,r3,c3,NULL,6,155);
    }
    const char *labels[]={"Esc","Tab","Bksp","Enter"};
    const uint8_t codes[]={41,43,42,40};
    keyrow(ctx,labels,codes,NULL,4,200);
    button(ctx,"Space",4,245,132,40,KEY,44,0,true);
    const char *ar[]={LV_SYMBOL_LEFT,LV_SYMBOL_DOWN,LV_SYMBOL_UP,LV_SYMBOL_RIGHT};
    const uint8_t ac[]={80,81,82,79};
    for(unsigned i=0;i<4;i++) button(ctx,ar[i],140+44*i,245,41,40,KEY,ac[i],0,true);
    button(ctx,s->keypage==0?"ABC > 123":s->keypage==1?"123 > Fn":"Fn > ABC",4,289,312,24,NAV,4,0,true);
}
static void render(app_ctx_t *ctx)
{
    state_t *s=ctx->state;
    lv_obj_clean(s->body); s->count=0;
    s->status=ui_label(s->body,"",4,3,195);
    lv_obj_set_style_text_font(s->status,&lv_font_montserrat_14,0);
    button(ctx,"释放全部",204,0,112,28,RELEASE,0,0,false);
    if(s->view==3) {
        button(ctx,"USB 有线连接",4,36,312,48,TRANSPORT,0,0,false);
        button(ctx,"蓝牙 BLE 连接",4,92,312,48,TRANSPORT,1,0,false);
        button(ctx,"开启配对（120 秒）",4,148,312,44,PAIR,0,0,false);
        button(ctx,"清除绑定并重新配对",4,200,312,44,FORGET,0,0,false);
        ui_label(s->body,usb_service_is_ble()?"电脑蓝牙搜索：\nTouchDesk KeyMouse":"连接原生 USB OTG 口\nD- GPIO19 / D+ GPIO20",8,254,304);
    } else if(s->view==0) {
        unsigned start=s->page?FIRST_PAGE_KEYS+(s->page-1)*KEYS_PER_PAGE:0;
        unsigned limit=s->page?KEYS_PER_PAGE:FIRST_PAGE_KEYS;
        for(unsigned i=0;i<limit && start+i<KEY_COUNT;i++)
            button(ctx,keys[start+i].label,4+(i%2)*158,31+(i/2)*62,154,57,SHORTCUT,start+i,0,false);
        if(s->page==0) {
            const char *labels[]={"音量减","静音","音量加"};
            const action_t actions[]={VOLUME_DOWN,MUTE,VOLUME_UP};
            for(unsigned i=0;i<3;i++) {
                lv_obj_t *o=button(ctx,labels[i],4+i*105,218,102,60,actions[i],0,0,false);
                lv_obj_t *l=lv_obj_get_child(o,0);
                lv_obj_set_style_text_font(l,&volume_font,0);
                lv_obj_center(l);
            }
        }
        char label[40]; snprintf(label,sizeof(label),"切换页面  %u/%u",s->page+1,(unsigned)PAGE_COUNT);
        button(ctx,label,4,282,312,30,NAV,3,0,false);
    } else if(s->view==1) keyboard(ctx);
    else {
        button(ctx,"触控板\n轻触单击 / 滑动移动",4,31,257,195,PAD,0,0,false);
        button(ctx,"滚\n轮\n\n上\n下",267,31,49,195,WHEEL,0,0,false);
        button(ctx,"左键",4,232,124,40,MOUSE_BUTTON,1,0,false);
        button(ctx,"右键",192,232,124,40,MOUSE_BUTTON,2,0,false);
        button(ctx,"拖动锁定",4,278,312,34,DRAG,0,0,false);
    }
    const char *tabs[]={"快捷键","键盘","鼠标","连接"};
    for(unsigned i=0;i<4;i++) {
        lv_obj_t *o=button(ctx,tabs[i],4+i*79,318,75,38,NAV,i==3?5:i,0,false);
        if(i==s->view) lv_obj_add_state(o,LV_STATE_CHECKED);
    }
}
static void connection(lv_timer_t *timer)
{
    app_ctx_t *ctx=timer->user_data;
    state_t *s=ctx->state;
    if(s->fault_seen!=board_input_fault_epoch()) cancel(ctx);
    if(s->seen!=usb_service_session()) {
        s->seen=usb_service_session(); local_reset(s); board_input_cancel();
    }
    if(s->rebuild) { s->rebuild=false; render(ctx); }
    lv_label_set_text(s->status,usb_service_connection_text());
    for(unsigned i=0;i<s->count;i++) {
        binding_t *b=&s->bindings[i];
        bool enabled=(b->action==PAIR || b->action==FORGET)?usb_service_is_ble():
            (b->action==NAV || b->action==RELEASE || b->action==TRANSPORT || usb_service_online());
        if(enabled) lv_obj_clear_state(b->obj,LV_STATE_DISABLED);
        else lv_obj_add_state(b->obj,LV_STATE_DISABLED);
    }
}
static esp_err_t create(app_ctx_t *ctx,lv_obj_t *root)
{
    state_t *s=ctx->state;
    s->seen=usb_service_session();
    s->fault_seen=board_input_fault_epoch();
    s->body=lv_obj_create(root); lv_obj_remove_style_all(s->body);
    lv_obj_set_size(s->body,320,360); lv_obj_clear_flag(s->body,LV_OBJ_FLAG_SCROLLABLE);
    render(ctx);
    lv_timer_t *t=app_timer_create(ctx,connection,20);
    if(!t) return ESP_ERR_NO_MEM;
    connection(t); return ESP_OK;
}
static void destroy(app_ctx_t *ctx) { cancel(ctx); }
const app_desc_t app_keysim={
    .id="keysim",.title="快捷键",.subtitle="USB / BLE 触屏键盘鼠标",.caps=APP_CAP_HID,
    .state_size=sizeof(state_t),.create=create,.destroy=destroy
};
