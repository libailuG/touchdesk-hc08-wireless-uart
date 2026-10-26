#include "app.h"
#include "ui.h"
#include "shell.h"
#include "xiaozhi_service.h"
#include "music_service.h"
#include <stdio.h>
#include <string.h>
typedef struct {lv_obj_t *status,*binding,*user,*answer,*hold,*connect;bool down;} view_t;
static void connect_event(lv_event_t *e){if(lv_event_get_code(e)==LV_EVENT_CLICKED)xiaozhi_connect();}
static void cancel_event(lv_event_t *e){if(lv_event_get_code(e)==LV_EVENT_CLICKED)xiaozhi_cancel();}
static void hold_event(lv_event_t *e){
 if(lv_event_get_code(e)==LV_EVENT_CLICKED){xiaozhi_snapshot_t s;xiaozhi_snapshot(&s);if(s.continuous)xiaozhi_cancel();else xiaozhi_continuous_start();}
}
static void refresh(lv_timer_t *t){
 app_ctx_t *ctx=t->user_data;view_t *v=ctx->state;xiaozhi_snapshot_t s;xiaozhi_snapshot(&s);
 lv_label_set_text(v->status,s.status[0]?s.status:"聊天服务不可用，请查看串口日志");
 lv_label_set_text_fmt(v->binding,s.state==XZ_ACTIVATING?"绑定码：%s":"%s",s.state==XZ_ACTIVATING?s.activation:"小智 AI · 连续对话");
 lv_label_set_text_fmt(v->user,"你：%s",s.user[0]?s.user:"点击开始，看到正在听后说话");
 lv_label_set_text_fmt(v->answer,"小智：%s",s.answer[0]?s.answer:s.state==XZ_ACTIVATING?"打开 xiaozhi.me，添加设备并输入上方绑定码。":"开始后自动听和回答。播放时暂停录音，点击停止结束。");
 const char *label=s.continuous?"结束连续聊天":"开始连续聊天";
 lv_label_set_text(lv_obj_get_child(v->hold,0),label);
 if(!s.available||s.state==XZ_ACTIVATING)lv_obj_add_state(v->hold,LV_STATE_DISABLED);else lv_obj_clear_state(v->hold,LV_STATE_DISABLED);
 if(!s.available||s.state==XZ_CONNECTING||s.state==XZ_ACTIVATING||s.state==XZ_LISTENING||s.state==XZ_SPEAKING||s.state==XZ_THINKING)lv_obj_add_state(v->connect,LV_STATE_DISABLED);else lv_obj_clear_state(v->connect,LV_STATE_DISABLED);
}
static esp_err_t create(app_ctx_t *ctx,lv_obj_t *root){
 view_t *v=ctx->state;
 v->status=ui_label(root,"正在准备",12,0,296);lv_obj_set_height(v->status,50);lv_label_set_long_mode(v->status,LV_LABEL_LONG_DOT);
 v->binding=ui_label(root,"小智 AI",12,52,296);lv_obj_set_style_text_align(v->binding,LV_TEXT_ALIGN_CENTER,0);
 v->user=ui_label(root,"",12,90,296);lv_obj_set_height(v->user,64);lv_label_set_long_mode(v->user,LV_LABEL_LONG_DOT);
 v->answer=ui_label(root,"",12,162,296);lv_obj_set_height(v->answer,100);lv_label_set_long_mode(v->answer,LV_LABEL_LONG_DOT);
 v->connect=ui_button(root,"连接 / 绑定",12,272,142,36,connect_event,NULL);
 ui_button(root,"停止",166,272,142,36,cancel_event,NULL);
 v->hold=ui_button(root,"开始连续聊天",12,318,296,42,NULL,NULL);
 lv_obj_clear_flag(v->hold,LV_OBJ_FLAG_PRESS_LOCK);
 lv_obj_add_event_cb(v->hold,hold_event,LV_EVENT_ALL,v);
 lv_timer_t *timer=app_timer_create(ctx,refresh,100);if(!timer)return ESP_ERR_NO_MEM;refresh(timer);return ESP_OK;
}
static void destroy(app_ctx_t *ctx){xiaozhi_leave();}
const app_desc_t app_xiaozhi={.id="xiaozhi",.title="AI 聊天",.subtitle="小智语音对话",.caps=APP_CAP_USB_AUDIO|APP_CAP_NETWORK|APP_CAP_VOICE,.state_size=sizeof(view_t),.create=create,.destroy=destroy};
