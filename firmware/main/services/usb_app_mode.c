#include "app_services.h"
#include "usb_app_mode.h"
#include "music_service.h"
#include "xiaozhi_service.h"
#include "shell.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdatomic.h>
#include <string.h>

static atomic_bool key_ui_requested,bridge_ui_requested,home_ui_requested;
static atomic_uint app_ui_requested;
bool usb_app_mode_console(const char *line)
{
    if(!strncmp(line,"app ui ",7)) {
        for(unsigned i=0;i<app_registry_count;i++) {
            if(!strcmp(line+7,app_registry[i]->id)) { atomic_store(&app_ui_requested,i+1);return true; }
        }
        ESP_LOGW("app","Unknown app id");return true;
    }
    if(!strcmp(line,"bridge ui")){atomic_store(&bridge_ui_requested,true);return true;}
    if(!strcmp(line,"system home")){atomic_store(&home_ui_requested,true);return true;}
    if(strcmp(line,"keysim ui")) return false;
    atomic_store(&key_ui_requested,true);
    return true;
}

esp_err_t usb_app_mode_prepare(const app_desc_t *next) { return app_services_prepare(next); }
bool usb_app_mode_pending(void) { return false; }
void usb_app_mode_poll(void) {
 unsigned selected=atomic_exchange(&app_ui_requested,0);
 if(selected&&selected<=app_registry_count)app_request(app_registry[selected-1]);
 if(atomic_exchange(&key_ui_requested,false)) { extern const app_desc_t app_keysim;app_request(&app_keysim); }
 if(atomic_exchange(&bridge_ui_requested,false)) { extern const app_desc_t app_keymouse;app_request(&app_keymouse); }
 if(atomic_exchange(&home_ui_requested,false))app_request(NULL);
}
