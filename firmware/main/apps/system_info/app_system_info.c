#include "app.h"
#include "ui.h"
#include "board.h"
#include "settings.h"
#include "usb_service.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
typedef struct { lv_obj_t *label; } state_t;
static void tick(lv_timer_t *timer)
{
    app_ctx_t *ctx=timer->user_data;
    state_t *s=ctx->state;
    lv_label_set_text_fmt(s->label,
        "TouchDesk %s\nESP-IDF %s\nLVGL 8.4.0\n"
        "Flash: 16 MB\nPSRAM: %u MB\n触摸: Goodix %s\n"
        "内部空闲: %u KB\nPSRAM 空闲: %u KB\n"
        "%s\nUSB: %s\n设置: %s\n运行时间: %u 秒\n复位原因: %d",
        esp_app_get_description()->version,esp_get_idf_version(),
        (unsigned)(esp_psram_get_size()/1024/1024),board_touch_id(),
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)/1024),
        (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)/1024),
        board_storage_summary(),usb_service_online()?"已连接":"未连接",settings_available()?"可用":"不可用",
        (unsigned)(esp_timer_get_time()/1000000),esp_reset_reason());
}
static esp_err_t create(app_ctx_t *ctx,lv_obj_t *root)
{
    state_t *s=ctx->state;
    s->label=ui_label(root,"",12,10,296);
    lv_timer_t *timer=app_timer_create(ctx,tick,1000);
    if(!timer) return ESP_ERR_NO_MEM;
    tick(timer); return ESP_OK;
}
static bool back(app_ctx_t *ctx)
{
    (void)ctx; extern const app_desc_t app_settings; app_request(&app_settings); return true;
}
const app_desc_t app_system_info={
    .id="system_info",.title="系统信息",.subtitle="版本与硬件状态",
    .state_size=sizeof(state_t),.create=create,.back=back
};
