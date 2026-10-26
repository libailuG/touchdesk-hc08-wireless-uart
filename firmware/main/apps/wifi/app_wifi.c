#include "app.h"
#include "ui.h"
#include "shell.h"
#include "board.h"
#include "wifi_service.h"
#include <stdio.h>
#include <string.h>
LV_FONT_DECLARE(font_weather12);

typedef enum { PAGE_LIST,PAGE_PASSWORD,PAGE_DETAIL,PAGE_FORGET,PAGE_SAVED,PAGE_KNOWN } page_t;
typedef struct { app_ctx_t *ctx; unsigned index; } binding_t;
typedef struct {
    page_t page,next;
    bool redraw,forget_selected;
    unsigned scan_revision,saved_revision;
    wifi_network_t known[WIFI_SAVED_LIMIT];
    size_t known_count;
    size_t count;
    wifi_status_t status;
    wifi_network_t networks[WIFI_NETWORK_LIMIT],selected;
    binding_t bindings[WIFI_NETWORK_LIMIT];
    lv_obj_t *root,*status_label,*toggle,*scan,*saved,*details,*list,*password,*keyboard,*info,*name_label;
} state_t;
static void request_page(state_t *s,page_t page) { s->next=page; s->redraw=true; }
static void password_clear(state_t *s)
{
    if(s->password) {
        /* LVGL textarea owns mutable copies (including pwd_tmp in password mode).
         * Wipe before set_text frees/reallocates the old text buffer. */
        const char *text=lv_textarea_get_text(s->password);
        wifi_secret_clear((void*)text,strlen(text));
        lv_textarea_set_text(s->password,"");
    }
}
static void accepted(bool ok) { if(!ok) shell_notice("操作未提交，请重试"); }
static void toggle(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED) return;
    accepted(wifi_service_enable(lv_obj_has_state(lv_event_get_target(e),LV_STATE_CHECKED)));
}
static void scan(lv_event_t *e)
{
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) accepted(wifi_service_scan());
}
static void saved_connect(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    app_ctx_t *ctx=lv_event_get_user_data(e); request_page(ctx->state,PAGE_SAVED);
}
static void connect_known(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    app_ctx_t *ctx=lv_event_get_user_data(e); state_t *s=ctx->state;
    if(wifi_service_connect_known(&s->selected)) request_page(s,PAGE_DETAIL); else accepted(false);
}
static void edit_password(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    app_ctx_t *ctx=lv_event_get_user_data(e); state_t *s=ctx->state;
    if(s->page==PAGE_DETAIL&&s->status.ssid_len) {
        memset(&s->selected,0,sizeof(s->selected));
        memcpy(s->selected.ssid,s->status.ssid,32); s->selected.ssid_len=s->status.ssid_len;
        s->selected.security=s->status.security; strcpy(s->selected.name,s->status.name);
    }
    if(s->selected.ssid_len) request_page(s,PAGE_PASSWORD);
}
static void detail(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    app_ctx_t *ctx=lv_event_get_user_data(e); request_page(ctx->state,PAGE_DETAIL);
}
static void choose(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    binding_t *b=lv_event_get_user_data(e);
    state_t *s=b->ctx->state;
    if(b->index>=s->count) return;
    if(s->page==PAGE_SAVED) { s->selected=s->known[b->index]; request_page(s,PAGE_KNOWN); }
    else {
        s->selected=s->networks[b->index];
        if(s->selected.remembered) {
            if(wifi_service_connect_known(&s->selected)) request_page(s,PAGE_DETAIL); else accepted(false);
        } else request_page(s,PAGE_PASSWORD);
    }
}
static void cancel(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    app_ctx_t *ctx=lv_event_get_user_data(e); state_t *s=ctx->state;
    request_page(s,PAGE_LIST);
}
static void show_password(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED) return;
    app_ctx_t *ctx=lv_event_get_user_data(e); state_t *s=ctx->state;
    lv_textarea_set_password_mode(s->password,!lv_obj_has_state(lv_event_get_target(e),LV_STATE_CHECKED));
}
static void connect(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    app_ctx_t *ctx=lv_event_get_user_data(e); state_t *s=ctx->state;
    if(!s->status.enabled) { shell_notice("请先开启 Wi-Fi"); return; }
    const char *password=s->password ? lv_textarea_get_text(s->password) : "";
    size_t length=strlen(password);
    if(s->selected.security!=NET_OPEN && (length<8 || length>63)) {
        shell_notice("密码需为 8 至 63 位"); return;
    }
    if(wifi_service_connect(&s->selected,password)) {
        password_clear(s); request_page(s,PAGE_DETAIL);
    } else accepted(false);
}
static const char *map_lower[]={
    "q","w","e","r","t","y","u","i","o","p","\n",
    "a","s","d","f","g","h","j","k","l","\n",
    "z","x","c","v","b","n","m",",",".","-","\n",
    "ABC","123","符号","空格","退格",""
};
static const char *map_upper[]={
    "Q","W","E","R","T","Y","U","I","O","P","\n",
    "A","S","D","F","G","H","J","K","L","\n",
    "Z","X","C","V","B","N","M",",",".","_","\n",
    "abc","123","符号","空格","退格",""
};
static const char *map_numbers[]={
    "1","2","3","4","5","6","7","8","9","0","\n",
    "!","@","#","$","%","^","&","*","(",")","\n",
    "+","-","=","/","?",":",";",",",".","_","\n",
    "abc","ABC","符号","空格","退格",""
};
static const char *map_symbols[]={
    "[","]","{","}","\\","|",";",":","\"","'","\n",
    "<",">","/","?","!","@","`","~","=","-","\n",
    "#","$","%","^","&","*","(",")","_","+","\n",
    "abc","ABC","123","空格","退格",""
};
static void keyboard_map(lv_obj_t *keyboard,const char **map)
{
    lv_btnmatrix_set_map(keyboard,map);
    lv_btnmatrix_set_btn_ctrl_all(keyboard,LV_BTNMATRIX_CTRL_NO_REPEAT|LV_BTNMATRIX_CTRL_CLICK_TRIG);
}
static void keyboard(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED) return;
    app_ctx_t *ctx=lv_event_get_user_data(e); state_t *s=ctx->state;
    lv_obj_t *kb=lv_event_get_target(e);
    uint16_t id=lv_btnmatrix_get_selected_btn(kb);
    if(id==LV_BTNMATRIX_BTN_NONE) return;
    const char *key=lv_btnmatrix_get_btn_text(kb,id);
    if(!strcmp(key,"abc")) keyboard_map(kb,map_lower);
    else if(!strcmp(key,"ABC")) keyboard_map(kb,map_upper);
    else if(!strcmp(key,"123")) keyboard_map(kb,map_numbers);
    else if(!strcmp(key,"符号")) keyboard_map(kb,map_symbols);
    else if(!strcmp(key,"退格")) lv_textarea_del_char(s->password);
    else lv_textarea_add_text(s->password,!strcmp(key,"空格") ? " " : key);
}
static void disconnect(lv_event_t *e)
{
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) accepted(wifi_service_disconnect());
}
static void forget_confirm(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    app_ctx_t *ctx=lv_event_get_user_data(e); state_t *s=ctx->state; s->forget_selected=s->page==PAGE_KNOWN; request_page(s,PAGE_FORGET);
}
static void forget(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    app_ctx_t *ctx=lv_event_get_user_data(e);
    state_t *s=ctx->state;
    if(s->forget_selected?wifi_service_forget_known(&s->selected):wifi_service_forget()) request_page(s,s->forget_selected?PAGE_SAVED:PAGE_LIST);
    else accepted(false);
}
static void enable_object(lv_obj_t *obj,bool enabled)
{
    if(enabled) lv_obj_clear_state(obj,LV_STATE_DISABLED);
    else lv_obj_add_state(obj,LV_STATE_DISABLED);
}
static bool busy(const wifi_status_t *s)
{
    return s->state==NET_CONNECTING || s->state==NET_GETTING_IP || s->state==NET_STOPPING;
}
static void list_build(app_ctx_t *ctx)
{
    state_t *s=ctx->state;
    ui_label(s->root,"2.4 GHz Wi-Fi",8,4,240);
    s->toggle=lv_switch_create(s->root); lv_obj_set_pos(s->toggle,258,2); lv_obj_set_size(s->toggle,54,30);
    if(s->status.enabled) lv_obj_add_state(s->toggle,LV_STATE_CHECKED);
    lv_obj_add_event_cb(s->toggle,toggle,LV_EVENT_VALUE_CHANGED,ctx);
    s->status_label=ui_label(s->root,wifi_state_text(&s->status),8,42,304);
    s->scan=ui_button(s->root,"扫描",8,78,94,40,scan,ctx);
    s->saved=ui_button(s->root,"已保存",112,78,94,40,saved_connect,ctx);
    s->details=ui_button(s->root,"详情",216,78,96,40,detail,ctx);
    s->list=lv_obj_create(s->root); lv_obj_remove_style_all(s->list);
    lv_obj_set_pos(s->list,0,126); lv_obj_set_size(s->list,320,234);
    lv_obj_set_scroll_dir(s->list,LV_DIR_VER); lv_obj_clear_flag(s->list,LV_OBJ_FLAG_SCROLL_ELASTIC);
    for(size_t i=0;i<s->count;i++) {
        s->bindings[i]=(binding_t){ctx,i};
        lv_obj_t *row=lv_btn_create(s->list); lv_obj_set_pos(row,8,i*66); lv_obj_set_size(row,304,60);
        lv_obj_clear_flag(row,LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_set_style_bg_color(row,ui_surface(),0);
        lv_obj_set_style_text_color(row,ui_foreground(),0);
        lv_obj_set_style_pad_all(row,0,0);
        lv_obj_t *name=ui_label(row,s->networks[i].name,8,2,288);
        lv_label_set_long_mode(name,LV_LABEL_LONG_DOT); lv_obj_set_height(name,26);
        lv_obj_t *description=ui_label(row,"",8,34,288);
        lv_obj_set_style_text_font(description,&font_weather12,0);
        lv_label_set_text_fmt(description,"%s   %d dBm%s",s->networks[i].security==NET_UNSUPPORTED ? "Unsupported" :
                              wifi_security_text(s->networks[i].security),s->networks[i].rssi,s->networks[i].remembered?"  已保存":"");
        lv_obj_add_event_cb(row,choose,LV_EVENT_CLICKED,&s->bindings[i]);
        enable_object(row,s->status.enabled && s->networks[i].security!=NET_UNSUPPORTED);
    }
    if(!s->count) ui_label(s->list,s->status.enabled?"点击扫描查找附近网络":"开启 Wi-Fi 后可扫描网络",8,10,304);
}
static void password_build(app_ctx_t *ctx)
{
    state_t *s=ctx->state;
    lv_obj_t *name=ui_label(s->root,s->selected.name,8,0,304);
    lv_label_set_long_mode(name,LV_LABEL_LONG_DOT); lv_obj_set_height(name,26);
    if(s->selected.security==NET_OPEN) {
        ui_label(s->root,"开放网络：连接不加密。\n点击连接以继续。",8,52,304);
        ui_button(s->root,"连接",8,158,144,44,connect,ctx);
        ui_button(s->root,"取消",168,158,144,44,cancel,ctx);
        return;
    }
    s->password=lv_textarea_create(s->root);
    lv_obj_set_pos(s->password,8,32); lv_obj_set_size(s->password,304,44);
    lv_textarea_set_one_line(s->password,true); lv_textarea_set_max_length(s->password,63);
    lv_textarea_set_password_mode(s->password,true); lv_textarea_set_password_show_time(s->password,0);
    lv_textarea_set_placeholder_text(s->password,"输入密码");
    lv_textarea_set_accepted_chars(s->password," !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~");
    lv_obj_set_style_bg_color(s->password,ui_surface(),0);
    lv_obj_set_style_text_color(s->password,ui_foreground(),0);
    lv_obj_t *show=lv_checkbox_create(s->root); lv_checkbox_set_text(show,"显示密码");
    lv_obj_set_pos(show,8,82); lv_obj_add_event_cb(show,show_password,LV_EVENT_VALUE_CHANGED,ctx);
    ui_button(s->root,"连接",8,116,144,36,connect,ctx);
    ui_button(s->root,"取消",168,116,144,36,cancel,ctx);
    s->keyboard=lv_btnmatrix_create(s->root);
    lv_obj_set_pos(s->keyboard,4,158); lv_obj_set_size(s->keyboard,312,198);
    lv_obj_set_style_pad_all(s->keyboard,2,0);
    lv_obj_set_style_pad_row(s->keyboard,3,0); lv_obj_set_style_pad_column(s->keyboard,2,0);
    lv_obj_set_style_bg_color(s->keyboard,ui_background(),0);
    lv_obj_set_style_bg_color(s->keyboard,ui_surface(),LV_PART_ITEMS);
    lv_obj_set_style_text_color(s->keyboard,ui_foreground(),LV_PART_ITEMS);
    keyboard_map(s->keyboard,map_lower);
    lv_obj_add_event_cb(s->keyboard,keyboard,LV_EVENT_VALUE_CHANGED,ctx);
}
static void detail_build(app_ctx_t *ctx)
{
    state_t *s=ctx->state;
    s->name_label=ui_label(s->root,"",8,4,304);
    lv_label_set_long_mode(s->name_label,LV_LABEL_LONG_DOT); lv_obj_set_height(s->name_label,26);
    s->info=ui_label(s->root,"",8,40,304);
    ui_button(s->root,"更改密码",8,205,304,40,edit_password,ctx);
    ui_button(s->root,"断开",8,256,144,42,disconnect,ctx);
    ui_button(s->root,"忘记网络",168,256,144,42,forget_confirm,ctx);
    s->saved=ui_button(s->root,"已保存网络",8,310,304,42,saved_connect,ctx);
}
static void saved_build(app_ctx_t *ctx)
{
    state_t *s=ctx->state;
    s->known_count=wifi_service_saved_networks(s->known,WIFI_SAVED_LIMIT,&s->saved_revision);
    char title[64]; snprintf(title,sizeof(title),"已保存网络 %u / %u",(unsigned)s->known_count,WIFI_SAVED_LIMIT);
    ui_label(s->root,title,8,4,304);
    s->list=lv_obj_create(s->root); lv_obj_remove_style_all(s->list);
    lv_obj_set_pos(s->list,0,42); lv_obj_set_size(s->list,320,276);
    lv_obj_set_scroll_dir(s->list,LV_DIR_VER); lv_obj_clear_flag(s->list,LV_OBJ_FLAG_SCROLL_ELASTIC);
    for(size_t i=0;i<s->known_count;i++) {
        s->bindings[i]=(binding_t){ctx,i};
        lv_obj_t *row=ui_button(s->list,s->known[i].name,8,i*54,304,48,choose,&s->bindings[i]);
        lv_obj_t *name=lv_obj_get_child(row,0); lv_label_set_long_mode(name,LV_LABEL_LONG_DOT);
    }
    if(!s->known_count) ui_label(s->list,"连接成功后自动记住网络。",8,18,304);
    lv_obj_t *hint=ui_label(s->root,"自动连接可用网络，满后替换最久未使用的。",8,335,304);
    lv_obj_set_style_text_font(hint,&font_weather12,0);
}
static void known_build(app_ctx_t *ctx)
{
    state_t *s=ctx->state;
    lv_obj_t *name=ui_label(s->root,s->selected.name,8,8,304);
    lv_label_set_long_mode(name,LV_LABEL_LONG_DOT); lv_obj_set_height(name,26);
    ui_label(s->root,"已保存，连接时无需重复输入密码。",8,55,304);
    lv_obj_t *b=ui_button(s->root,"连接此网络",8,112,304,44,connect_known,ctx); enable_object(b,s->status.enabled);
    b=ui_button(s->root,"更改密码",8,170,304,44,edit_password,ctx); enable_object(b,s->selected.security!=NET_OPEN);
    ui_button(s->root,"忘记此网络",8,228,304,44,forget_confirm,ctx);
    ui_button(s->root,"返回列表",8,286,304,44,saved_connect,ctx);
}
static void render(app_ctx_t *ctx)
{
    state_t *s=ctx->state;
    password_clear(s); board_input_cancel(); lv_obj_clean(s->root);
    s->password=s->keyboard=s->status_label=s->toggle=s->scan=s->saved=s->details=s->list=s->info=s->name_label=NULL;
    s->page=s->next; s->redraw=false;
    if(s->page==PAGE_LIST) list_build(ctx);
    else if(s->page==PAGE_PASSWORD) password_build(ctx);
    else if(s->page==PAGE_DETAIL) detail_build(ctx);
    else if(s->page==PAGE_SAVED) saved_build(ctx);
    else if(s->page==PAGE_KNOWN) known_build(ctx);
    else {
        ui_label(s->root,"忘记网络？",8,24,304);
        ui_label(s->root,"仅删除这个网络的连接记录。\n其他已保存网络会保留。",8,78,304);
        lv_obj_t *name=ui_label(s->root,s->forget_selected?s->selected.name:s->status.name,8,138,304);
        lv_label_set_long_mode(name,LV_LABEL_LONG_DOT); lv_obj_set_height(name,26);
        ui_button(s->root,"确认忘记",8,190,144,48,forget,ctx);
        ui_button(s->root,"取消",168,190,144,48,cancel,ctx);
    }
}
static void refresh(lv_timer_t *timer)
{
    app_ctx_t *ctx=timer->user_data; state_t *s=ctx->state;
    bool enabled=s->status.enabled,warning=s->status.save_warning;
    net_error_t previous_error=s->status.error;
    wifi_service_status(&s->status);
    if(s->status.save_warning && !warning) shell_notice("配置未保存，请重试");
    if(s->status.error==NET_ERR_SCAN && previous_error!=NET_ERR_SCAN) shell_notice("扫描失败，请重试");
    unsigned revision=0;
    if(s->page==PAGE_LIST && !s->redraw) {
        size_t count=wifi_service_networks(s->networks,WIFI_NETWORK_LIMIT,&revision);
        if(revision!=s->scan_revision || enabled!=s->status.enabled) {
            s->scan_revision=revision; s->count=count; request_page(s,PAGE_LIST);
        }
    }
    if(s->page==PAGE_SAVED&&s->status.saved_revision!=s->saved_revision) request_page(s,PAGE_SAVED);
    if(s->redraw) {
        if(s->next==PAGE_LIST) s->count=wifi_service_networks(s->networks,WIFI_NETWORK_LIMIT,&s->scan_revision);
        render(ctx);
    }
    if(s->page==PAGE_LIST) {
        lv_label_set_text(s->status_label,wifi_state_text(&s->status));
        if(s->status.enabled) lv_obj_add_state(s->toggle,LV_STATE_CHECKED);
        else lv_obj_clear_state(s->toggle,LV_STATE_CHECKED);
        enable_object(s->toggle,s->status.available);
        enable_object(s->scan,s->status.enabled && !s->status.scanning && !busy(&s->status));
        enable_object(s->saved,s->status.available);
    } else if(s->page==PAGE_DETAIL) {
        char name[WIFI_NAME_SIZE];
        char signal[24]="--";
        if(s->status.state==NET_CONNECTED && s->status.rssi<0)
            snprintf(signal,sizeof(signal),"%d dBm",s->status.rssi);
        snprintf(name,sizeof(name),"%s",s->status.name[0]?s->status.name:s->status.saved_name);
        /* Keep arbitrary SSIDs on a single ellipsized line. */
        lv_label_set_text_fmt(s->info,"%s\nIP: %s\n信号: %s\n重连等待: %u 秒\n%s\n%s",
            wifi_state_text(&s->status),s->status.ip[0]?s->status.ip:"--",signal,s->status.retry_seconds,
            s->status.has_saved?(s->status.save_warning?"网络配置未保存":"已记住网络，支持多地点切换"):"尚未保存网络",
            s->status.save_warning?"配置保存失败":"获取 IP 不代表可上网");
        /* Name has its own one-line label below the fixed detail block. */
        lv_label_set_text(s->name_label,name);
        enable_object(s->saved,s->status.available);
    }
}
static esp_err_t create(app_ctx_t *ctx,lv_obj_t *root)
{
    state_t *s=ctx->state; s->root=root;
    wifi_service_status(&s->status);
    s->count=wifi_service_networks(s->networks,WIFI_NETWORK_LIMIT,&s->scan_revision);
    s->next=PAGE_LIST; render(ctx);
    lv_timer_t *timer=app_timer_create(ctx,refresh,100);
    if(!timer) return ESP_ERR_NO_MEM;
    refresh(timer);
    if(s->status.save_warning) shell_notice("配置存储不可用");
    if(s->status.enabled && !s->count && !busy(&s->status)) wifi_service_scan();
    return ESP_OK;
}
static void destroy(app_ctx_t *ctx) { if(ctx->state) password_clear(ctx->state); }
static bool back(app_ctx_t *ctx)
{
    state_t *s=ctx->state;
    if(s->page==PAGE_LIST && !s->redraw) return false;
    request_page(s,s->page==PAGE_KNOWN?PAGE_SAVED:s->page==PAGE_FORGET&&s->forget_selected?PAGE_KNOWN:PAGE_LIST); return true;
}
const app_desc_t app_wifi={
    .id="wifi",.caps=APP_CAP_NETWORK,.title="Wi-Fi",.subtitle="无线网络连接",.state_size=sizeof(state_t),
    .create=create,.destroy=destroy,.back=back
};
