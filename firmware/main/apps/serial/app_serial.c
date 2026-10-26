#include "app_serial.h"
#include "app.h"
#include "ui.h"
LV_FONT_DECLARE(font_serial_status12);
#include "weather_style.h"
#include "serial_transport.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "board.h"
#include "serial_plot.h"
#include "esp_heap_caps.h"
#include <math.h>
#include <stdatomic.h>
#include <string.h>
#include <stdio.h>
typedef struct { app_ctx_t *ctx;unsigned index; } binding_t;
typedef struct {
 serial_frame_t tx;serial_uart_snapshot_t rx;
 lv_obj_t *fields[10],*values[10],*tabs[3],*heading,*hint,*send,*continuous_switch,*continuous_label,*result,*count;
 lv_obj_t *editor,*input,*validation,*matrix,*confirm;lv_font_t keypad_font;
 lv_obj_t *states[4],*state_received[4],*state_dots[4];binding_t state_bindings[4];
 binding_t bindings[10];bool receive,selftest,test_sent;int editing;int64_t entered;
 lv_timer_t *send_timer;
 bool plotting,paused,dirty;unsigned channel,window_ms;
 uint32_t cursor,lost;serial_plot_t *history;serial_plot_view_t view;serial_sample_t batch[64];
 lv_obj_t *plot,*chart,*selector,*timescale,*plot_title,*latest,*range,*xrange,*pause,*plot_status;
 lv_chart_series_t *series;
} state_t;
static atomic_bool test_requested,ui_requested,curve_requested,wireless_requested;
static bool report_pending;
static serial_uart_snapshot_t test_result;
static unsigned test_points;static uint32_t test_sequence,test_lost;static float test_value;
static unsigned test_window,test_visible,test_rendered;static int64_t test_span;
extern const app_desc_t app_serial,app_wireless;
static void refresh(state_t *s);
void serial_app_selftest_request(void){atomic_store(&test_requested,true);atomic_store(&ui_requested,true);}
void wireless_app_selftest_request(void){atomic_store(&wireless_requested,true);atomic_store(&test_requested,true);atomic_store(&ui_requested,true);}
void serial_app_curve_request(void){atomic_store(&curve_requested,true);atomic_store(&ui_requested,true);}
void serial_app_poll(void) {
 if(atomic_exchange(&ui_requested,false))app_request(atomic_exchange(&wireless_requested,false)?&app_wireless:&app_serial);
 if(report_pending&&!serial_transport_active()) {
  uint8_t raw[48];char hex[97];serial_frame_encode(&test_result.last,raw);
  for(unsigned i=0;i<48;i++)snprintf(hex+2*i,3,"%02x",raw[i]);
  ESP_LOGI("serial_test","tx=%lu rx=%lu discarded=%lu bad_tail=%lu uart_errors=%lu last=%s curve_points=%u curve_seq=%lu curve_value=%.6g history_lost=%lu fifo_errors=%lu buffer_errors=%lu line_errors=%lu",(unsigned long)test_result.tx,(unsigned long)test_result.rx,(unsigned long)test_result.discarded,(unsigned long)test_result.bad_tail,(unsigned long)test_result.uart_errors,hex,test_points,(unsigned long)test_sequence,(double)test_value,(unsigned long)test_lost,(unsigned long)test_result.fifo_errors,(unsigned long)test_result.buffer_errors,(unsigned long)test_result.line_errors);
  report_pending=false;
  ESP_LOGI("serial_window","window_ms=%u visible=%u span_us=%lld rendered=%u capacity=%u psram_bytes=%u",test_window,test_visible,(long long)test_span,test_rendered,SERIAL_PLOT_CAPACITY,(unsigned)sizeof(serial_plot_t));
 }
}
static lv_obj_t *label(lv_obj_t *p,const char *t,int x,int y,int w,const lv_font_t *font) {
 lv_obj_t *l=ui_label(p,t,x,y,w);lv_obj_set_style_text_font(l,font,0);lv_label_set_long_mode(l,LV_LABEL_LONG_CLIP);lv_obj_clear_flag(l,LV_OBJ_FLAG_CLICKABLE);return l;
}
static const uint32_t field_colors[10]={0xbad7ff,0xabe5eb,0xbce6c8,0xd7c6f1,0xffd5aa,0xf4bfce,0xaee0d7,0xdce7a6,0xbacdec,0xeddbb5};
static lv_color_t card_color(unsigned index){
 return lv_color_mix(lv_color_hex(field_colors[index]),ui_background(),lv_color_brightness(ui_background())<128?72:220);
}
static lv_color_t card_ink(void){return lv_color_hex(lv_color_brightness(ui_background())<128?0xf4f7fc:0x15253a);}
static const lv_font_t *value_font(const char *text,bool large){
 const lv_font_t *small[]={&lv_font_montserrat_26,&lv_font_montserrat_20,&lv_font_montserrat_18,&lv_font_montserrat_14};
 const lv_font_t *big[]={&lv_font_montserrat_48,&lv_font_montserrat_40,&lv_font_montserrat_32,&lv_font_montserrat_26,&lv_font_montserrat_20,&lv_font_montserrat_18,&lv_font_montserrat_14};
 const lv_font_t **fonts=large?big:small;unsigned count=large?sizeof(big)/sizeof(big[0]):sizeof(small)/sizeof(small[0]);
 for(unsigned i=0;i<count;i++){lv_point_t size;lv_txt_get_size(&size,text,fonts[i],0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);if(size.x<=100)return fonts[i];}
 return &lv_font_montserrat_14;
}
static void close_editor(state_t *s){if(s->editor){lv_obj_del(s->editor);s->editor=NULL;s->input=NULL;}s->editing=-1;}
static bool commit(state_t *s) {
 if(!serial_value_parse(&s->tx,(unsigned)s->editing,lv_textarea_get_text(s->input))){lv_label_set_text(s->validation,s->editing<2?"整数范围 0~4294967295":"请输入有效的浮点数");return false;}
 close_editor(s);refresh(s);return true;
}
static void keypad(lv_event_t *e) {
 if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED)return;
 state_t *s=((app_ctx_t*)lv_event_get_user_data(e))->state;
 uint16_t id=lv_btnmatrix_get_selected_btn(s->matrix);if(id==LV_BTNMATRIX_BTN_NONE)return;
 const char *key=lv_btnmatrix_get_btn_text(s->matrix,id);
 if(!strcmp(key,"确定")){commit(s);return;}
 if(!strcmp(key,"取消")){close_editor(s);return;}
 if(!strcmp(key,"清空")){lv_textarea_set_text(s->input,"");return;}
 if(!strcmp(key,"退格")||!strcmp(key,"<")){lv_textarea_del_char(s->input);return;}
 if(!strcmp(key,"+/-")) {
  const char *text=lv_textarea_get_text(s->input);char next[40];
  if(*text=='-')snprintf(next,sizeof(next),"%s",text+1);else snprintf(next,sizeof(next),"-%s",text);
  lv_textarea_set_text(s->input,next);lv_textarea_set_cursor_pos(s->input,LV_TEXTAREA_CURSOR_LAST);return;
 }
 lv_textarea_add_text(s->input,key);
}
static void editor_confirm(lv_event_t *e) {
 if(lv_event_get_code(e)!=LV_EVENT_CLICKED)return;
 app_ctx_t *ctx=lv_event_get_user_data(e);state_t *s=ctx->state;
 if(ctx->active&&s->editor)commit(s);
}
static const char *keymap[]={"7","8","9","退格","\n","4","5","6","清空","\n","1","2","3","e","\n","+/-","0",".","取消",""};
static void edit(lv_event_t *e) {
 if(lv_event_get_code(e)!=LV_EVENT_CLICKED)return;
 binding_t *b=lv_event_get_user_data(e);app_ctx_t *ctx=b->ctx;state_t *s=ctx->state;
 if(!ctx->active||s->receive||s->plotting||s->editor)return;
 s->editing=(int)b->index;
 s->editor=lv_obj_create(ctx->root);lv_obj_remove_style_all(s->editor);lv_obj_set_size(s->editor,480,320);lv_obj_set_style_bg_color(s->editor,ui_background(),0);lv_obj_set_style_bg_opa(s->editor,LV_OPA_COVER,0);lv_obj_clear_flag(s->editor,LV_OBJ_FLAG_SCROLLABLE);
 char title[32];snprintf(title,sizeof(title),"编辑 data%u",b->index+1);label(s->editor,title,12,0,150,&font_ui);
 label(s->editor,b->index<2?"uint32":"float32",394,7,74,&lv_font_montserrat_14);
 s->input=lv_textarea_create(s->editor);lv_obj_set_pos(s->input,12,28);lv_obj_set_size(s->input,340,42);lv_textarea_set_one_line(s->input,true);lv_textarea_set_max_length(s->input,32);
 lv_obj_set_style_text_font(s->input,&lv_font_montserrat_26,0);
 lv_obj_set_style_pad_hor(s->input,10,0);lv_obj_set_style_pad_ver(s->input,6,0);lv_obj_set_style_border_width(s->input,1,0);lv_obj_set_style_radius(s->input,8,0);lv_obj_set_style_bg_color(s->input,ui_surface(),0);lv_obj_set_style_text_color(s->input,ui_foreground(),0);
 lv_textarea_set_accepted_chars(s->input,b->index<2?"0123456789":"0123456789.+-eE");
 char value[40];serial_value_format(&s->tx,b->index,value,sizeof(value),true);lv_textarea_set_text(s->input,value);lv_textarea_set_cursor_pos(s->input,LV_TEXTAREA_CURSOR_LAST);
 s->validation=label(s->editor,b->index<2?"0~4294967295":"小数 / 负数 / e",166,0,220,&font_ui);
 s->matrix=lv_btnmatrix_create(s->editor);lv_obj_set_pos(s->matrix,12,80);lv_obj_set_size(s->matrix,456,234);lv_btnmatrix_set_map(s->matrix,keymap);
 s->confirm=ui_button(s->editor,"确定",364,28,104,42,editor_confirm,ctx);
 lv_obj_set_style_bg_color(s->confirm,lv_color_hex(weather_palette(ui_theme())->accent),0);lv_obj_set_style_text_color(s->confirm,ui_accent_foreground(),0);
 s->keypad_font=lv_font_montserrat_32;s->keypad_font.fallback=&font_ui;
 lv_obj_set_style_pad_all(s->matrix,0,0);lv_obj_set_style_pad_row(s->matrix,6,0);lv_obj_set_style_pad_column(s->matrix,8,0);
 lv_obj_set_style_radius(s->matrix,9,LV_PART_ITEMS);lv_obj_set_style_border_width(s->matrix,0,LV_PART_ITEMS);lv_obj_set_style_shadow_width(s->matrix,0,LV_PART_ITEMS);
 lv_obj_set_style_text_font(s->matrix,&s->keypad_font,LV_PART_ITEMS);lv_obj_set_style_bg_color(s->matrix,ui_surface(),LV_PART_ITEMS);lv_obj_set_style_text_color(s->matrix,ui_foreground(),LV_PART_ITEMS);lv_obj_set_style_bg_color(s->matrix,ui_background(),0);lv_obj_set_style_border_width(s->matrix,0,0);
 lv_btnmatrix_set_btn_ctrl_all(s->matrix,LV_BTNMATRIX_CTRL_NO_REPEAT);
 if(b->index<2){lv_btnmatrix_set_btn_ctrl(s->matrix,11,LV_BTNMATRIX_CTRL_DISABLED);lv_btnmatrix_set_btn_ctrl(s->matrix,12,LV_BTNMATRIX_CTRL_DISABLED);lv_btnmatrix_set_btn_ctrl(s->matrix,14,LV_BTNMATRIX_CTRL_DISABLED);}
 lv_obj_add_event_cb(s->matrix,keypad,LV_EVENT_VALUE_CHANGED,ctx);
}
static void transmit(state_t *s) {
 lv_label_set_text(s->result,serial_transport_send(&s->tx)?(serial_ble_active()?"已排队":"发送成功"):"发送失败");refresh(s);
}
static void send_once(lv_event_t *e) {
 if(lv_event_get_code(e)!=LV_EVENT_CLICKED)return;
 app_ctx_t *ctx=lv_event_get_user_data(e);state_t *s=ctx->state;
 if(!ctx->active||s->receive||s->plotting||s->editor)return;
 transmit(s);
}
static void continuous_tick(lv_timer_t *timer) {
 app_ctx_t *ctx=timer->user_data;state_t *s=ctx->state;
 if(!ctx->active||s->editor||!lv_obj_has_state(s->continuous_switch,LV_STATE_CHECKED))return;
 /* One attempt per tick; transport backpressure never causes catch-up bursts. */
 if(!serial_transport_send(&s->tx))lv_label_set_text(s->result,"发送失败");
}
static void continuous_changed(lv_event_t *e) {
 if(lv_event_get_code(e)!=LV_EVENT_VALUE_CHANGED)return;
 app_ctx_t *ctx=lv_event_get_user_data(e);state_t *s=ctx->state;
 if(lv_obj_has_state(s->continuous_switch,LV_STATE_CHECKED)&&ctx->active){
  lv_timer_reset(s->send_timer);lv_timer_resume(s->send_timer);
 }else{
  lv_obj_clear_state(s->continuous_switch,LV_STATE_CHECKED);lv_timer_pause(s->send_timer);
 }
}
static void toggle_state(lv_event_t *e) {
 if(lv_event_get_code(e)!=LV_EVENT_CLICKED)return;
 binding_t *b=lv_event_get_user_data(e);state_t *s=b->ctx->state;
 if(!b->ctx->active||s->receive||s->plotting||s->editor)return;
 s->tx.data1^=UINT32_C(1)<<b->index;transmit(s);
}
static void collect(state_t *s) {
 for(unsigned batch=0;batch<8;batch++) {
  size_t n=serial_transport_history(&s->cursor,s->batch,64,&s->lost);if(!n)break;
  for(size_t i=0;i<n;i++)serial_plot_push(s->history,&s->batch[i]);
  s->dirty=true;
 }
}
static void draw_plot(state_t *s) {
 serial_plot_reduce(s->history,s->channel,s->window_ms,&s->view);
 double lo=s->view.finite?s->view.minimum:0,hi=s->view.finite?s->view.maximum:1;bool found=s->view.finite;char text[100];
 double span=hi-lo,pad=span>0?span*.1:fmax(fabs(lo)*.05,1e-6);double lower=lo-pad,upper=hi+pad;
 lv_chart_set_all_value(s->chart,s->series,LV_CHART_POINT_NONE);
 for(unsigned i=0;i<s->view.count;i++){float v=s->view.points[i].value;lv_coord_t y=isfinite(v)?(lv_coord_t)((v-lower)/(upper-lower)*1000):LV_CHART_POINT_NONE;lv_chart_set_value_by_id2(s->chart,s->series,i,s->view.points[i].x,y);}
 snprintf(text,sizeof(text),"data%u 实时曲线",s->channel+3);lv_label_set_text(s->plot_title,text);
 if(s->history->count){float v=serial_plot_at(s->history,s->history->count-1)->values[s->channel];serial_float_format(v,text,sizeof(text));}else snprintf(text,sizeof(text),"--");lv_label_set_text(s->latest,text);lv_obj_set_style_text_font(s->latest,strlen(text)>8?&lv_font_montserrat_14:&lv_font_montserrat_20,0);
 if(found){char minimum[40],maximum[40];serial_float_format(s->view.minimum,minimum,sizeof(minimum));serial_float_format(s->view.maximum,maximum,sizeof(maximum));snprintf(text,sizeof(text),"Y %s ~ %s",minimum,maximum);}else snprintf(text,sizeof(text),"Y --");lv_label_set_text(s->range,text);
 snprintf(text,sizeof(text),"X -%g..0 s  N %u  lost %lu%s",s->window_ms/1000.0,s->view.visible,(unsigned long)s->lost,s->view.truncated?" *":"");lv_label_set_text(s->xrange,text);
 lv_label_set_text(s->plot_status,s->paused?"曲线已暂停":s->history->count?"显示中":"等待接收");lv_chart_refresh(s->chart);s->dirty=false;
}
static void plot_control(lv_event_t *e) {
 state_t *s=((app_ctx_t*)lv_event_get_user_data(e))->state;lv_obj_t *target=lv_event_get_target(e);
 if(target==s->selector&&lv_event_get_code(e)==LV_EVENT_VALUE_CHANGED){s->channel=lv_dropdown_get_selected(s->selector);draw_plot(s);}
 else if(target==s->timescale&&lv_event_get_code(e)==LV_EVENT_VALUE_CHANGED){static const unsigned windows[]={250,1000,2000,5000,10000};s->window_ms=windows[lv_dropdown_get_selected(s->timescale)];draw_plot(s);}
 else if(lv_event_get_code(e)==LV_EVENT_CLICKED) {
  if(target==s->pause){s->paused=!s->paused;lv_label_set_text(lv_obj_get_child(s->pause,0),s->paused?"继续显示":"暂停显示");if(s->paused)lv_label_set_text(s->plot_status,"曲线已暂停");else draw_plot(s);}
  else {serial_transport_snapshot(&s->rx);s->cursor=s->rx.rx;s->history->count=s->history->head=0;s->lost=0;draw_plot(s);}
 }
}
static void create_plot(app_ctx_t *ctx) {
 state_t *s=ctx->state;s->plot=lv_obj_create(ctx->root);lv_obj_remove_style_all(s->plot);lv_obj_set_pos(s->plot,0,44);lv_obj_set_size(s->plot,480,276);lv_obj_clear_flag(s->plot,LV_OBJ_FLAG_SCROLLABLE);lv_obj_set_style_bg_color(s->plot,ui_background(),0);lv_obj_set_style_bg_opa(s->plot,LV_OPA_COVER,0);
 s->plot_title=label(s->plot,"data3 实时曲线",12,5,180,&font_ui);
 s->timescale=lv_dropdown_create(s->plot);lv_obj_set_pos(s->timescale,200,0);lv_obj_set_size(s->timescale,136,34);lv_dropdown_set_options(s->timescale,"0.25 s\n1 s\n2 s\n5 s\n10 s");lv_dropdown_set_selected(s->timescale,3);lv_obj_set_style_text_font(s->timescale,&lv_font_montserrat_14,0);lv_obj_add_event_cb(s->timescale,plot_control,LV_EVENT_VALUE_CHANGED,ctx);
 s->selector=lv_dropdown_create(s->plot);lv_obj_set_pos(s->selector,360,33);lv_obj_set_size(s->selector,108,36);lv_dropdown_set_options(s->selector,"data3\ndata4\ndata5\ndata6\ndata7\ndata8\ndata9\ndata10");lv_obj_set_style_text_font(s->selector,&lv_font_montserrat_14,0);lv_obj_add_event_cb(s->selector,plot_control,LV_EVENT_VALUE_CHANGED,ctx);
 label(s->plot,"显示通道",360,5,108,&font_ui);label(s->plot,"最新值",360,80,108,&font_ui);s->latest=label(s->plot,"--",360,111,108,&lv_font_montserrat_20);
 s->chart=lv_chart_create(s->plot);lv_obj_set_pos(s->chart,46,43);lv_obj_set_size(s->chart,290,170);lv_obj_set_style_pad_all(s->chart,0,0);lv_chart_set_type(s->chart,LV_CHART_TYPE_SCATTER);lv_chart_set_point_count(s->chart,SERIAL_PLOT_POINTS);lv_chart_set_range(s->chart,LV_CHART_AXIS_PRIMARY_Y,0,1000);lv_chart_set_range(s->chart,LV_CHART_AXIS_PRIMARY_X,0,10000);lv_chart_set_div_line_count(s->chart,5,5);lv_obj_set_style_size(s->chart,0,LV_PART_INDICATOR);s->series=lv_chart_add_series(s->chart,lv_color_hex(weather_palette(ui_theme())->accent),LV_CHART_AXIS_PRIMARY_Y);
 /* This bundled LVGL version does not initialize scatter X ownership. */
 s->series->x_ext_buf_assigned=false;
 s->range=label(s->plot,"Y --",12,220,324,&lv_font_montserrat_14);s->xrange=label(s->plot,"",12,244,330,&lv_font_montserrat_14);
 s->pause=ui_button(s->plot,"暂停显示",360,151,108,36,plot_control,ctx);ui_button(s->plot,"清空曲线",360,200,108,36,plot_control,ctx);s->plot_status=label(s->plot,"等待接收",360,244,108,&font_ui);
 draw_plot(s);lv_obj_add_flag(s->plot,LV_OBJ_FLAG_HIDDEN);
}
static void refresh(state_t *s) {
 serial_transport_snapshot(&s->rx);
 serial_frame_t empty;serial_frame_init(&empty);
 const serial_frame_t *frame=s->receive?(s->rx.has_frame?&s->rx.last:&empty):&s->tx;
 /* Main state is requested; dot always follows the latest received bit. */
 for(unsigned i=0;i<4;i++) {
  bool known=s->rx.has_frame;
  bool requested=(s->tx.data1>>i)&1u,received=known&&((s->rx.last.data1>>i)&1u);
  char text[64];lv_color_t bg=ui_surface(),ink=ui_foreground();
  if(s->receive){
   snprintf(text,sizeof(text),"%u\n%s",i+1,known?(received?"开":"关"):"?");
   if(received){bg=lv_color_hex(0x20844b);ink=lv_color_hex(0xffffff);}
   lv_obj_add_flag(s->state_dots[i],LV_OBJ_FLAG_HIDDEN);
   lv_obj_set_style_border_width(s->states[i],0,0);
   lv_obj_clear_flag(s->states[i],LV_OBJ_FLAG_CLICKABLE);
  }else{
   snprintf(text,sizeof(text),"%u\n%s",i+1,requested?"开":"关");
   if(requested){bg=lv_color_hex(0x20844b);ink=lv_color_hex(0xffffff);}
   lv_obj_clear_flag(s->state_dots[i],LV_OBJ_FLAG_HIDDEN);
   /* Always opaque: an unknown RX must never reveal the requested-state background. */
   lv_obj_set_style_bg_color(s->state_dots[i],known?lv_color_hex(received?0x43d47a:0x6b7280):ui_surface(),0);
   lv_obj_set_style_bg_opa(s->state_dots[i],LV_OPA_COVER,0);
   lv_obj_set_style_border_color(s->state_dots[i],lv_color_hex(0xffffff),0);
   lv_obj_set_style_border_width(s->states[i],2,0);
   lv_obj_add_flag(s->states[i],LV_OBJ_FLAG_CLICKABLE);
  }
  lv_label_set_text(s->state_received[i],text);lv_obj_center(s->state_received[i]);
  lv_obj_set_style_bg_color(s->states[i],bg,0);lv_obj_set_style_text_color(s->states[i],ink,0);
  lv_obj_set_style_bg_color(s->states[i],bg,LV_STATE_PRESSED);lv_obj_set_style_text_color(s->states[i],ink,LV_STATE_PRESSED);
 }
 for(unsigned i=0;i<10;i++) {
  char text[40];serial_value_format(frame,i,text,sizeof(text),false);
  const char *shown=s->receive&&!s->rx.has_frame?"--":text;
  lv_obj_set_style_text_font(s->values[i],value_font(shown,i==2||i==3),0);
  lv_label_set_text(s->values[i],shown);lv_obj_center(s->values[i]);
  if(s->receive)lv_obj_clear_flag(s->fields[i],LV_OBJ_FLAG_CLICKABLE);else lv_obj_add_flag(s->fields[i],LV_OBJ_FLAG_CLICKABLE);
 }
 char count[48];snprintf(count,sizeof(count),"TX %lu   RX %lu",(unsigned long)s->rx.tx,(unsigned long)s->rx.rx);lv_label_set_text(s->count,count);
 if(serial_ble_active())lv_label_set_text(s->result,serial_ble_status());
 else if(s->receive)lv_label_set_text(s->result,s->rx.has_frame?"已更新接收数据":"等待接收");
}
static void tab(lv_event_t *e) {
 if(lv_event_get_code(e)!=LV_EVENT_CLICKED)return;
 app_ctx_t *ctx=lv_event_get_user_data(e);state_t *s=ctx->state;
 s->plotting=lv_event_get_target(e)==s->tabs[2];s->receive=lv_event_get_target(e)==s->tabs[1];
 lv_label_set_text(s->heading,s->receive?"接收数据":"发送数据");lv_label_set_text(s->hint,"状态1~4");
 if(s->plotting){lv_obj_clear_flag(s->plot,LV_OBJ_FLAG_HIDDEN);collect(s);if(!s->paused)draw_plot(s);}else lv_obj_add_flag(s->plot,LV_OBJ_FLAG_HIDDEN);
 if(s->receive||s->plotting){
  lv_obj_add_flag(s->send,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(s->continuous_switch,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(s->continuous_label,LV_OBJ_FLAG_HIDDEN);
 }else{
  lv_obj_clear_flag(s->send,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(s->continuous_switch,LV_OBJ_FLAG_HIDDEN);lv_obj_clear_flag(s->continuous_label,LV_OBJ_FLAG_HIDDEN);
 }
 for(unsigned i=0;i<3;i++)lv_obj_set_style_bg_color(s->tabs[i],i==(s->plotting?2u:(unsigned)s->receive)?lv_color_hex(weather_palette(ui_theme())->line):ui_background(),0);
 lv_label_set_text(s->result,s->receive?"等待接收":"待发送");refresh(s);
}
static void home(lv_event_t *e){if(lv_event_get_code(e)==LV_EVENT_CLICKED)app_request(NULL);}
static void tick(lv_timer_t *t) {
 app_ctx_t *ctx=t->user_data;state_t *s=ctx->state;refresh(s);collect(s);if(s->plotting&&!s->paused&&s->dirty)draw_plot(s);
 if(s->selftest) {
  int64_t elapsed=esp_timer_get_time()-s->entered;
  if(elapsed>2000000&&!s->test_sent&&(!serial_ble_active()||!strcmp(serial_ble_status(),"BLE ready"))){s->test_sent=true;lv_event_send(s->states[0],LV_EVENT_CLICKED,NULL);lv_event_send(s->states[1],LV_EVENT_CLICKED,NULL);lv_event_send(s->tabs[2],LV_EVENT_CLICKED,NULL);}
  if(elapsed>(serial_ble_active()?30000000:12000000))app_request(NULL);
 }
}
static esp_err_t create(app_ctx_t *ctx,lv_obj_t *root) {
 state_t *s=ctx->state;s->window_ms=5000;s->history=heap_caps_calloc(1,sizeof(*s->history),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!s->history)return ESP_ERR_NO_MEM;
 esp_err_t rotation=board_display_landscape(true);if(rotation!=ESP_OK)return rotation;lv_obj_set_size(root,480,320);
 serial_frame_init(&s->tx);s->editing=-1;s->entered=esp_timer_get_time();s->selftest=atomic_exchange(&test_requested,false);
 if(s->selftest) {
  const char *values[]={"305419896","4294967295","-1.25","0","1.5","42","0.0001","1000","-2.5","3.25"};
  for(unsigned i=0;i<10;i++)serial_value_parse(&s->tx,i,values[i]);
 }
 lv_obj_set_style_bg_color(root,ui_background(),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
 ui_button(root,"<",4,0,40,40,home,ctx);label(root,(ctx->desc->caps&APP_CAP_WIRELESS)?"无线串口":"串口助手",49,7,126,&font_ui);
 s->tabs[0]=ui_button(root,"发送",180,4,92,34,tab,ctx);s->tabs[1]=ui_button(root,"接收",278,4,92,34,tab,ctx);s->tabs[2]=ui_button(root,"曲线",376,4,92,34,tab,ctx);
 s->heading=label(root,"发送数据",12,0,140,&font_ui);s->hint=label(root,"状态1~4",166,0,146,&font_ui);lv_obj_add_flag(s->heading,LV_OBJ_FLAG_HIDDEN);lv_obj_add_flag(s->hint,LV_OBJ_FLAG_HIDDEN);
 for(unsigned i=0;i<4;i++) {
  s->state_bindings[i]=(binding_t){ctx,i};
  s->states[i]=ui_button(root,"",12+116*i,46,108,86,toggle_state,&s->state_bindings[i]);
  lv_obj_clean(s->states[i]);lv_obj_set_style_pad_all(s->states[i],0,0);
  lv_obj_set_style_clip_corner(s->states[i],true,0);
  lv_obj_set_style_radius(s->states[i],10,0);lv_obj_set_style_shadow_width(s->states[i],0,0);
  lv_obj_set_style_border_width(s->states[i],2,0);
  lv_obj_set_style_border_color(s->states[i],lv_color_hex(field_colors[i]),0);
  lv_obj_set_style_border_post(s->states[i],true,0);
  s->state_received[i]=label(s->states[i],"?",0,0,104,&font_ui);
  lv_obj_set_style_text_align(s->state_received[i],LV_TEXT_ALIGN_CENTER,0);
  s->state_dots[i]=lv_obj_create(s->states[i]);lv_obj_remove_style_all(s->state_dots[i]);
  lv_obj_set_pos(s->state_dots[i],68,4);lv_obj_set_size(s->state_dots[i],28,28);
  lv_obj_set_style_radius(s->state_dots[i],LV_RADIUS_CIRCLE,0);
  lv_obj_set_style_border_width(s->state_dots[i],3,0);
  lv_obj_clear_flag(s->state_dots[i],LV_OBJ_FLAG_CLICKABLE|LV_OBJ_FLAG_SCROLLABLE);

 }
 for(unsigned i=0;i<10;i++) {
  /* Shared send/receive grid: data3/4 are switch-sized, other fields keep their identities. */
  static const struct {int x,y,w,h;} cards[10]={
   {244,140,108,38},{360,140,108,38},{12,140,108,86},{128,140,108,86},
   {244,184,108,38},{360,184,108,38},{12,232,108,34},{128,232,108,34},
   {244,232,108,34},{360,232,108,34}
  };
  s->fields[i]=ui_button(root,"",cards[i].x,cards[i].y,cards[i].w,cards[i].h,edit,&s->bindings[i]);lv_obj_clean(s->fields[i]);
  s->bindings[i]=(binding_t){ctx,i};lv_obj_set_style_pad_all(s->fields[i],0,0);
  lv_obj_set_style_border_width(s->fields[i],1,0);lv_obj_set_style_border_color(s->fields[i],lv_color_hex(field_colors[i]),0);
  lv_obj_set_style_radius(s->fields[i],10,0);lv_obj_set_style_shadow_width(s->fields[i],0,0);
  lv_obj_set_style_bg_color(s->fields[i],card_color(i),0);lv_obj_set_style_text_color(s->fields[i],card_ink(),0);
  lv_obj_set_style_bg_color(s->fields[i],lv_color_mix(card_color(i),card_ink(),210),LV_STATE_PRESSED);
  lv_obj_set_style_text_color(s->fields[i],card_ink(),LV_STATE_PRESSED);
  s->values[i]=label(s->fields[i],"0",4,0,100,&lv_font_montserrat_26);
  lv_obj_set_style_text_align(s->values[i],LV_TEXT_ALIGN_CENTER,0);lv_obj_center(s->values[i]);
 }
 s->send=ui_button(root,"发送一次",12,274,224,30,send_once,ctx);
 s->continuous_label=label(root,"持续发送20ms",250,277,142,&font_ui);
 s->continuous_switch=lv_switch_create(root);lv_obj_set_pos(s->continuous_switch,400,274);lv_obj_set_size(s->continuous_switch,68,30);
 lv_obj_set_style_bg_color(s->continuous_switch,lv_color_hex(0x20844b),LV_PART_INDICATOR|LV_STATE_CHECKED);
 lv_obj_add_event_cb(s->continuous_switch,continuous_changed,LV_EVENT_VALUE_CHANGED,ctx);
 s->send_timer=app_timer_create(ctx,continuous_tick,20);if(!s->send_timer)return ESP_ERR_NO_MEM;lv_timer_pause(s->send_timer);
 lv_obj_set_style_bg_color(s->send,lv_color_hex(weather_palette(ui_theme())->accent),0);lv_obj_set_style_text_color(s->send,ui_accent_foreground(),0);
 s->result=label(root,"待发送",12,304,224,&font_serial_status12);s->count=label(root,"TX 0   RX 0",260,304,208,&lv_font_montserrat_14);
 create_plot(ctx);refresh(s);if(atomic_exchange(&curve_requested,false))lv_event_send(s->tabs[2],LV_EVENT_CLICKED,NULL);return app_timer_create(ctx,tick,100)?ESP_OK:ESP_ERR_NO_MEM;
}
static void destroy(app_ctx_t *ctx) {
 state_t *s=ctx->state;if(!s)return;
 if(s->selftest&&s->history){collect(s);serial_plot_reduce(s->history,s->channel,s->window_ms,&s->view);serial_transport_snapshot(&test_result);test_points=s->history->count;test_sequence=s->cursor;test_lost=s->lost;test_value=s->history->count?serial_plot_at(s->history,s->history->count-1)->values[s->channel]:0;test_window=s->window_ms;test_visible=s->view.visible;test_span=s->view.span_us;test_rendered=s->view.count;report_pending=true;}
 heap_caps_free(s->history);s->history=NULL;
 /* remove_series frees both X/Y; this LVGL version's chart destructor frees only Y. */
 if(s->chart&&s->series){lv_chart_remove_series(s->chart,s->series);s->series=NULL;}
 close_editor(s);board_display_landscape(false);lv_obj_set_size(ctx->root,320,480);
}
static bool back(app_ctx_t *ctx){state_t *s=ctx->state;if(s->editor){close_editor(s);return true;}return false;}
const app_desc_t app_serial={.id="serial",.title="串口助手",.subtitle="结构体收发",.caps=APP_CAP_FULLSCREEN|APP_CAP_SERIAL,.state_size=sizeof(state_t),.create=create,.destroy=destroy,.back=back};

const app_desc_t app_wireless={.id="wireless",.title="无线串口",.subtitle="HC-08 BLE",.caps=APP_CAP_FULLSCREEN|APP_CAP_SERIAL|APP_CAP_WIRELESS,.state_size=sizeof(state_t),.create=create,.destroy=destroy,.back=back};
