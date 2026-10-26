#include "app.h"
#include "ui.h"
#include "weather.h"
#include "weather_style.h"
#include "weather_icons.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
LV_FONT_DECLARE(font_weather12);
LV_FONT_DECLARE(font_weather13);
LV_FONT_DECLARE(font_weather16);
LV_FONT_DECLARE(font_weather52);
LV_FONT_DECLARE(font_weather72);
typedef enum { HOME,SETTINGS,CITIES,APPEARANCE,STATUS,ABOUT } page_t;
typedef struct {
    app_ctx_t *ctx; page_t page,parent,pending; bool change,hourly,seconds;
    lv_obj_t *body,*clock,*date,*stamp,*message;
    weather_snapshot_t snapshot;
    lv_color_t bg,ink,sub,line,accent;
    unsigned revision;
    int minute,date_key,shown_second;
    int64_t message_until;
} state_t;
enum { ACT_BACK=1,ACT_DESKTOP,ACT_REFRESH,ACT_SETTINGS,ACT_CITIES,ACT_APPEARANCE,ACT_STATUS,ACT_ABOUT,ACT_HOURLY,ACT_COLORS,ACT_CITY=100,ACT_THEME=200 };
static void draw(state_t *s);
static lv_obj_t *label(state_t *s,const char *text,int x,int y,int width,const lv_font_t *font,lv_color_t color)
{
    lv_obj_t *l=lv_label_create(s->body); lv_label_set_text(l,text);
    lv_obj_set_pos(l,x,y); lv_obj_set_width(l,width);
    lv_label_set_long_mode(l,LV_LABEL_LONG_CLIP); lv_label_set_recolor(l,true);
    lv_obj_set_style_text_font(l,font,0); lv_obj_set_style_text_color(l,color,0);
    return l;
}
static void note(state_t *s,const char *text)
{
    if(!s->message) return;
    lv_label_set_text(s->message,text); lv_obj_clear_flag(s->message,LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s->message); s->message_until=esp_timer_get_time()+3500000;
}
static void line(state_t *s,int y)
{
    lv_obj_t *o=lv_obj_create(s->body); lv_obj_remove_style_all(o);
    lv_obj_set_pos(o,22,y); lv_obj_set_size(o,276,1);
    lv_obj_set_style_bg_color(o,s->line,0); lv_obj_set_style_bg_opa(o,LV_OPA_COVER,0);
    lv_obj_clear_flag(o,LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);
}
static void event(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    state_t *s=lv_event_get_user_data(e);
    int action=(int)(intptr_t)lv_obj_get_user_data(lv_event_get_target(e));
    if(action==ACT_COLORS) {
        if(weather_choose_colors(!s->snapshot.dynamic_colors)!=ESP_OK) note(s,"保存失败，保留原设置");
        else { s->pending=APPEARANCE; s->change=true; }
        return;
    }
    if(action==ACT_HOURLY) { s->hourly=!s->hourly; s->pending=HOME; s->change=true; return; }
    if(action==ACT_DESKTOP) { app_request(NULL); return; }
    if(action==ACT_REFRESH) {
        if(weather_refresh()) note(s,"正在更新天气");
        else note(s,!s->snapshot.network?"请在系统 Wi-Fi 中连接网络":!s->snapshot.time_valid?"等待网络校时":"请稍候再刷新");
        return;
    }
    if(action>=ACT_THEME&&action<ACT_THEME+WEATHER_THEMES) {
        if(weather_choose_theme(action-ACT_THEME)!=ESP_OK) { note(s,"保存失败，保留原设置"); return; }
        s->pending=APPEARANCE;
    } else if(action>=ACT_CITY&&action<ACT_CITY+WEATHER_CITIES) {
        if(weather_choose_city(action-ACT_CITY)!=ESP_OK) { note(s,"保存失败，保留原城市"); return; }
        s->pending=s->parent;
    } else if(action==ACT_BACK) s->pending=s->page==SETTINGS?HOME:s->page==CITIES?s->parent:SETTINGS;
    else if(action==ACT_SETTINGS) s->pending=SETTINGS;
    else if(action==ACT_CITIES) { s->parent=s->page==HOME?HOME:SETTINGS; s->pending=CITIES; }
    else if(action==ACT_APPEARANCE) s->pending=APPEARANCE;
    else if(action==ACT_STATUS) s->pending=STATUS;
    else if(action==ACT_ABOUT) s->pending=ABOUT;
    else return;
    s->change=true;
}
static lv_obj_t *button(state_t *s,const char *text,int x,int y,int width,int height,int action)
{
    lv_obj_t *b=lv_btn_create(s->body); lv_obj_remove_style_all(b);
    lv_obj_set_pos(b,x,y); lv_obj_set_size(b,width,height);
    lv_obj_clear_flag(b,LV_OBJ_FLAG_SCROLLABLE|LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_set_style_bg_color(b,s->line,LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(b,LV_OPA_COVER,LV_STATE_PRESSED);
    lv_obj_set_style_radius(b,6,0);
    lv_obj_set_user_data(b,(void*)(intptr_t)action);
    lv_obj_add_event_cb(b,event,LV_EVENT_CLICKED,s);
    lv_obj_t *l=lv_label_create(b); lv_label_set_text(l,text); lv_obj_set_width(l,width-4);
    lv_label_set_long_mode(l,LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(l,&font_weather16,0); lv_obj_set_style_text_color(l,s->ink,0);
    lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0); lv_obj_center(l);
    return b;
}
static void header(state_t *s,const char *title)
{
    button(s,"< 返回",12,8,74,44,ACT_BACK);
    label(s,title,98,19,200,&font_weather16,s->ink); line(s,62);
}
static void row(state_t *s,const char *title,const char *subtitle,const char *value,int y,int action)
{
    button(s,"",18,y,284,65,action);
    lv_obj_t *a=label(s,title,22,y+9,160,&font_weather16,s->ink);
    lv_obj_t *b=label(s,subtitle,22,y+34,220,&font_weather12,s->sub);
    lv_obj_t *c=label(s,value,220,y+19,78,&font_weather12,s->accent);
    lv_obj_set_style_text_align(c,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_clear_flag(a,LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(b,LV_OBJ_FLAG_CLICKABLE); lv_obj_clear_flag(c,LV_OBJ_FLAG_CLICKABLE);
    line(s,y+65);
}
static void update_time(state_t *s)
{
    if(!s->clock) return;
    lv_obj_set_style_text_font(s->clock,s->seconds?weather_clock_seconds_font(s->snapshot.theme):weather_clock_font(s->snapshot.theme),0);
    if(!s->snapshot.time_valid) {
        lv_label_set_text(s->clock,s->seconds?"--:--:--":"--:--"); lv_label_set_text(s->date,"等待网络校时"); return;
    }
    time_t now=weather_now(); struct tm t; localtime_r(&now,&t);
    int minute=t.tm_hour*60+t.tm_min,key=weather_date_key(&t);
    if(minute!=s->minute||(s->seconds&&t.tm_sec!=s->shown_second)) { char text[16]; strftime(text,sizeof(text),s->seconds?"%H:%M:%S":"%H:%M",&t); lv_label_set_text(s->clock,text); s->minute=minute; s->shown_second=t.tm_sec; }
    if(key!=s->date_key) {
        char lunar[64],date[128]; static const char *week[]={"星期日","星期一","星期二","星期三","星期四","星期五","星期六"};
        if(!weather_lunar(t.tm_year+1900,t.tm_mon+1,t.tm_mday,lunar,sizeof(lunar))) snprintf(lunar,sizeof(lunar),"农历超出范围");
        snprintf(date,sizeof(date),"%d月%d日  农历%s  %s",t.tm_mon+1,t.tm_mday,lunar,week[t.tm_wday]);
        lv_label_set_text(s->date,date); s->date_key=key;
    }
}
static void clock_clicked(lv_event_t *e)
{
    state_t *s=lv_event_get_user_data(e); s->seconds=!s->seconds; s->minute=-1; update_time(s);
}
static void home(state_t *s)
{
    weather_snapshot_t *w=&s->snapshot; weather_data_t *d=&w->data;
    time_t now=weather_now(); bool stale=d->valid&&now-d->data_time>=6*3600;
    s->clock=label(s,"--:--",0,0,320,weather_clock_font(w->theme),s->ink);
    lv_obj_set_style_text_align(s->clock,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_add_flag(s->clock,LV_OBJ_FLAG_CLICKABLE); lv_obj_add_event_cb(s->clock,clock_clicked,LV_EVENT_CLICKED,s);
    s->date=label(s,"等待网络校时",10,89,300,&font_weather13,s->sub);
    lv_obj_set_style_text_align(s->date,LV_TEXT_ALIGN_CENTER,0);
    char text[160];
    if(!s->hourly) {
    label(s,"点击查看5小时",188,121,112,&font_weather12,s->sub);
    weather_icon(s->body,6,130,196,d->valid&&!stale?d->code:-1,d->daytime,s->accent,s->bg);
    if(d->valid&&!stale) snprintf(text,sizeof(text),"%.0f°",(double)d->temperature); else snprintf(text,sizeof(text),"--°");
    lv_obj_t *temp=label(s,text,198,175,108,weather_temperature_font(w->theme),d->valid&&!stale?weather_value_color(w->theme,w->dynamic_colors,WEATHER_TEMPERATURE,d->temperature):s->sub); lv_obj_set_style_text_align(temp,LV_TEXT_ALIGN_RIGHT,0);
    lv_obj_t *desc=label(s,!d->valid?"暂无天气":stale?"已过期":weather_description(d->code),205,245,95,&font_weather16,s->ink);
    lv_obj_set_style_text_align(desc,LV_TEXT_ALIGN_RIGHT,0);
    text[0]=0;
    struct tm today; localtime_r(&now,&today);
    for(int i=0;i<WEATHER_DAYS;i++) if(d->valid&&d->days[i].date==weather_date_key(&today)) {
        char low[32],high[32];
        weather_color_number(low,sizeof(low),w->theme,w->dynamic_colors,WEATHER_TEMPERATURE,d->days[i].low,0);
        weather_color_number(high,sizeof(high),w->theme,w->dynamic_colors,WEATHER_TEMPERATURE,d->days[i].high,0);
        snprintf(text,sizeof(text),"%s° - %s°",low,high);
    }
    if(!text[0]) snprintf(text,sizeof(text),"等待更新");
    lv_obj_t *range=label(s,text,202,279,98,&font_weather12,s->sub); lv_obj_set_style_text_align(range,LV_TEXT_ALIGN_RIGHT,0);
    if(d->valid&&!stale) {
        char apparent[32],humidity[32],wind[32];
        weather_color_number(apparent,sizeof(apparent),w->theme,w->dynamic_colors,WEATHER_TEMPERATURE,d->apparent,0);
        weather_color_number(humidity,sizeof(humidity),w->theme,w->dynamic_colors,WEATHER_HUMIDITY,d->humidity,0);
        weather_color_number(wind,sizeof(wind),w->theme,w->dynamic_colors,WEATHER_WIND,d->wind,1);
        snprintf(text,sizeof(text),"体感 %s°C   湿度 %s%%   风 %s m/s",apparent,humidity,wind);
    } else snprintf(text,sizeof(text),"%s",w->network?"等待有效天气数据":"网络未连接，请在系统中连接 Wi-Fi");
    label(s,text,22,318,276,&font_weather13,s->sub);
    } else {
        label(s,"点击返回当前",188,121,112,&font_weather12,s->sub);
        unsigned next=0;
        while(next<d->hour_count&&d->hours[next].time<=now) next++;
        for(int i=0;i<5;i++) {
            int x=20+i*56;
            const weather_hour_t *h=d->valid&&w->time_valid&&next+(unsigned)i<d->hour_count?&d->hours[next+i]:NULL;
            if(h) { struct tm t; localtime_r(&h->time,&t); snprintf(text,sizeof(text),"%02d:00",t.tm_hour); }
            else strcpy(text,"--:--");
            lv_obj_t *l=label(s,text,x,169,56,&font_weather12,s->sub);
            lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
            weather_icon(s->body,x,195,56,h?h->code:-1,h?h->daytime:true,s->accent,s->bg);
            if(h) snprintf(text,sizeof(text),"%.0f°",(double)h->temperature); else strcpy(text,"--°");
            l=label(s,text,x,265,56,&font_weather16,h?weather_value_color(w->theme,w->dynamic_colors,WEATHER_TEMPERATURE,h->temperature):s->sub);
            lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
            if(h) snprintf(text,sizeof(text),"%d%%",h->precipitation); else strcpy(text,"--%");
            l=label(s,text,x,294,56,&font_weather12,s->sub);
            lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
        }
        label(s,"降水概率",22,318,276,&font_weather12,s->sub);
    }
    /* One transparent target covers only the middle weather panel. */
    button(s,"",12,106,296,234,ACT_HOURLY);
    button(s,weather_cities[w->city].name,12,106,74,44,ACT_CITIES);
    line(s,344); label(s,"未来三天",22,353,180,&font_weather12,s->sub);
    static const char *week[]={"周日","周一","周二","周三","周四","周五","周六"};
    for(int i=0;i<3;i++) {
        int x=22+i*94; time_t future=now+(i+1)*86400; struct tm f; localtime_r(&future,&f);
        const weather_day_t *day=NULL;
        for(int j=0;j<WEATHER_DAYS;j++) if(d->valid&&d->days[j].date==weather_date_key(&f)) day=&d->days[j];
        snprintf(text,sizeof(text),"%s %d日",i?week[f.tm_wday]:"明天",f.tm_mday);
        lv_obj_t *l=label(s,w->time_valid?text:"--",x,375,88,&font_weather12,s->sub); lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
        weather_icon(s->body,x+27,391,34,day?day->code:-1,true,s->accent,s->bg);
        if(day) {
            char low[32],high[32];
            weather_color_number(low,sizeof(low),w->theme,w->dynamic_colors,WEATHER_TEMPERATURE,day->low,0);
            weather_color_number(high,sizeof(high),w->theme,w->dynamic_colors,WEATHER_TEMPERATURE,day->high,0);
            snprintf(text,sizeof(text),"%s° / %s°",low,high);
        } else snprintf(text,sizeof(text),"-- / --");
        l=label(s,text,x,426,88,&font_weather12,s->ink); lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_CENTER,0);
    }
    button(s,"< 桌面",8,436,70,44,ACT_DESKTOP);
    button(s,"刷新",218,436,44,44,ACT_REFRESH); button(s,"设置",268,436,44,44,ACT_SETTINGS);
    const char *status=w->phase==WEATHER_FETCHING?"正在更新":!w->network?"离线":!w->time_valid?"等待校时":w->phase==WEATHER_LIMITED?"服务限流":w->phase==WEATHER_ERROR?"更新失败":!d->valid?"等待天气":stale?"天气已过期":now-d->data_time>=3600?"天气已过期":"已更新";
    if(d->valid) { struct tm t; localtime_r(&d->data_time,&t); snprintf(text,sizeof(text),"%s %02d:%02d",status,t.tm_hour,t.tm_min); }
    else snprintf(text,sizeof(text),"%s",status);
    s->stamp=label(s,text,80,450,135,&font_weather12,s->sub); lv_obj_set_style_text_align(s->stamp,LV_TEXT_ALIGN_CENTER,0);
    update_time(s);
}
static void status_row(state_t *s,const char *name,const char *value,int y)
{
    label(s,name,22,y+12,110,&font_weather12,s->ink);
    lv_obj_t *l=label(s,value,130,y+12,168,&font_weather12,s->sub); lv_obj_set_style_text_align(l,LV_TEXT_ALIGN_RIGHT,0); line(s,y+44);
}
static void draw(state_t *s)
{
    weather_snapshot(&s->snapshot); s->revision=s->snapshot.revision;
    const weather_palette_t *p=weather_palette(s->snapshot.theme);
    s->bg=lv_color_hex(p->bg); s->ink=lv_color_hex(p->ink); s->sub=lv_color_hex(p->sub);
    s->line=lv_color_hex(p->line); s->accent=lv_color_hex(p->accent);
    lv_obj_clean(s->body); s->clock=s->date=s->stamp=s->message=NULL; s->minute=s->date_key=s->shown_second=-1;
    lv_obj_set_style_bg_color(s->body,s->bg,0); lv_obj_set_style_bg_opa(s->body,LV_OPA_COVER,0);
    if(s->page==HOME) home(s);
    else if(s->page==SETTINGS) {
        header(s,"应用设置"); row(s,"天气城市","单城市 · 中国地区",weather_cities[s->snapshot.city].name,72,ACT_CITIES);
        row(s,"字体与配色","桌面与应用统一外观",weather_theme_name(s->snapshot.theme),139,ACT_APPEARANCE);
        row(s,"天气与时间","更新、校时及连接状态","查看 >",206,ACT_STATUS);
        char version[40]; snprintf(version,sizeof(version),"%s >",esp_app_get_description()->version);
        row(s,"关于","天气来源与版本",version,273,ACT_ABOUT);
        label(s,"Wi-Fi、背光及熄屏时间\n请在外部系统设置中操作。",22,366,276,&font_weather12,s->sub);
    } else if(s->page==CITIES) {
        header(s,"天气城市");
        for(int i=0;i<WEATHER_CITIES;i++) row(s,weather_cities[i].name,weather_cities[i].region,i==(int)s->snapshot.city?"已选择":"选择 >",72+i*65,ACT_CITY+i);
        label(s,"选择后保存，并自动更新天气。",22,421,276,&font_weather12,s->sub);
    } else if(s->page==APPEARANCE) {
        header(s,"字体与配色");
        static const char *subtitles[]={"饱满粗体 · 米白与墨绿","修长窄体 · 雾蓝与深蓝","圆润数字 · 曜石与暖金"};
        for(unsigned i=0;i<WEATHER_THEMES;i++) row(s,weather_theme_name(i),subtitles[i],s->snapshot.theme==i?"已选择":"选择 >",72+i*67,ACT_THEME+i);
        row(s,"数值随高低变色","温度、湿度、风速",s->snapshot.dynamic_colors?"已开启 >":"已关闭 >",273,ACT_COLORS);
        label(s,"16:28",22,354,276,weather_clock_font(s->snapshot.theme),s->ink);
        label(s,"设置立即生效，重启后保留。",22,445,276,&font_weather12,s->sub);
    } else if(s->page==STATUS) {
        header(s,"天气与时间"); weather_snapshot_t *w=&s->snapshot;
        char text[80];
        status_row(s,"网络状态",w->network?"已连接":"未连接",73);
        const char *phase[]={"等待更新","等待系统 Wi-Fi","等待校时","正在更新","已更新","更新失败","服务限流"};
        status_row(s,"天气状态",phase[w->phase],118);
        if(w->data.valid) { struct tm t; localtime_r(&w->data.data_time,&t); strftime(text,sizeof(text),"%m-%d %H:%M",&t); } else strcpy(text,"--");
        status_row(s,"数据时间",text,163); status_row(s,"自动更新","每15分钟",208); status_row(s,"时区","北京 UTC+8",253);
        if(w->last_sync) { struct tm t; localtime_r(&w->last_sync,&t); strftime(text,sizeof(text),"%H:%M",&t); } else strcpy(text,"等待校时");
        status_row(s,"最近校时",text,298);
        button(s,"刷新天气",22,360,276,48,ACT_REFRESH);
        snprintf(text,sizeof(text),"HTTP %d · %s\n联网请返回系统 Wi-Fi。",w->http_status,esp_err_to_name(w->error));
        label(s,text,22,422,276,&font_weather12,s->sub);
    } else {
        header(s,"关于"); label(s,"天气时钟",22,97,276,&font_weather16,s->ink);
        char about[768]; snprintf(about,sizeof(about),"TouchDesk %s\n\n天气来源：Open-Meteo\n天气数据：CC BY 4.0\nopen-meteo.com\ncreativecommons.org/licenses/by/4.0\n\n农历：香港天文台日期对照表\n离线农历支持 2024—2035 年\n\n室外模型天气，不是室内温度。",esp_app_get_description()->version);
        label(s,about,22,143,276,&font_weather12,s->sub);
    }
    s->message=label(s,"",18,405,284,&font_weather12,s->bg);
    lv_obj_set_style_bg_color(s->message,s->ink,0); lv_obj_set_style_bg_opa(s->message,LV_OPA_COVER,0);
    lv_obj_set_style_pad_all(s->message,10,0); lv_obj_set_style_radius(s->message,6,0);
    lv_obj_set_style_text_align(s->message,LV_TEXT_ALIGN_CENTER,0); lv_obj_add_flag(s->message,LV_OBJ_FLAG_HIDDEN);
}
static void tick(lv_timer_t *timer)
{
    app_ctx_t *ctx=timer->user_data; state_t *s=ctx->state;
    weather_snapshot_t latest; weather_snapshot(&latest);
    time_t now=weather_now(); struct tm t; localtime_r(&now,&t);
    bool day_changed=latest.time_valid&&s->date_key>0&&weather_date_key(&t)!=s->date_key;
    bool minute_changed=s->page==HOME&&latest.time_valid&&s->minute>=0&&s->minute!=t.tm_hour*60+t.tm_min;
    if(s->change||latest.revision!=s->revision||day_changed||minute_changed) { if(s->change) s->page=s->pending; s->change=false; draw(s); }
    else update_time(s);
    if(s->message_until&&esp_timer_get_time()>s->message_until) { lv_obj_add_flag(s->message,LV_OBJ_FLAG_HIDDEN); s->message_until=0; }
}
static esp_err_t create(app_ctx_t *ctx,lv_obj_t *root)
{
    esp_err_t e=weather_start(); if(e!=ESP_OK) return e;
    state_t *s=ctx->state; s->ctx=ctx; s->page=HOME; s->parent=HOME;
    s->body=lv_obj_create(root); lv_obj_remove_style_all(s->body); lv_obj_set_size(s->body,320,480);
    lv_obj_clear_flag(s->body,LV_OBJ_FLAG_SCROLLABLE); weather_activate(true); draw(s);
    return app_timer_create(ctx,tick,250)?ESP_OK:ESP_ERR_NO_MEM;
}
static void destroy(app_ctx_t *ctx) { (void)ctx; weather_activate(false); }
static bool back(app_ctx_t *ctx)
{
    state_t *s=ctx->state; if(s->page==HOME) return false;
    s->pending=s->page==SETTINGS?HOME:s->page==CITIES?s->parent:SETTINGS; s->change=true; return true;
}
const app_desc_t app_weather={.id="weather",.title="天气时钟",.subtitle="时间、农历和天气",.caps=APP_CAP_FULLSCREEN|APP_CAP_NETWORK|APP_CAP_WEATHER,.state_size=sizeof(state_t),.create=create,.destroy=destroy,.back=back};
