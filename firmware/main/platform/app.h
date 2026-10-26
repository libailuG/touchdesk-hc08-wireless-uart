#pragma once
#ifndef TOUCHDESK_APP_H
#define TOUCHDESK_APP_H
#include "lvgl.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define APP_CAP_HID 1u
#define APP_CAP_FULLSCREEN 2u
#define APP_CAP_USB_AUDIO 4u
#define APP_CAP_USB_HOST_HID 8u
#define APP_CAP_NETWORK 16u
#define APP_CAP_VOICE 32u
#define APP_CAP_WEATHER 64u
#define APP_CAP_MUSIC 128u
#define APP_CAP_SERIAL 256u
#define APP_CAP_WIRELESS 512u
#define APP_MAX_TIMERS 8
#define APP_MAX_CLEANUPS 8
typedef struct app_ctx app_ctx_t;
typedef struct {
    const char *id;
    const char *title;
    const char *subtitle;
    unsigned caps;
    size_t state_size;
    esp_err_t (*create)(app_ctx_t *ctx, lv_obj_t *root);
    void (*destroy)(app_ctx_t *ctx);
    bool (*back)(app_ctx_t *ctx);
} app_desc_t;
typedef void (*app_cleanup_cb)(void *resource);
struct app_ctx {
    const app_desc_t *desc;
    void *state;
    lv_obj_t *root;
    unsigned hid_owner;
    bool active;
    lv_timer_t *timers[APP_MAX_TIMERS];
    struct { app_cleanup_cb cb; void *resource; } cleanup[APP_MAX_CLEANUPS];
};
extern const app_desc_t *const app_registry[];
extern const size_t app_registry_count;
void app_platform_init(lv_obj_t *content);
/* Deferred until LVGL event dispatch completes. NULL requests the desktop. */
void app_request(const app_desc_t *desc);
void app_request_back(void);
void app_process(void);
const app_desc_t *app_current(void);
lv_timer_t *app_timer_create(app_ctx_t *ctx,lv_timer_cb_t cb,uint32_t period);
bool app_track(app_ctx_t *ctx,app_cleanup_cb cb,void *resource);
bool app_hid_click(app_ctx_t *ctx,unsigned gesture,uint8_t mods,uint8_t key);
#endif
