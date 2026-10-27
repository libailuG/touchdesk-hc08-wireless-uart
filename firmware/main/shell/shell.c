#include "shell.h"
#include "ui.h"
#include "app.h"
#include "board.h"
#include "settings.h"
#include "usb_service.h"
#include "wifi_service.h"
#include "desktop.h"
#include "weather.h"
#include "weather_style.h"
#include "esp_timer.h"
#include "esp_log.h"
static lv_obj_t *title,*usb,*notice;
static lv_obj_t *app_content,*back_button,*home_button;
LV_FONT_DECLARE(font_weather12);
static void apply_theme(void)
{
    weather_snapshot_t snapshot; weather_snapshot(&snapshot); ui_set_theme(snapshot.theme);
    ui_reset_page(app_content);
    lv_obj_set_style_bg_color(lv_scr_act(),ui_background(),0);
    lv_obj_set_style_text_color(lv_scr_act(),ui_foreground(),0);
    lv_obj_set_style_text_color(title,ui_foreground(),0);
    lv_obj_set_style_text_color(usb,ui_foreground(),0);
    lv_obj_set_style_text_color(notice,lv_color_hex(weather_palette(snapshot.theme)->accent),0);
    lv_obj_t *nav[]={back_button,home_button};
    for(unsigned i=0;i<2;i++) {
        ui_style_button(nav[i]);
    }
}
void shell_fullscreen(bool enabled)
{
    lv_obj_t *chrome[]={title,usb,notice,back_button,home_button};
    for(unsigned i=0;i<5;i++) {
        if(enabled) lv_obj_add_flag(chrome[i],LV_OBJ_FLAG_HIDDEN);
        else lv_obj_clear_flag(chrome[i],LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_pos(app_content,0,enabled?0:62);
    lv_obj_set_size(app_content,320,enabled?480:360);
    lv_obj_set_pos(title,12,8); lv_obj_set_style_text_font(title,&font_ui,0);
    lv_obj_set_style_text_font(notice,&font_ui,0);
    lv_obj_set_pos(notice,12,35); lv_obj_set_size(notice,296,26);
    apply_theme();
}
static int64_t notice_until;
static bool asleep;
void shell_title(const char *text)
{
    lv_label_set_text(title,text);
    lv_label_set_text(notice,""); notice_until=0;
}
void shell_notice(const char *text)
{
    lv_label_set_text(notice,text);
    notice_until=esp_timer_get_time()+4000000;
}
static void home(lv_event_t *e)
{
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) app_request(NULL);
}
static void back(lv_event_t *e)
{
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) app_request_back();
}
void shell_desktop(lv_obj_t *root)
{
    lv_obj_set_pos(root,0,36); lv_obj_set_size(root,320,444);
    lv_obj_add_flag(back_button,LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(home_button,LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(title,"TouchDesk"); lv_obj_set_style_text_font(title,&font_weather12,0);
    lv_obj_set_pos(title,18,10);
    lv_obj_set_style_text_font(notice,&font_weather12,0);
    lv_obj_set_pos(notice,16,462); lv_obj_set_size(notice,288,18);
    lv_label_set_long_mode(notice,LV_LABEL_LONG_CLIP);
    desktop_create(root); apply_theme();
}
void shell_init(void)
{
    lv_obj_t *screen=lv_scr_act();
    lv_obj_clear_flag(screen,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen,lv_color_hex(0x101b2b),0);
    lv_obj_set_style_text_color(screen,lv_color_hex(0xe8f0f8),0);
    lv_obj_set_style_text_font(screen,&font_ui,0);
    title=ui_label(screen,"桌面",12,8,178);
    usb=ui_label(screen,"",176,10,126);
    lv_obj_set_style_text_font(usb,&lv_font_montserrat_14,0);
    lv_obj_set_style_text_align(usb,LV_TEXT_ALIGN_RIGHT,0);
    notice=ui_label(screen,"",12,35,296);
    lv_obj_set_style_text_color(notice,lv_color_hex(0x62d5e8),0);
    lv_obj_t *content=lv_obj_create(screen);
    app_content=content;
    lv_obj_set_pos(content,0,62); lv_obj_set_size(content,320,360);
    lv_obj_remove_style_all(content);
    lv_obj_set_pos(content,0,62); lv_obj_set_size(content,320,360);
    lv_obj_clear_flag(content,LV_OBJ_FLAG_SCROLLABLE);
    back_button=ui_button(screen,"返回",10,430,140,42,back,NULL);
    home_button=ui_button(screen,"主页",170,430,140,42,home,NULL);
    app_platform_init(content); app_request(NULL);
}
void shell_sleep(void)
{
    if(asleep) return;
    extern void xiaozhi_leave(void);
    xiaozhi_leave();
    usb_service_cancel();
    board_input_cancel();
    if(board_backlight(false)==ESP_OK) asleep=true;
    else shell_notice("背光操作失败");
}
void shell_poll(void)
{
    /* Physical touch activity is independent of LVGL, including while gated. */
    if(asleep && board_touch_down()) {
        board_input_cancel(); usb_service_cancel();
        if(board_backlight(true)==ESP_OK) asleep=false;
    }
    uint32_t timeout = settings_sleep_seconds();
    if(!asleep && timeout && board_idle_us()>((int64_t)timeout*1000000)) shell_sleep();
    static int previous=-1,previous_visible=-1;
    static bool was_connecting;
    static int64_t connecting_since;
    int state=usb_service_online() ? (usb_service_is_ble()?2:1) : 0;
    wifi_status_t network; wifi_service_status(&network);
    bool connecting=network.enabled && (network.state==NET_CONNECTING ||
        network.state==NET_GETTING_IP || network.state==NET_RETRY);
    int64_t now=esp_timer_get_time();
    if(connecting && !was_connecting) connecting_since=now;
    was_connecting=connecting;
    bool visible=network.enabled && (network.state==NET_CONNECTED ||
        (connecting && ((now-connecting_since)/500000)%2==0));
    if(state!=previous || (int)visible!=previous_visible) {
        lv_label_set_text_fmt(usb,"%s%s%s",visible?LV_SYMBOL_WIFI:"",
            visible&&state?"  ":"",state==2?LV_SYMBOL_BLUETOOTH:state?LV_SYMBOL_USB:"");
        previous=state; previous_visible=visible;
    }
    if(notice_until && esp_timer_get_time()>notice_until) {
        notice_until=0; lv_label_set_text(notice,"");
    }
    desktop_poll();
}
