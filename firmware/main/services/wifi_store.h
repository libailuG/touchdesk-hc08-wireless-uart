#ifndef TOUCHDESK_WIFI_STORE_H
#define TOUCHDESK_WIFI_STORE_H
#include "wifi_service.h"
typedef struct {
    uint32_t version;
    uint8_t enabled,has_profile;
    uint8_t count,reserved;
    wifi_profile_t profile;
    wifi_profile_t history[WIFI_SAVED_LIMIT-1];
} wifi_saved_t;
const wifi_profile_t *wifi_saved_profile(const wifi_saved_t *data,unsigned index);
int wifi_saved_find(const wifi_saved_t *data,const uint8_t *ssid,unsigned length,unsigned security);
void wifi_saved_remember(wifi_saved_t *data,const wifi_profile_t *profile);
void wifi_saved_remove(wifi_saved_t *data,unsigned index);
int wifi_saved_best(const wifi_saved_t *data,const wifi_network_t *networks,size_t count,unsigned excluded);
esp_err_t wifi_store_load(wifi_saved_t *out);
esp_err_t wifi_store_save(const wifi_saved_t *data);
#endif
