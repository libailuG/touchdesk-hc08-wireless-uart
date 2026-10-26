#include "app.h"
#include "ui.h"
#include "shell.h"
#include "music_service.h"
#include <stdio.h>
typedef struct { lv_obj_t *device,*status,*db,*bar,*level,*stats,*toggle; } view_t;
static void toggle(lv_event_t *e) {
 if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
 microphone_snapshot_t s; microphone_snapshot(&s);
 bool ok=s.active?microphone_stop():microphone_start();
 if(!ok) shell_notice("操作繁忙，请稍后重试");
}
static void reset(lv_event_t *e) { if(lv_event_get_code(e)==LV_EVENT_CLICKED) microphone_reset_peak(); }
static void refresh(lv_timer_t *timer) {
 app_ctx_t *ctx=timer->user_data; view_t *v=ctx->state; microphone_snapshot_t s; microphone_snapshot(&s);
 lv_label_set_text(v->device,s.connected?s.device:"USB 麦克风 · 未连接");
 lv_label_set_text(v->status,s.status[0]?s.status:"等待麦克风");
 char db[32];snprintf(db,sizeof(db),"%.1f dBFS",(double)s.dbfs);lv_label_set_text(v->db,db);
 lv_bar_set_value(v->bar,s.level,LV_ANIM_OFF);
 lv_obj_set_style_bg_color(v->bar,lv_color_hex(s.level>=90?0xe65b55:s.level>=75?0xe4a13b:0x28b887),LV_PART_INDICATOR);
 lv_label_set_text_fmt(v->level,"实时电平 %u%%    峰值 %u%%",s.level,s.peak_level);
 lv_label_set_text_fmt(v->stats,"%u Hz / %u ch / 16 bit\n时长 %02u:%02u · 数据 %u 帧\n削波样本 %u",s.rate,s.channels,s.seconds/60,s.seconds%60,s.frames,s.clipped);
 lv_label_set_text(lv_obj_get_child(v->toggle,0),s.active?"停止测试":"开始测试");
}
static esp_err_t create(app_ctx_t *ctx,lv_obj_t *root) {
 view_t *v=ctx->state;
 v->device=ui_label(root,"USB 麦克风",12,0,296);
 v->status=ui_label(root,"正在启动测试",12,31,296);lv_label_set_long_mode(v->status,LV_LABEL_LONG_SCROLL_CIRCULAR);
 v->db=ui_label(root,"-96.0 dBFS",12,75,296);lv_obj_set_style_text_font(v->db,&lv_font_montserrat_20,0);lv_obj_set_style_text_align(v->db,LV_TEXT_ALIGN_CENTER,0);
 v->bar=lv_bar_create(root);lv_obj_set_pos(v->bar,16,115);lv_obj_set_size(v->bar,288,26);lv_bar_set_range(v->bar,0,100);lv_obj_set_style_bg_color(v->bar,ui_surface(),0);
 v->level=ui_label(root,"",12,155,296);
 v->stats=ui_label(root,"",12,190,296);
 ui_label(root,"对着耳机麦克风说话，\n观察电平和峰值是否变化。",12,271,296);
 v->toggle=ui_button(root,"停止测试",12,326,142,34,toggle,NULL);
 ui_button(root,"清除峰值",166,326,142,34,reset,NULL);
 lv_timer_t *timer=app_timer_create(ctx,refresh,100);if(!timer) return ESP_ERR_NO_MEM;
 if(!microphone_start()) shell_notice("麦克风启动请求失败");
 refresh(timer);return ESP_OK;
}
static void destroy(app_ctx_t *ctx) { microphone_stop(); }
const app_desc_t app_microphone={.id="microphone",.title="麦克风测试",.subtitle="USB 耳机输入电平",.caps=APP_CAP_USB_AUDIO,.state_size=sizeof(view_t),.create=create,.destroy=destroy};
