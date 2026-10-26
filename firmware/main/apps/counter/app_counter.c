#include "app.h"
#include "ui.h"
#include "shell.h"
#include "settings.h"
#include "esp_timer.h"
typedef struct { app_ctx_t *ctx; int delta; } binding_t;
typedef struct {
    int value; bool dirty; int64_t changed; lv_obj_t *label;
    binding_t decrement,increment;
} state_t;
static void update(state_t *s) { lv_label_set_text_fmt(s->label,"%d",s->value); }
static void save(state_t *s)
{
    if(!s->dirty) return;
    if(settings_counter_save(s->value)==ESP_OK) s->dirty=false;
    else { shell_notice("计数保存失败"); s->changed=esp_timer_get_time(); }
}
static void tick(lv_timer_t *timer)
{
    app_ctx_t *ctx=timer->user_data;
    state_t *s=ctx->state;
    if(s->dirty && esp_timer_get_time()-s->changed>1000000) save(s);
}
static void change(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    binding_t *binding=lv_event_get_user_data(e);
    app_ctx_t *ctx=binding->ctx;
    state_t *s=ctx->state;
    int next=s->value+binding->delta;
    if(next<0 || next>9999) return;
    s->value=next;
    s->dirty=true; s->changed=esp_timer_get_time(); update(s);
}
static void clear(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    app_ctx_t *ctx=lv_event_get_user_data(e);
    state_t *s=ctx->state;
    s->value=0; s->dirty=true; s->changed=esp_timer_get_time(); update(s);
}
static esp_err_t create(app_ctx_t *ctx,lv_obj_t *root)
{
    state_t *s=ctx->state;
    if(settings_counter_load(&s->value)!=ESP_OK) shell_notice("使用默认计数");
    ui_label(root,"计数器 · 示例应用",12,16,296);
    ui_label(root,"演示页面、事件与设置保存",12,50,296);
    s->label=ui_label(root,"0",12,106,296);
    lv_obj_set_style_text_align(s->label,LV_TEXT_ALIGN_CENTER,0);
    lv_obj_set_style_text_font(s->label,&lv_font_montserrat_20,0);
    s->decrement=(binding_t){ctx,-1}; s->increment=(binding_t){ctx,1};
    ui_button(root,"- 1",12,170,142,64,change,&s->decrement);
    ui_button(root,"+ 1",166,170,142,64,change,&s->increment);
    ui_button(root,"归零",12,254,296,48,clear,ctx);
    ui_label(root,"停止操作一秒后保存",12,326,296);
    update(s);
    return app_timer_create(ctx,tick,200) ? ESP_OK : ESP_ERR_NO_MEM;
}
static void destroy(app_ctx_t *ctx) { if(ctx->state) save(ctx->state); }
const app_desc_t app_counter={
    .id="counter",.title="计数器",.subtitle="最小应用示例",.state_size=sizeof(state_t),
    .create=create,.destroy=destroy
};
