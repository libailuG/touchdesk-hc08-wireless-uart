#include "app.h"
extern const app_desc_t app_serial,app_wireless;
extern const app_desc_t app_keysim, app_counter, app_settings, app_system_info;
extern const app_desc_t app_wifi;
extern const app_desc_t app_weather,app_music,app_microphone;
extern const app_desc_t app_xiaozhi,app_keymouse;
const app_desc_t *const app_registry[] = {
    &app_serial, &app_wireless, &app_keymouse, &app_weather, &app_music, &app_microphone, &app_keysim, &app_counter, &app_settings, &app_system_info, &app_wifi, &app_xiaozhi
};
const size_t app_registry_count=sizeof(app_registry)/sizeof(app_registry[0]);
