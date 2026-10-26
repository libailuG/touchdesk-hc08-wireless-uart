#include "app.h"
#include "board.h"
#include "shell.h"
#include "usb_service.h"
#include "usb_app_mode.h"
#include "app_services.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>
static app_ctx_t current;
static lv_obj_t *host;
static const app_desc_t *requested;
static bool switching, going_back;
const app_desc_t *app_current(void) { return current.desc; }
void app_request(const app_desc_t *desc) { requested=desc; switching=true; }
void app_request_back(void) { going_back=true; }
void app_platform_init(lv_obj_t *content) { host=content; }
lv_timer_t *app_timer_create(app_ctx_t *ctx,lv_timer_cb_t cb,uint32_t period)
{
    for(unsigned i=0;i<APP_MAX_TIMERS;i++) if(!ctx->timers[i]) {
        ctx->timers[i]=lv_timer_create(cb,period,ctx);
        return ctx->timers[i];
    }
    return NULL;
}
bool app_track(app_ctx_t *ctx,app_cleanup_cb cb,void *resource)
{
    if(!cb) return false;
    for(unsigned i=0;i<APP_MAX_CLEANUPS;i++) if(!ctx->cleanup[i].cb) {
        ctx->cleanup[i].cb=cb; ctx->cleanup[i].resource=resource; return true;
    }
    return false;
}
bool app_hid_click(app_ctx_t *ctx,unsigned gesture,uint8_t mods,uint8_t key)
{
    return ctx==&current && ctx->active && (ctx->desc->caps&APP_CAP_HID) &&
        usb_service_click(ctx->hid_owner,gesture,mods,key);
}
static void close_current(void)
{
    current.active=false;
    usb_service_set_owner(false);
    board_input_cancel();
    for(unsigned i=0;i<APP_MAX_TIMERS;i++) if(current.timers[i]) {
        lv_timer_del(current.timers[i]); current.timers[i]=NULL;
    }
    if(current.desc && current.desc->destroy) current.desc->destroy(&current);
    for(int i=APP_MAX_CLEANUPS-1;i>=0;i--)
        if(current.cleanup[i].cb) current.cleanup[i].cb(current.cleanup[i].resource);
    /* Delete callbacks may still refer to ctx->state, so free it afterwards. */
    lv_obj_clean(host);
    heap_caps_free(current.state);
    memset(&current,0,sizeof(current));
}
void app_process(void)
{
    if(usb_app_mode_pending()) return;
    if(going_back) {
        going_back=false;
        if(!switching && (!current.desc || !current.desc->back || !current.desc->back(&current)))
            app_request(NULL);
    }
    if(!switching) return;
    const app_desc_t *next=requested;
    switching=false;
    close_current();
    esp_err_t mode_err=usb_app_mode_prepare(next);
    if(mode_err!=ESP_OK) {
        ESP_LOGE("app","USB transition failed: %s",esp_err_to_name(mode_err));
        app_services_prepare(NULL);
        shell_fullscreen(false);shell_title("桌面");shell_desktop(host);
        shell_notice("服务切换失败，请重试");
        return;
    }
    if(usb_app_mode_pending()) {
        shell_fullscreen(false);
        shell_title(next->title);
        shell_notice("正在切换 USB 模式");
        return;
    }
    shell_fullscreen(next && (next->caps&APP_CAP_FULLSCREEN));
    shell_title(next ? next->title : "桌面");
    if(!next) { shell_desktop(host); return; }
    /* Fail before creating objects if either heap is already under pressure. */
    if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<24*1024 ||
       heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)<128*1024) {
        app_services_prepare(NULL); shell_fullscreen(false); shell_desktop(host); shell_title("桌面"); shell_notice("内存不足，已返回桌面"); return;
    }
    current.desc=next;
    current.root=host;
    if(next->state_size) current.state=heap_caps_calloc(1,next->state_size,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    esp_err_t err=next->state_size && !current.state ? ESP_ERR_NO_MEM : ESP_OK;
    if(err==ESP_OK) {
        current.active=true;
        current.hid_owner=usb_service_set_owner((next->caps&APP_CAP_HID)!=0);
        err=next->create(&current,host);
    }
    if(err!=ESP_OK) {
        ESP_LOGE("app","%s create failed: %s",next->id,esp_err_to_name(err));
        close_current(); app_services_prepare(NULL); shell_fullscreen(false); shell_title("桌面"); shell_desktop(host);
        shell_notice("应用启动失败，已返回桌面");
    } else ESP_LOGI("app","opened %s; internal=%u psram=%u",next->id,
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
        (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
}
