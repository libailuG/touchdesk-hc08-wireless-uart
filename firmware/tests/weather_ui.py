"""Render the real firmware UI and check navigation, all weather states, and app lifecycle."""
from pathlib import Path
import argparse, subprocess, tempfile
from ui_simulator import HEADERS, EXTRA_HEADERS, HOST, ROOT

MOCK=r'''
#include "weather.h"
#include "weather_style.h"
#include "cJSON.h"
static weather_snapshot_t weather={.theme=WEATHER_DEFAULT_THEME,.dynamic_colors=true};
static bool mock_save_failure;
static uint32_t persisted_config;
static bool app_owner,desktop_owner;
static bool simulated_usb=true;
static time_t simulated_now=1792398480;
time_t sim_time(time_t *out) { if(out) *out=simulated_now; return simulated_now; }
time_t weather_now(void) { return sim_time(NULL); }
struct tm *sim_localtime_r(const time_t *epoch,struct tm *out) {
    time_t local=*epoch+8*3600; struct tm *value=gmtime(&local); if(!value) return NULL; *out=*value; return out;
}
esp_err_t weather_start(void) { return ESP_OK; }
void weather_activate(bool active) { app_owner=active; weather.active=app_owner||desktop_owner; weather.revision++; }
void weather_desktop_activate(bool active) { desktop_owner=active; weather.active=app_owner||desktop_owner; weather.revision++; }
void weather_snapshot(weather_snapshot_t *out) { *out=weather; }
esp_err_t weather_choose_city(unsigned city) { weather.city=city; weather.data.valid=false; weather.revision++; return ESP_OK; }
esp_err_t weather_choose_theme(unsigned theme) {
    if(mock_save_failure) return ESP_ERR_INVALID_STATE;
    if(theme>=WEATHER_THEMES) return ESP_ERR_INVALID_ARG;
    persisted_config=weather_config_encode(weather.city,theme,weather.dynamic_colors);
    weather.theme=theme; weather.revision++; return ESP_OK;
}
esp_err_t weather_choose_colors(bool dynamic) {
    if(mock_save_failure) return ESP_ERR_INVALID_STATE;
    persisted_config=weather_config_encode(weather.city,weather.theme,dynamic);
    weather.dynamic_colors=dynamic; weather.revision++; return ESP_OK;
}
bool weather_refresh(void) { return weather.network&&weather.time_valid; }
static lv_obj_t *body(void) { return lv_obj_get_child(content(),0); }
static lv_obj_t *desktop_button(const app_desc_t *app) {
    for(unsigned i=0;i<lv_obj_get_child_cnt(body());i++) {
        lv_obj_t *o=lv_obj_get_child(body(),i);
        if(lv_obj_check_type(o,&lv_btn_class)&&lv_obj_get_user_data(o)==app) return o;
    }
    assert(!"missing desktop button"); return NULL;
}
static lv_obj_t *action(int id) {
    for(unsigned i=0;i<lv_obj_get_child_cnt(body());i++) {
        lv_obj_t *o=lv_obj_get_child(body(),i);
        if(lv_obj_check_type(o,&lv_btn_class)&&(int)(intptr_t)lv_obj_get_user_data(o)==id) return o;
    }
    assert(!"missing action"); return NULL;
}
static bool has_text(const char *text) {
    for(unsigned i=0;i<lv_obj_get_child_cnt(body());i++) {
        lv_obj_t *o=lv_obj_get_child(body(),i);
        if(lv_obj_check_type(o,&lv_label_class)&&!strcmp(lv_label_get_text(o),text)) return true;
    }
    return false;
}
static void valid_weather(void) {
    weather.network=true; weather.time_valid=true; weather.phase=WEATHER_OK; weather.revision++;
    weather.data=(weather_data_t){.valid=true,.daytime=true,.temperature=23,.apparent=24,.humidity=64,.wind=2.1,.code=2,.fetched=sim_time(NULL),.data_time=sim_time(NULL)};
    weather.data.hour_count=WEATHER_HOURS;
    for(int i=0;i<WEATHER_HOURS;i++) weather.data.hours[i]=(weather_hour_t){sim_time(NULL)/3600*3600+i*3600,23-i*0.3f,i%3?2:61,20+i,i<5};
    for(int i=0;i<4;i++) { time_t t=sim_time(NULL)+i*86400; struct tm tm; sim_localtime_r(&t,&tm);
        weather.data.days[i]=(weather_day_t){weather_date_key(&tm),i==1?61:i==2?0:2,17+i,23+i}; }
}
int main(void) {
    for(unsigned city=0;city<WEATHER_CITIES;city++) for(unsigned theme=0;theme<WEATHER_THEMES;theme++) for(unsigned dynamic=0;dynamic<2;dynamic++) {
        unsigned c=99,t=99; bool d=false;
        assert(weather_config_decode(weather_config_encode(city,theme,dynamic),&c,&t,&d)&&c==city&&t==theme&&d==dynamic);
    }
    unsigned c=99,t=99; bool dynamic=false;
    assert(!weather_config_decode(0x10000,&c,&t,&dynamic));
    assert(!weather_config_decode(0x20300,&c,&t,&dynamic));
    assert(!weather_config_decode(0x201ff,&c,&t,&dynamic));
    assert(!weather_config_decode(0x20900,&c,&t,&dynamic));
    for(unsigned theme=0;theme<WEATHER_THEMES;theme++) {
        const lv_font_t *font=weather_clock_font(theme);
        assert(lv_txt_get_width("88:88",5,font,0,LV_TEXT_FLAG_NONE)<=288);
        assert(lv_txt_get_width("88:88:88",8,weather_clock_seconds_font(theme),0,LV_TEXT_FLAG_NONE)<=320);
        assert(weather_clock_font(theme)!=weather_clock_font((theme+1)%WEATHER_THEMES));
        assert(weather_value_color(theme,true,WEATHER_TEMPERATURE,9.9f).full!=weather_value_color(theme,true,WEATHER_TEMPERATURE,10).full);
        assert(weather_value_color(theme,true,WEATHER_TEMPERATURE,27.9f).full!=weather_value_color(theme,true,WEATHER_TEMPERATURE,28).full);
        assert(weather_value_color(theme,true,WEATHER_TEMPERATURE,34.9f).full!=weather_value_color(theme,true,WEATHER_TEMPERATURE,35).full);
        assert(weather_value_color(theme,true,WEATHER_HUMIDITY,39.9f).full!=weather_value_color(theme,true,WEATHER_HUMIDITY,40).full);
        assert(weather_value_color(theme,true,WEATHER_HUMIDITY,70).full!=weather_value_color(theme,true,WEATHER_HUMIDITY,70.1f).full);
        assert(weather_value_color(theme,true,WEATHER_WIND,2.9f).full!=weather_value_color(theme,true,WEATHER_WIND,3).full);
        assert(weather_value_color(theme,true,WEATHER_WIND,5.9f).full!=weather_value_color(theme,true,WEATHER_WIND,6).full);
        assert(weather_value_color(theme,true,WEATHER_WIND,9.9f).full!=weather_value_color(theme,true,WEATHER_WIND,10).full);
        assert(weather_value_color(theme,false,WEATHER_TEMPERATURE,40).full==lv_color_hex(weather_palette(theme)->ink).full);
    }
    puts("PASS appearance configuration: all 30 combinations round-trip, corrupt/legacy values rejected, three distinct fonts fit seconds, color thresholds and disabled state");
    FILE *fixture=fopen("../../work/weather-live.json","rb"); assert(fixture);
    char json[16385]; size_t length=fread(json,1,16384,fixture); fclose(fixture); json[length]=0;
    cJSON *root=cJSON_Parse(json); assert(root);
    time_t fixture_now=(time_t)cJSON_GetObjectItem(cJSON_GetObjectItem(root,"current"),"time")->valuedouble+60;
    weather_data_t parsed; assert(weather_parse(json,0,fixture_now,&parsed)); assert(parsed.valid);
    cJSON *hours=cJSON_AddObjectToObject(root,"hourly");
    const char *hk[]={"time","temperature_2m","weather_code","precipitation_probability","is_day"};
    for(int k=0;k<5;k++) {
        cJSON *a=cJSON_AddArrayToObject(hours,hk[k]);
        for(int i=0;i<24;i++) cJSON_AddItemToArray(a,cJSON_CreateNumber(k==0?fixture_now/3600*3600+i*3600:k==1?22:k==2?61:k==3?35:1));
    }
    char *with_hours=cJSON_PrintUnformatted(root);
    assert(weather_parse(with_hours,0,fixture_now,&parsed)&&parsed.hour_count==24);
    assert(parsed.hours[1].precipitation==35&&parsed.hours[1].code==61);
    cJSON_free(with_hours);
    cJSON_ReplaceItemInArray(cJSON_GetObjectItem(hours,"temperature_2m"),1,cJSON_CreateNull());
    with_hours=cJSON_PrintUnformatted(root); assert(weather_parse(with_hours,0,fixture_now,&parsed)&&parsed.hour_count==23); cJSON_free(with_hours);
    assert(!weather_parse("{}",0,fixture_now,&parsed)); assert(!weather_parse("{",0,fixture_now,&parsed));
    cJSON_ReplaceItemInObject(cJSON_GetObjectItem(root,"current"),"temperature_2m",cJSON_CreateNull());
    char *bad=cJSON_PrintUnformatted(root); assert(!weather_parse(bad,0,fixture_now,&parsed)); cJSON_free(bad); cJSON_Delete(root);
    fixture=fopen("../../work/weather-hourly-live.json","rb"); assert(fixture);
    length=fread(json,1,16384,fixture); fclose(fixture); json[length]=0;
    root=cJSON_Parse(json); assert(root);
    fixture_now=(time_t)cJSON_GetObjectItem(cJSON_GetObjectItem(root,"current"),"time")->valuedouble+60;
    assert(weather_parse(json,0,fixture_now,&parsed)&&parsed.hour_count==24); cJSON_Delete(root);
    lv_init(); static lv_disp_draw_buf_t buf; static lv_disp_drv_t drv;
    lv_disp_draw_buf_init(&buf,draw,NULL,320*40);
    lv_disp_drv_init(&drv); drv.hor_res=320; drv.ver_res=480; drv.flush_cb=flush; drv.draw_buf=&buf;
    assert(lv_disp_drv_register(&drv)); shell_init(); app_process();
    lv_obj_t *status_icon=lv_obj_get_child(lv_scr_act(),1);
    net.enabled=false; shell_poll(); assert(!strcmp(lv_label_get_text(status_icon),LV_SYMBOL_USB));
    net.enabled=true; net.state=NET_CONNECTING; shell_poll();
    assert(!strcmp(lv_label_get_text(status_icon),LV_SYMBOL_WIFI "  " LV_SYMBOL_USB));
    lv_tick_inc(499); shell_poll(); assert(strstr(lv_label_get_text(status_icon),LV_SYMBOL_WIFI));
    lv_tick_inc(1); shell_poll(); assert(!strstr(lv_label_get_text(status_icon),LV_SYMBOL_WIFI));
    lv_tick_inc(500); shell_poll(); assert(strstr(lv_label_get_text(status_icon),LV_SYMBOL_WIFI));
    net.state=NET_GETTING_IP; lv_tick_inc(500); shell_poll(); assert(!strstr(lv_label_get_text(status_icon),LV_SYMBOL_WIFI));
    net.state=NET_CONNECTED; shell_poll(); assert(strstr(lv_label_get_text(status_icon),LV_SYMBOL_WIFI));
    lv_tick_inc(500); shell_poll(); assert(strstr(lv_label_get_text(status_icon),LV_SYMBOL_WIFI));
    snapshot("wifi-icon-connected");
    net.state=NET_RETRY; shell_poll(); assert(strstr(lv_label_get_text(status_icon),LV_SYMBOL_WIFI));
    lv_tick_inc(500); shell_poll(); assert(!strstr(lv_label_get_text(status_icon),LV_SYMBOL_WIFI));
    net.state=NET_ERROR; shell_poll(); assert(!strstr(lv_label_get_text(status_icon),LV_SYMBOL_WIFI));
    net.state=NET_IDLE; net.scanning=true; shell_poll(); assert(!strstr(lv_label_get_text(status_icon),LV_SYMBOL_WIFI));
    snapshot("wifi-icon-disconnected");
    net.state=NET_CONNECTED; net.enabled=false; shell_poll(); assert(!strstr(lv_label_get_text(status_icon),LV_SYMBOL_WIFI));
    net.enabled=true; net.scanning=false; shell_poll();
    simulated_usb=false; shell_poll(); assert(!strcmp(lv_label_get_text(status_icon),LV_SYMBOL_WIFI));
    net.enabled=false; shell_poll(); assert(!strcmp(lv_label_get_text(status_icon),""));
    simulated_usb=true; net.enabled=true; shell_poll();
    puts("PASS status icons: Wi-Fi steady after IP, 500ms connect/DHCP/retry blink, hidden idle/error/off/scan; USB connected only, no text markers");
    char lunar[64]; assert(weather_lunar(2026,10,19,lunar,sizeof(lunar))&&!strcmp(lunar,"九月初十"));
    assert(weather_lunar(2026,2,17,lunar,sizeof(lunar))&&!strcmp(lunar,"正月初一"));
    assert(!weather_lunar(2040,1,1,lunar,sizeof(lunar)));
    extern const app_desc_t app_weather,app_keysim,app_counter,app_wifi,app_settings,app_system_info;
    assert(weather.active&&desktop_owner&&!app_owner);
    assert(lv_obj_get_height(content())==444);
    assert(lv_obj_has_flag(lv_obj_get_child(lv_scr_act(),4),LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_has_flag(lv_obj_get_child(lv_scr_act(),5),LV_OBJ_FLAG_HIDDEN));
    assert(lv_obj_get_child_cnt(body())==7);
    snapshot("desktop-first-open"); check_bounds(body());
    valid_weather(); snapshot("desktop-light"); check_bounds(body());
    lv_obj_t *clock_label=lv_obj_get_child(body(),0);
    const lv_font_t *clock_font=lv_obj_get_style_text_font(clock_label,LV_PART_MAIN);
    assert(strlen(lv_label_get_text(clock_label))==5);
    click(clock_label); assert(strlen(lv_label_get_text(clock_label))==8);
    assert(lv_obj_get_style_text_font(clock_label,LV_PART_MAIN)==clock_font);
    assert(lv_txt_get_width("88:88:88",8,clock_font,0,LV_TEXT_FLAG_NONE)<=lv_obj_get_width(clock_label));
    snapshot("desktop-seconds"); check_bounds(body());
    char old_seconds[16]; strcpy(old_seconds,lv_label_get_text(clock_label));
    simulated_now++; lv_tick_inc(1000); shell_poll();
    assert(strcmp(old_seconds,lv_label_get_text(clock_label))!=0);
    /* One navigation round-trip must retain the seconds display preference. */
    extern const app_desc_t app_counter;
    app_request(&app_counter); app_process(); app_request(NULL); app_process(); settle();
    clock_label=lv_obj_get_child(body(),0); assert(strlen(lv_label_get_text(clock_label))==8);
    click(clock_label); assert(strlen(lv_label_get_text(clock_label))==5);
    weather.time_valid=false; weather.revision++; lv_tick_inc(300); shell_poll();
    click(clock_label); assert(!strcmp(lv_label_get_text(clock_label),"--:--:--"));
    click(clock_label); assert(!strcmp(lv_label_get_text(clock_label),"--:--"));
    valid_weather(); simulated_now--; lv_tick_inc(300); shell_poll();
    puts("PASS clock seconds: click toggles HH:MM/HH:MM:SS, same 72px font, full width fits, seconds advance, navigation retains choice, unsynced placeholders correct");
    unsigned notice_before=0,notice_after=0;
    for(int y=462;y<480;y++) for(int x=16;x<304;x++) notice_before+=frame[y*320+x].full;
    shell_notice("设置已保存"); snapshot("desktop-notice");
    for(int y=462;y<480;y++) for(int x=16;x<304;x++) notice_after+=frame[y*320+x].full;
    assert(notice_before!=notice_after); lv_tick_inc(4001); shell_poll(); settle();
    net.state=NET_CONNECTED; weather.network=false; weather.revision++; snapshot("desktop-offline");
    valid_weather(); weather.data.data_time-=7*3600; weather.revision++; snapshot("desktop-stale");
    valid_weather();
    click(desktop_button(&app_keysim)); assert(app_current()==&app_keysim&&!desktop_owner);
    assert(lv_obj_get_height(content())==360); assert(!lv_obj_has_flag(lv_obj_get_child(lv_scr_act(),4),LV_OBJ_FLAG_HIDDEN));
    app_request(NULL); app_process(); settle(); assert(desktop_owner&&weather.active);
    click(desktop_button(&app_counter)); assert(app_current()==&app_counter); app_request(NULL); app_process(); settle();
    click(desktop_button(&app_wifi)); assert(app_current()==&app_wifi); snapshot("wifi-paper");
    net.has_saved=true; net.saved_count=1; net.saved_revision++;
    click(lv_obj_get_child(content(),4)); snapshot("wifi-saved-list"); check_bounds(content());
    click(lv_obj_get_child(lv_obj_get_child(content(),1),0)); snapshot("wifi-saved-actions"); check_bounds(content());
    click(lv_obj_get_child(content(),3)); snapshot("wifi-update-password"); check_bounds(content());
    app_request_back(); app_process(); settle();
    click(lv_obj_get_child(content(),4));
    click(lv_obj_get_child(lv_obj_get_child(content(),1),0));
    click(lv_obj_get_child(content(),4)); snapshot("wifi-forget-one"); check_bounds(content());
    click(lv_obj_get_child(content(),3)); snapshot("wifi-saved-empty"); assert(!net.has_saved); check_bounds(content());
    app_request(NULL); app_process(); settle();
    puts("PASS saved-network UI: list, connect/edit/forget actions, password page, per-network deletion and empty state");
    click(desktop_button(&app_settings)); assert(app_current()==&app_settings); snapshot("system-settings-paper"); check_bounds(content());
    /* Existing system settings has duration dropdown at child2 and theme at child4. */
    lv_obj_t *theme_dropdown=lv_obj_get_child(content(),4);
    lv_dropdown_set_selected(theme_dropdown,2); lv_event_send(theme_dropdown,LV_EVENT_VALUE_CHANGED,NULL); app_process(); settle();
    assert(weather.theme==2&&app_current()==&app_settings); snapshot("system-settings-dark");
    lv_obj_t *colors_switch=lv_obj_get_child(content(),6);
    assert(lv_obj_has_state(colors_switch,LV_STATE_CHECKED));
    lv_obj_clear_state(colors_switch,LV_STATE_CHECKED); lv_event_send(colors_switch,LV_EVENT_VALUE_CHANGED,NULL); assert(!weather.dynamic_colors);
    unsigned restored_city,restored_theme; bool restored_dynamic;
    assert(weather_config_decode(persisted_config,&restored_city,&restored_theme,&restored_dynamic)&&restored_theme==2&&!restored_dynamic);
    mock_save_failure=true;
    lv_obj_add_state(colors_switch,LV_STATE_CHECKED); lv_event_send(colors_switch,LV_EVENT_VALUE_CHANGED,NULL);
    assert(!weather.dynamic_colors&&!lv_obj_has_state(colors_switch,LV_STATE_CHECKED));
    mock_save_failure=false;
    lv_obj_add_state(colors_switch,LV_STATE_CHECKED); lv_event_send(colors_switch,LV_EVENT_VALUE_CHANGED,NULL); assert(weather.dynamic_colors);
    click(lv_obj_get_child(content(),9)); assert(app_current()==&app_system_info); snapshot("system-info-dark");
    app_request_back(); app_process(); settle(); assert(app_current()==&app_settings);
    app_request(NULL); app_process(); settle(); snapshot("desktop-dark"); check_bounds(body());
    weather.theme=0; weather.revision++; app_request(NULL); app_process(); settle();
    click(desktop_button(&app_weather)); assert(app_current()==&app_weather); snapshot("weather-light"); check_bounds(body());
    assert(lv_obj_get_height(content())==480);
    const int artwork_codes[]={0,0,2,2,3,45,51,61,65,95,73,-1};
    for(int i=0;i<12;i++) {
        weather.data.code=artwork_codes[i]; weather.data.daytime=i!=1&&i!=3; weather.revision++;
        char shot[64]; snprintf(shot,sizeof(shot),"soft3d-art-%02d",i);
        snapshot(shot); check_bounds(body());
    }
    valid_weather(); snapshot("weather-light");
    puts("PASS soft 3D artwork: 12 weather states, day/night, large/small icons and transparent backgrounds");
    click(action(9)); snapshot("weather-hourly-light"); check_bounds(body());
    assert(has_text("17:00")&&has_text("21:00")&&!has_text("16:00"));
    for(int i=0;i<40;i++) click(action(9));
    simulated_now+=3600; snapshot("weather-hourly-next-hour"); check_bounds(body()); assert(has_text("18:00")&&has_text("22:00")&&!has_text("17:00")); simulated_now-=3600;
    time_t previous_now=simulated_now;
    simulated_now+=7*3600; valid_weather(); snapshot("weather-hourly-midnight");
    assert(has_text("00:00")&&has_text("04:00")&&!has_text("23:00"));
    simulated_now=previous_now; valid_weather();
    click(action(9)); snapshot("weather-current-restored");
    puts("PASS middle weather panel: toggles hourly/current, 5 upcoming timestamps, optional/malformed hourly parsing, repeated switches");
    click(action(4)); snapshot("weather-settings");
    click(action(5)); snapshot("weather-cities"); click(action(102)); assert(weather.city==2);
    click(action(6)); click(action(202)); snapshot("weather-appearance"); click(action(1)); click(action(1));
    click(action(4)); click(action(6));
    for(unsigned theme=0;theme<WEATHER_THEMES;theme++) {
        click(action(200+theme)); char shot[64]; snprintf(shot,sizeof(shot),"appearance-%u",theme); snapshot(shot); check_bounds(body());
        assert(weather.theme==theme);
    }
    click(action(10)); assert(!weather.dynamic_colors); click(action(10)); assert(weather.dynamic_colors);
    click(action(1)); click(action(1));
    valid_weather();
    for(unsigned theme=0;theme<WEATHER_THEMES;theme++) {
        weather_choose_theme(theme); snapshot(theme==0?"theme-paper":theme==1?"theme-blue":"theme-gold");
        lv_obj_t *clock=lv_obj_get_child(body(),0); click(clock); assert(strlen(lv_label_get_text(clock))==8);
        assert(lv_obj_get_style_text_font(clock,LV_PART_MAIN)==weather_clock_seconds_font(theme));
        click(clock);
        weather.data.temperature=36; weather.data.apparent=38; weather.data.humidity=85; weather.data.wind=12; weather.revision++;
        snapshot(theme==0?"values-high-paper":theme==1?"values-high-blue":"values-high-gold");
        valid_weather();
    }
    weather_choose_theme(2);
    valid_weather(); snapshot("weather-dark"); check_bounds(body());
    click(action(9)); snapshot("weather-hourly-dark"); check_bounds(body());
    weather.data.hour_count=0; weather.revision++; snapshot("weather-hourly-missing");
    click(action(9)); valid_weather();
    weather.network=false; weather.revision++; snapshot("weather-offline");
    weather.phase=WEATHER_ERROR; weather.network=true; weather.revision++; snapshot("weather-error");
    weather.data.data_time-=7*3600; weather.revision++; snapshot("weather-stale");
    weather.time_valid=false; weather.data.valid=false; weather.revision++; snapshot("weather-first-open");
    click(action(4)); click(action(7)); snapshot("weather-status"); check_bounds(body());
    click(action(1)); click(action(8)); snapshot("weather-about"); check_bounds(body());
    click(action(1)); click(action(1)); click(action(2)); assert(!app_current()); assert(weather.active&&desktop_owner&&!app_owner); assert(lv_obj_get_height(content())==444);
    snapshot("desktop-weather"); valid_weather(); lv_mem_monitor_t before,after; lv_mem_monitor(&before);
    for(int i=0;i<300;i++) { app_request(&app_weather); app_process(); settle(); app_request(NULL); app_process(); settle(); }
    lv_mem_monitor(&after); assert(after.free_size+1024>=before.free_size);
    lv_mem_monitor_t stable;
    for(int i=0;i<300;i++) { app_request(&app_weather); app_process(); settle(); app_request(NULL); app_process(); settle(); }
    lv_mem_monitor(&stable); assert(stable.free_size>=after.free_size); assert(lv_mem_test()==LV_RES_OK); assert(keys==0);
    printf("PASS desktop and weather: five desktop entries, nested info return, themes, backgrounds and status chrome, weather ownership, data states/date/lunar, 600 app switches, no HID, heap %u -> %u -> %u (stable)\n",before.free_size,after.free_size,stable.free_size);
    return 0;
}
'''

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--cc',required=True); parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='weather-ui-') as tmp:
        tmp=Path(tmp)
        headers={k:v for k,v in HEADERS.items() if k not in ('lvgl.h','board.h','shell.h')}; headers.update(EXTRA_HEADERS)
        headers['esp_log.h']+='#define ESP_LOGW(...) ((void)0)\n'
        headers['clock_compat.h']='#include <time.h>\ntime_t sim_time(time_t *out);\nstruct tm *sim_localtime_r(const time_t*,struct tm*);\n'
        for name,text in headers.items():
            p=tmp/name; p.parent.mkdir(parents=True,exist_ok=True)
            guard='WEATHER_TEST_'+name.upper().replace('/','_').replace('.','_')
            p.write_text(f'#ifndef {guard}\n#define {guard}\n{text}\n#endif\n',encoding='utf-8')
        prefix=HOST.split('int main(void)')[0].replace('bool usb_service_online(void) { return true; }','extern bool simulated_usb; bool usb_service_online(void) { return simulated_usb; }')
        host=tmp/'weather_sim.c'; host.write_text(prefix+MOCK.replace('static bool simulated_usb=true;','bool simulated_usb=true;'),encoding='utf-8')
        sources=list((ROOT/'components/lvgl/src').rglob('*.c'))
        sources += [ROOT/'main/app_registry.c',ROOT/'main/platform/app.c',ROOT/'main/services/wifi_policy.c']
        sources += list((ROOT/'main/shell').glob('*.c'))
        sources += [p for p in (ROOT/'main/apps').rglob('*.c') if p.name not in ('weather_service.c','weather_data.c')]
        sources += [ROOT/'templates/minimal/app_minimal.c',ROOT/'main/apps/weather/weather_data.c',Path('C:/play/esp32s3touchscreen/toolchains/idf-5.4.1-cn/components/json/cJSON/cJSON.c')]
        cmd=[args.cc,'-DLV_CONF_SKIP','-DLV_MEM_SIZE=2097152','-DLV_FONT_MONTSERRAT_20=1','-Dlocaltime_r=sim_localtime_r','-include',str(tmp/'clock_compat.h')]
        cmd+=['-I'+str(p) for p in [tmp,ROOT/'components/lvgl',ROOT/'main/platform',ROOT/'main/shell',ROOT/'main/services',ROOT/'main/apps/weather',Path('C:/play/esp32s3touchscreen/toolchains/idf-5.4.1-cn/components/json/cJSON')]]
        objects=[]
        for i,source in enumerate([host,*sources]):
            obj=tmp/f'{i}.o'; subprocess.run([*cmd,'-c',str(source),'-o',str(obj)],check=True,stdout=subprocess.DEVNULL); objects.append(obj)
        exe=tmp/'weather-sim.exe'; subprocess.run([args.cc,*map(str,objects),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],cwd=args.output,check=True)
    from PIL import Image
    for ppm in args.output.glob('*.ppm'): Image.open(ppm).save(ppm.with_suffix('.png')); ppm.unlink()

if __name__=='__main__': main()
