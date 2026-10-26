#include "desktop.h"
#include "app.h"
#include "ui.h"
#include "weather.h"
#include "weather_style.h"
#include "weather_icons.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
LV_FONT_DECLARE(font_weather12);
LV_FONT_DECLARE(font_weather13);
LV_FONT_DECLARE(font_weather16);
LV_FONT_DECLARE(font_weather52);
LV_FONT_DECLARE(font_weather72);
extern const app_desc_t app_serial,app_wireless;
extern const app_desc_t app_keymouse,app_weather,app_keysim,app_counter,app_wifi,app_settings,app_music,app_xiaozhi,app_microphone;
static struct {
    lv_obj_t *view,*clock,*date,*summary,*temperature,*updated,*icon;
    lv_color_t bg,ink,sub,surface,accent;
    int time_key,date_key,code,day;
    int64_t last_poll;
} desk;
/* Keep this display preference while navigating; reboot returns to HH:MM. */
static bool show_seconds;
static void toggle_seconds(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    show_seconds=!show_seconds; desk.time_key=-2; desk.last_poll=-1;
    desktop_poll();
}
static lv_obj_t *text(lv_obj_t *parent,const char *value,int x,int y,int w,const lv_font_t *font,lv_color_t color)
{
    lv_obj_t *label=lv_label_create(parent); lv_label_set_text(label,value);
    lv_obj_set_pos(label,x,y); lv_obj_set_width(label,w);
    lv_label_set_long_mode(label,LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(label,font,0); lv_obj_set_style_text_color(label,color,0);
    lv_obj_clear_flag(label,LV_OBJ_FLAG_CLICKABLE); return label;
}
static void launch(lv_event_t *e)
{
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) app_request(lv_event_get_user_data(e));
}
static lv_obj_t *tile(int x,int y,int w,int h,const app_desc_t *app)
{
    lv_obj_t *button=lv_btn_create(desk.view); lv_obj_remove_style_all(button);
    lv_obj_set_pos(button,x,y); lv_obj_set_size(button,w,h);
    lv_obj_set_style_bg_color(button,desk.surface,0); lv_obj_set_style_bg_opa(button,LV_OPA_COVER,0);
    lv_obj_set_style_radius(button,14,0); lv_obj_set_style_bg_opa(button,LV_OPA_60,LV_STATE_PRESSED);
    lv_obj_clear_flag(button,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_user_data(button,(void*)app);
    lv_obj_add_event_cb(button,launch,LV_EVENT_CLICKED,(void*)app); return button;
}
static void icon_line(lv_draw_ctx_t *ctx,const lv_area_t *a,int x1,int y1,int x2,int y2)
{
    lv_draw_line_dsc_t d; lv_draw_line_dsc_init(&d); d.width=2; d.color=desk.accent; d.round_start=d.round_end=true;
    lv_point_t p={a->x1+x1,a->y1+y1},q={a->x1+x2,a->y1+y2}; lv_draw_line(ctx,&d,&p,&q);
}
static void icon_event(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_DRAW_MAIN) return;
    lv_obj_t *obj=lv_event_get_target(e); lv_draw_ctx_t *ctx=lv_event_get_draw_ctx(e);
    lv_area_t a; lv_obj_get_coords(obj,&a);
    int type=(int)(intptr_t)lv_event_get_user_data(e);
    if(type==1) {
        icon_line(ctx,&a,13,7,13,25); icon_line(ctx,&a,27,4,27,22);
        icon_line(ctx,&a,13,7,27,4); icon_line(ctx,&a,13,11,27,8);
        icon_line(ctx,&a,7,25,13,25); icon_line(ctx,&a,7,27,12,27);
        icon_line(ctx,&a,21,22,27,22); icon_line(ctx,&a,21,24,26,24);
    } else if(type==4) {
        lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_TRANSP;d.border_color=desk.accent;d.border_width=2;d.radius=7;
        lv_area_t r={a.x1+2,a.y1+4,a.x1+30,a.y1+25};lv_draw_rect(ctx,&d,&r);
        icon_line(ctx,&a,8,25,8,31);icon_line(ctx,&a,8,31,15,25);
        icon_line(ctx,&a,10,14,10,15);icon_line(ctx,&a,22,14,22,15);
    } else if(type==5) {
        lv_draw_rect_dsc_t d;lv_draw_rect_dsc_init(&d);d.bg_opa=LV_OPA_TRANSP;d.border_color=desk.accent;d.border_width=2;d.radius=5;
        lv_area_t r={a.x1+11,a.y1+2,a.x1+21,a.y1+20};lv_draw_rect(ctx,&d,&r);
        icon_line(ctx,&a,6,17,6,22);icon_line(ctx,&a,6,22,16,27);icon_line(ctx,&a,16,27,26,22);icon_line(ctx,&a,26,22,26,17);icon_line(ctx,&a,16,27,16,32);
    } else if(type==0) {
        lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d); d.bg_opa=LV_OPA_TRANSP;
        d.border_color=desk.accent; d.border_width=2; d.radius=4;
        lv_area_t r={a.x1+2,a.y1+5,a.x1+29,a.y1+27}; lv_draw_rect(ctx,&d,&r);
        if(type==0) {
            for(int row=0;row<2;row++) for(int col=0;col<4;col++) icon_line(ctx,&a,7+col*5,11+row*5,8+col*5,11+row*5);
            icon_line(ctx,&a,10,22,22,22);
        } else { icon_line(ctx,&a,9,16,23,16); icon_line(ctx,&a,16,9,16,23); }
    } else if(type==2) {
        lv_draw_arc_dsc_t d; lv_draw_arc_dsc_init(&d); d.color=desk.accent; d.width=2; d.rounded=true;
        lv_point_t center={a.x1+16,a.y1+28};
        for(int r=7;r<=17;r+=5) lv_draw_arc(ctx,&d,&center,r,225,315);
        icon_line(ctx,&a,16,25,16,26);
    } else {
        for(int row=0;row<3;row++) {
            int y=7+row*9,x=row==1?22:10;
            icon_line(ctx,&a,3,y,29,y);
            lv_draw_rect_dsc_t d; lv_draw_rect_dsc_init(&d); d.bg_color=desk.surface;
            d.border_color=desk.accent; d.border_width=2; d.radius=LV_RADIUS_CIRCLE;
            lv_area_t r={a.x1+x-3,a.y1+y-3,a.x1+x+3,a.y1+y+3}; lv_draw_rect(ctx,&d,&r);
        }
    }
}
static void shortcut(const app_desc_t *app,int x,int y,int type)
{
    lv_obj_t *b=tile(x,y,92,80,app);
    lv_obj_t *icon=lv_obj_create(b); lv_obj_remove_style_all(icon);
    lv_obj_set_pos(icon,29,6); lv_obj_set_size(icon,34,34);
    lv_obj_clear_flag(icon,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(icon,icon_event,LV_EVENT_DRAW_MAIN,(void*)(intptr_t)type);
    lv_obj_t *title=text(b,app==&app_microphone?"麦克风":app==&app_keysim?"快捷键":app->title,3,46,86,&font_ui,desk.ink);
    lv_obj_set_style_text_align(title,LV_TEXT_ALIGN_CENTER,0);
}
static void deleted(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_DELETE) return;
    desk.view=NULL; desk.clock=NULL; desk.date=NULL; desk.icon=NULL;
    weather_desktop_activate(false);
}
bool desktop_active(void) { return desk.view!=NULL; }
void desktop_poll(void)
{
    if(!desk.view) return;
    int64_t now_us=esp_timer_get_time();
    if(desk.last_poll>=0&&now_us-desk.last_poll<250000) return;
    desk.last_poll=now_us;
    weather_snapshot_t s; weather_snapshot(&s); time_t now=weather_now(); struct tm t; localtime_r(&now,&t);
    int key=s.time_valid?(t.tm_hour*60+t.tm_min)*60+(show_seconds?t.tm_sec:0):-1;
    int date=s.time_valid?weather_date_key(&t):-1;
    if(key!=desk.time_key) {
        lv_obj_set_style_text_font(desk.clock,show_seconds?weather_clock_seconds_font(s.theme):weather_clock_font(s.theme),0);
        char value[16];
        if(s.time_valid) strftime(value,sizeof(value),show_seconds?"%H:%M:%S":"%H:%M",&t);
        else strcpy(value,show_seconds?"--:--:--":"--:--");
        lv_label_set_text(desk.clock,value); desk.time_key=key;
    }
    if(date!=desk.date_key) {
        char lunar[64],value[128]; static const char *week[]={"周日","周一","周二","周三","周四","周五","周六"};
        if(s.time_valid) {
            if(weather_lunar(t.tm_year+1900,t.tm_mon+1,t.tm_mday,lunar,sizeof(lunar)))
                snprintf(value,sizeof(value),"%d月%d日  农历%s  %s",t.tm_mon+1,t.tm_mday,lunar,week[t.tm_wday]);
            else snprintf(value,sizeof(value),"%d月%d日  %s",t.tm_mon+1,t.tm_mday,week[t.tm_wday]);
        } else snprintf(value,sizeof(value),"等待网络校时");
        lv_label_set_text(desk.date,value); desk.date_key=date;
    }
    bool stale=s.data.valid&&now-s.data.data_time>=6*3600;
    int code=s.data.valid&&!stale?s.data.code:-1,day=s.data.daytime;
    if(code!=desk.code||day!=desk.day) {
        if(desk.icon) lv_obj_del(desk.icon);
        desk.icon=weather_icon(lv_obj_get_parent(desk.summary),188,17,94,code,day,desk.accent,desk.surface);
        desk.code=code; desk.day=day;
    }
    char value[128];
    snprintf(value,sizeof(value),"%s · %s",weather_cities[s.city].name,!s.data.valid?"暂无数据":stale?"数据过期":weather_description(s.data.code));
    if(strcmp(lv_label_get_text(desk.summary),value)) lv_label_set_text(desk.summary,value);
    if(s.data.valid&&!stale) snprintf(value,sizeof(value),"%.0f°",(double)s.data.temperature); else strcpy(value,"--°");
    if(strcmp(lv_label_get_text(desk.temperature),value)) lv_label_set_text(desk.temperature,value);
    lv_obj_set_style_text_color(desk.temperature,s.data.valid&&!stale?
        weather_value_color(s.theme,s.dynamic_colors,WEATHER_TEMPERATURE,s.data.temperature):desk.sub,0);
    const char *status=!s.network?"离线缓存":s.phase==WEATHER_FETCHING?"正在更新":s.phase==WEATHER_ERROR?"更新失败":
        s.phase==WEATHER_LIMITED?"服务限流":!s.time_valid?"等待校时":!s.data.valid?"等待天气":now-s.data.data_time>=3600?"天气已过期":"已更新";
    if(s.data.valid) { struct tm data; localtime_r(&s.data.data_time,&data); snprintf(value,sizeof(value),"%02d:%02d %s  >",data.tm_hour,data.tm_min,status); }
    else snprintf(value,sizeof(value),"%s  >",!s.network?"请先连接 Wi-Fi":status);
    if(strcmp(lv_label_get_text(desk.updated),value)) lv_label_set_text(desk.updated,value);
}
void desktop_create(lv_obj_t *parent)
{
    /* Show cached weather immediately; app_services owns background network updates. */
    weather_snapshot_t s; weather_snapshot(&s);
    memset(&desk,0,sizeof(desk)); desk.time_key=desk.date_key=-2; desk.code=-999; desk.day=-1; desk.last_poll=-1;
    const weather_palette_t *p=weather_palette(s.theme);
    desk.bg=lv_color_hex(p->bg); desk.ink=lv_color_hex(p->ink);
    desk.sub=lv_color_hex(p->sub); desk.surface=lv_color_hex(p->surface); desk.accent=lv_color_hex(p->accent);
    /* Leave the bottom 18px transparent so the shell's notice label stays visible. */
    desk.view=lv_obj_create(parent); lv_obj_remove_style_all(desk.view); lv_obj_set_size(desk.view,320,426);
    lv_obj_add_flag(desk.view,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(desk.view,LV_DIR_VER);
    lv_obj_set_style_bg_color(desk.view,desk.bg,0); lv_obj_set_style_bg_opa(desk.view,LV_OPA_COVER,0);
    lv_obj_add_event_cb(desk.view,deleted,LV_EVENT_DELETE,NULL);
    desk.clock=text(desk.view,"--:--",0,0,320,weather_clock_font(s.theme),desk.ink);
    lv_obj_set_style_text_align(desk.clock,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_add_flag(desk.clock,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(desk.clock,toggle_seconds,LV_EVENT_CLICKED,NULL);
    desk.date=text(desk.view,"等待网络校时",12,82,296,&font_weather13,desk.sub);
    lv_obj_set_style_text_align(desk.date,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_t *weather=tile(16,112,288,126,&app_weather);
    text(weather,"天气时钟",16,10,150,&font_weather12,desk.sub);
    desk.summary=text(weather,"",16,34,164,&font_weather16,desk.ink);
    desk.temperature=text(weather,"--°",16,49,168,weather_temperature_font(s.theme),desk.ink);
    desk.updated=text(weather,"等待天气",16,106,256,&font_weather12,desk.sub);
    shortcut(&app_xiaozhi,12,254,4); shortcut(&app_wireless,114,254,0); shortcut(&app_keymouse,216,254,0);
    shortcut(&app_wifi,12,346,2); shortcut(&app_keysim,114,346,0); shortcut(&app_settings,216,346,3);
    shortcut(&app_serial,12,438,0); shortcut(&app_music,114,438,1);
    weather_desktop_activate(true); desktop_poll();
}
