#include "settings.h"
#include "nvs_flash.h"
#include "nvs.h"
static nvs_handle_t system_nvs;
static bool ready, autosleep = true;
static uint32_t sleep_seconds = 120;
static bool valid_sleep_seconds(uint32_t value)
{
    return value==0 || value==30 || value==60 || value==120 || value==300 || value==600;
}
esp_err_t settings_init(void)
{
    /* Preserve existing data on version/full errors; use defaults, never erase silently. */
    esp_err_t err = nvs_flash_init();
    if(err != ESP_OK) return err;
    err = nvs_open("system",NVS_READWRITE,&system_nvs);
    if(err != ESP_OK) return err;
    uint32_t version = 0;
    err = nvs_get_u32(system_nvs,"schema",&version);
    if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto failed;
    if(version > 1) { err=ESP_ERR_NOT_SUPPORTED; goto failed; }
    if(version == 0) {
        err = nvs_set_u32(system_nvs,"schema",1);
        if(err == ESP_OK) err = nvs_commit(system_nvs);
        if(err != ESP_OK) goto failed;
    }
    uint8_t value = 1;
    err = nvs_get_u8(system_nvs,"autosleep",&value);
    if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto failed;
    autosleep = value <= 1 ? value : true;
    uint32_t seconds = autosleep ? 120 : 0;
    err = nvs_get_u32(system_nvs,"sleep_seconds",&seconds);
    if(err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) goto failed;
    sleep_seconds = valid_sleep_seconds(seconds) ? seconds : 120;
    autosleep = sleep_seconds != 0;
    ready = true;
    return ESP_OK;
failed:
    nvs_close(system_nvs);
    return err;
}
bool settings_available(void) { return ready; }
bool settings_autosleep(void) { return autosleep; }
uint32_t settings_sleep_seconds(void) { return sleep_seconds; }
esp_err_t settings_set_sleep_seconds(uint32_t seconds)
{
    if(!ready) return ESP_ERR_INVALID_STATE;
    if(!valid_sleep_seconds(seconds)) return ESP_ERR_INVALID_ARG;
    esp_err_t err = nvs_set_u32(system_nvs,"sleep_seconds",seconds);
    if(err == ESP_OK) err = nvs_commit(system_nvs);
    if(err == ESP_OK) {
        sleep_seconds = seconds;
        autosleep = seconds != 0;
    }
    return err;
}
esp_err_t settings_set_autosleep(bool enabled)
{
    return settings_set_sleep_seconds(enabled ? (sleep_seconds ? sleep_seconds : 120) : 0);
}
esp_err_t settings_counter_load(int *value)
{
    *value = 0;
    if(!ready) return ESP_ERR_INVALID_STATE;
    nvs_handle_t h;
    esp_err_t err = nvs_open("app_counter",NVS_READWRITE,&h);
    if(err != ESP_OK) return err;
    int32_t v = 0;
    err = nvs_get_i32(h,"value_v1",&v);
    if(err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if(err == ESP_OK && v >= 0 && v <= 9999) *value = v;
    nvs_close(h);
    return err;
}
esp_err_t settings_counter_save(int value)
{
    if(!ready || value < 0 || value > 9999) return ESP_ERR_INVALID_STATE;
    nvs_handle_t h;
    esp_err_t err = nvs_open("app_counter",NVS_READWRITE,&h);
    if(err != ESP_OK) return err;
    err = nvs_set_i32(h,"value_v1",value);
    if(err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}
