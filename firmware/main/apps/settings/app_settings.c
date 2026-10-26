#include "app.h"
#include "ui.h"
#include "shell.h"
#include "settings.h"
#include "weather.h"
#include "weather_style.h"
extern const app_desc_t app_settings,app_system_info,app_counter,app_microphone;
LV_FONT_DECLARE(font_weather12);
LV_FONT_DECLARE(font_weather16);
static void system_info(lv_event_t *e)
{
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) app_request(&app_system_info);
}
static void microphone_open(lv_event_t *e) { if(lv_event_get_code(e)==LV_EVENT_CLICKED) app_request(&app_microphone); }
static void counter(lv_event_t *e) { if(lv_event_get_code(e)==LV_EVENT_CLICKED) app_request(&app_counter); }
static void appearance(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED) return;
    unsigned theme=lv_dropdown_get_selected(lv_event_get_target(e));
    if(weather_choose_theme(theme)!=ESP_OK) {
        lv_dropdown_set_selected(lv_event_get_target(e),ui_theme());
        shell_notice("外观保存失败"); return;
    }
    app_request(&app_settings);
}
static void number_colors(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED) return;
    bool enabled=lv_obj_has_state(lv_event_get_target(e),LV_STATE_CHECKED);
    if(weather_choose_colors(enabled)!=ESP_OK) {
        weather_snapshot_t s; weather_snapshot(&s);
        if(s.dynamic_colors) lv_obj_add_state(lv_event_get_target(e),LV_STATE_CHECKED);
        else lv_obj_clear_state(lv_event_get_target(e),LV_STATE_CHECKED);
        shell_notice("数值颜色保存失败");
    }
}
static void dropdown_style(lv_obj_t *dropdown)
{
    lv_dropdown_set_symbol(dropdown,NULL);
    lv_obj_t *arrow=lv_label_create(dropdown); lv_label_set_text(arrow,LV_SYMBOL_DOWN);
    lv_obj_set_style_text_font(arrow,&lv_font_montserrat_14,0);
    lv_obj_align(arrow,LV_ALIGN_RIGHT_MID,-8,0);
    lv_obj_clear_flag(arrow,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(dropdown,&font_weather16,0);
    lv_obj_set_style_bg_color(dropdown,ui_surface(),0);
    lv_obj_set_style_text_color(dropdown,ui_foreground(),0);
    lv_obj_t *list=lv_dropdown_get_list(dropdown);
    lv_obj_set_style_text_font(list,&font_weather16,0);
    lv_obj_set_style_bg_color(list,ui_surface(),0);
    lv_obj_set_style_text_color(list,ui_foreground(),0);
    lv_obj_set_style_bg_color(list,lv_color_hex(weather_palette(ui_theme())->accent),LV_PART_SELECTED|LV_STATE_CHECKED);
}
static const uint32_t durations[] = {30,60,120,300,600,0};
static unsigned selected_duration(void)
{
    for(unsigned i=0;i<sizeof(durations)/sizeof(durations[0]);i++)
        if(durations[i]==settings_sleep_seconds()) return i;
    return 2;
}
static void change_duration(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED) return;
    lv_obj_t *dropdown=lv_event_get_target(e);
    unsigned selected=lv_dropdown_get_selected(dropdown);
    if(selected>=sizeof(durations)/sizeof(durations[0])) return;
    if(settings_set_sleep_seconds(durations[selected])!=ESP_OK) {
        lv_dropdown_set_selected(dropdown,selected_duration());
        shell_notice("设置保存失败");
    } else shell_notice("设置已保存");
}
static void sleep_now(lv_event_t *e)
{
    if(lv_event_get_code(e)==LV_EVENT_CLICKED) shell_sleep();
}
static esp_err_t create(app_ctx_t *ctx,lv_obj_t *root)
{
    ui_label(root,"显示与休眠",12,8,296);
    ui_label(root,"自动熄屏时间",12,43,296);
    lv_obj_t *dropdown=lv_dropdown_create(root);
    lv_obj_set_pos(dropdown,12,72); lv_obj_set_size(dropdown,296,40);
    lv_dropdown_set_options(dropdown,"30 秒\n1 分钟\n2 分钟\n5 分钟\n10 分钟\n永不熄屏");
    lv_dropdown_set_selected(dropdown,selected_duration());
    dropdown_style(dropdown);
    if(!settings_available()) lv_obj_add_state(dropdown,LV_STATE_DISABLED);
    lv_obj_add_event_cb(dropdown,change_duration,LV_EVENT_VALUE_CHANGED,NULL);
    ui_label(root,"字体与配色",12,118,296);
    lv_obj_t *theme=lv_dropdown_create(root);
    lv_obj_set_pos(theme,12,146); lv_obj_set_size(theme,296,40);
    lv_dropdown_set_options(theme,"纸白墨绿\n雾蓝科技\n曜石暖金"); lv_dropdown_set_selected(theme,ui_theme());
    dropdown_style(theme);
    if(!settings_available()) lv_obj_add_state(theme,LV_STATE_DISABLED);
    lv_obj_add_event_cb(theme,appearance,LV_EVENT_VALUE_CHANGED,NULL);
    ui_label(root,"数值随高低变色",12,203,228);
    lv_obj_t *colors=lv_switch_create(root); lv_obj_set_pos(colors,248,199); lv_obj_set_size(colors,60,32);
    weather_snapshot_t snapshot; weather_snapshot(&snapshot);
    if(snapshot.dynamic_colors) lv_obj_add_state(colors,LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(colors,lv_color_hex(weather_palette(snapshot.theme)->line),LV_PART_MAIN);
    lv_obj_set_style_bg_color(colors,lv_color_hex(weather_palette(snapshot.theme)->accent),LV_PART_INDICATOR|LV_STATE_CHECKED);
    if(!settings_available()) lv_obj_add_state(colors,LV_STATE_DISABLED);
    lv_obj_add_event_cb(colors,number_colors,LV_EVENT_VALUE_CHANGED,NULL);
    lv_obj_t *hint=ui_label(root,"温度、湿度、风速 · 设置自动保存",12,239,296);
    lv_obj_set_style_text_font(hint,&font_weather12,0);
    ui_button(root,"立即熄屏",12,276,140,44,sleep_now,NULL);
    ui_button(root,"系统信息",164,276,144,44,system_info,NULL);
    ui_button(root,"计数器",12,328,140,36,counter,NULL);
    ui_button(root,"麦克风测试",164,328,144,36,microphone_open,NULL);
    return ESP_OK;
}
const app_desc_t app_settings={.id="settings",.title="设置",.subtitle="显示与休眠",.create=create};
