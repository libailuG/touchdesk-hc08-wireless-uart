/* Copy this file to main/apps/minimal/app_minimal.c, add it to CMakeLists.txt,
 * and register &app_minimal in main/app_registry.c. No hardware initialization here. */
#include "app.h"
#include "ui.h"
typedef struct { unsigned clicks; lv_obj_t *label; } state_t;
static void clicked(lv_event_t *e)
{
    if(lv_event_get_code(e)!=LV_EVENT_CLICKED) return;
    app_ctx_t *ctx=lv_event_get_user_data(e);
    state_t *s=ctx->state;
    lv_label_set_text_fmt(s->label,"点击次数: %u",++s->clicks);
}
static esp_err_t create(app_ctx_t *ctx,lv_obj_t *root)
{
    state_t *s=ctx->state;
    s->label=ui_label(root,"点击次数: 0",12,24,296);
    ui_button(root,"点击",12,100,296,60,clicked,ctx);
    return ESP_OK;
}
const app_desc_t app_minimal={
    .id="minimal",.title="示例应用",.subtitle="从这里开始",
    .state_size=sizeof(state_t),.create=create
};
