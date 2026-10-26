#include "app.h"
#include "ui.h"
#include "shell.h"
#include "music_service.h"
#include <stdio.h>
extern const app_desc_t app_microphone;
static void mic_open(lv_event_t *e) { if(lv_event_get_code(e)==LV_EVENT_CLICKED) app_request(&app_microphone); }
typedef struct { lv_obj_t *status,*device,*title,*progress,*play,*list,*volume_label,*mode; unsigned count,revision; bool listed; } view_t;
static void action(lv_event_t *e) {
 if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
 int a=(int)(intptr_t)lv_event_get_user_data(e);
 music_snapshot_t s; music_snapshot(&s);
 if(a==MUSIC_PLAY) a=s.playing&&!s.paused?MUSIC_PAUSE:MUSIC_PLAY;
 if(!music_command(a,0)) shell_notice("操作繁忙，请稍后重试");
}
static void select_song(lv_event_t *e) {
 if(lv_event_get_code(e)==LV_EVENT_VALUE_CHANGED) music_command(MUSIC_SELECT,lv_dropdown_get_selected(lv_event_get_target(e)));
}
static void volume(lv_event_t *e) {
 if(lv_event_get_code(e)==LV_EVENT_VALUE_CHANGED) music_command(MUSIC_VOLUME,lv_slider_get_value(lv_event_get_target(e)));
}
static void role_answer(lv_event_t *e) {
 lv_obj_t *box=lv_event_get_current_target(e);
 uint16_t id=lv_msgbox_get_active_btn(box);
 if(id==0) music_command(MUSIC_ROLE,0);
 lv_msgbox_close(box);
}
static void role(lv_event_t *e) {
 if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
 static const char *buttons[]={"切换","取消",""};
 const char *hint=music_host_mode()?"切换后原生USB作为电脑键盘，音乐输出和键鼠桥接停止。":"切换前请从原生USB拔下电脑，接扩展坞或数字USB耳机。COM口继续供电和烧录。";
 lv_obj_t *box=lv_msgbox_create(NULL,"USB 模式",hint,buttons,false); lv_obj_set_width(box,300); lv_obj_center(box);
 lv_obj_add_event_cb(box,role_answer,LV_EVENT_VALUE_CHANGED,NULL);
}
static void refresh(lv_timer_t *timer) {
 app_ctx_t *ctx=timer->user_data; view_t *v=ctx->state; music_snapshot_t s; music_snapshot(&s);
 lv_label_set_text(lv_obj_get_child(v->mode,0),s.host?"改为 USB 键盘模式":"改为 USB 音频模式");
 lv_label_set_text(v->status,s.status[0]?s.status:"正在初始化");
 lv_label_set_text(v->device,s.host?(s.connected?s.device:"USB音频 · 未检测到耳机"):"USB键盘模式 · 音频已停用");
 lv_label_set_text(v->title,s.title[0]?s.title:"选择 TF 卡中的音乐");
 lv_label_set_text_fmt(v->progress,"%02u:%02u    %s%u 首    %u Hz",s.seconds/60,s.seconds%60,s.scanning?"扫描中 · ":"",s.count,s.rate);
 lv_label_set_text(lv_obj_get_child(v->play,0),s.playing&&!s.paused?"暂停":"播放");
 lv_label_set_text_fmt(v->volume_label,"音量 %u%%",s.volume);
 if(!s.scanning&&(!v->listed||s.catalog_revision!=v->revision)) {
  lv_dropdown_clear_options(v->list);
  for(unsigned i=0;i<s.count;i++) { char name[256]; music_track_name(i,name,sizeof(name)); for(char *p=name;*p;p++) if(*p=='\n'||*p=='\r') *p=' '; lv_dropdown_add_option(v->list,name,LV_DROPDOWN_POS_LAST); }
  if(!s.count) { lv_dropdown_add_option(v->list,"无 MP3 / WAV 文件",LV_DROPDOWN_POS_LAST); lv_obj_add_state(v->list,LV_STATE_DISABLED); }
  v->count=s.count; v->revision=s.catalog_revision; v->listed=true;
 }
 if(v->listed&&s.count&&!lv_dropdown_is_open(v->list)) lv_dropdown_set_selected(v->list,s.index);
}
static esp_err_t create(app_ctx_t *ctx,lv_obj_t *root) {
 view_t *v=ctx->state; music_snapshot_t s; music_snapshot(&s);
 v->device=ui_label(root,"USB 音频",12,0,296);
 v->status=ui_label(root,"",12,28,296); lv_label_set_long_mode(v->status,LV_LABEL_LONG_SCROLL_CIRCULAR);
 v->title=ui_label(root,"",12,58,296); lv_label_set_long_mode(v->title,LV_LABEL_LONG_SCROLL_CIRCULAR);
 v->list=lv_dropdown_create(root); lv_obj_set_pos(v->list,12,92); lv_obj_set_size(v->list,296,40); lv_dropdown_set_options(v->list,"正在扫描 TF 卡");
 lv_obj_set_style_bg_color(v->list,ui_surface(),0); lv_obj_set_style_text_color(v->list,ui_foreground(),0);
 lv_obj_t *list=lv_dropdown_get_list(v->list); lv_obj_set_style_bg_color(list,ui_surface(),0); lv_obj_set_style_text_color(list,ui_foreground(),0); lv_obj_set_style_max_height(list,200,0);
 lv_obj_add_event_cb(v->list,select_song,LV_EVENT_VALUE_CHANGED,NULL);
 v->progress=ui_label(root,"",12,142,296);
 ui_button(root,"上一首",12,179,88,44,action,(void*)MUSIC_PREVIOUS);
 v->play=ui_button(root,"播放",112,179,96,44,action,(void*)MUSIC_PLAY);
 ui_button(root,"下一首",220,179,88,44,action,(void*)MUSIC_NEXT);
 v->volume_label=ui_label(root,"音量",12,237,100);
 lv_obj_t *slider=lv_slider_create(root); lv_obj_set_pos(slider,124,246); lv_obj_set_size(slider,172,10); lv_slider_set_range(slider,0,100); lv_slider_set_value(slider,s.volume,LV_ANIM_OFF); lv_obj_add_event_cb(slider,volume,LV_EVENT_VALUE_CHANGED,NULL);
 v->mode=ui_button(root,s.host?"改为 USB 键盘模式":"改为 USB 音频模式",12,285,296,44,role,NULL);
 ui_button(root,"麦克风测试",12,332,296,28,mic_open,NULL);
 lv_timer_t *timer=app_timer_create(ctx,refresh,250); if(timer) refresh(timer);
 return timer?ESP_OK:ESP_ERR_NO_MEM;
}
const app_desc_t app_music={.id="music",.title="音乐",.subtitle="TF · USB耳机",.caps=APP_CAP_USB_AUDIO|APP_CAP_MUSIC,.state_size=sizeof(view_t),.create=create};
