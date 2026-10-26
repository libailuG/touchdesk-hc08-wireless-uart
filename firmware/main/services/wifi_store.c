#include "wifi_store.h"
#include "nvs.h"
#include <string.h>
static bool writable;
typedef struct { uint32_t version; uint8_t enabled,has_profile,reserved[2]; wifi_profile_t profile; } legacy_saved_t;
static wifi_profile_t *entry(wifi_saved_t *data,unsigned i) { return i?&data->history[i-1]:&data->profile; }
const wifi_profile_t *wifi_saved_profile(const wifi_saved_t *data,unsigned i)
{
    return i<data->count?(i?&data->history[i-1]:&data->profile):NULL;
}
int wifi_saved_find(const wifi_saved_t *data,const uint8_t *ssid,unsigned length,unsigned security)
{
    if(!ssid||!length||length>32) return -1;
    for(unsigned i=0;i<data->count;i++) {
        const wifi_profile_t *p=wifi_saved_profile(data,i);
        if(p->ssid_len==length&&wifi_security_compatible(p->security,security)&&!memcmp(p->ssid,ssid,length)) return (int)i;
    }
    return -1;
}
void wifi_saved_remove(wifi_saved_t *data,unsigned index)
{
    if(index>=data->count) return;
    for(unsigned i=index;i+1<data->count;i++) *entry(data,i)=*entry(data,i+1);
    wifi_secret_clear(entry(data,--data->count),sizeof(wifi_profile_t));
    data->has_profile=data->count!=0;
}
void wifi_saved_remember(wifi_saved_t *data,const wifi_profile_t *profile)
{
    if(!wifi_profile_valid(profile)) return;
    wifi_profile_t copy=*profile;
    for(unsigned i=data->count;i>0;i--) {
        const wifi_profile_t *old=wifi_saved_profile(data,i-1);
        if(old->ssid_len==copy.ssid_len&&wifi_security_compatible(old->security,copy.security)&&!memcmp(old->ssid,copy.ssid,copy.ssid_len)) wifi_saved_remove(data,i-1);
    }
    if(data->count==WIFI_SAVED_LIMIT) wifi_saved_remove(data,WIFI_SAVED_LIMIT-1);
    for(unsigned i=data->count;i>0;i--) *entry(data,i)=*entry(data,i-1);
    data->profile=copy; data->count++; data->has_profile=true; data->version=2;
    wifi_secret_clear(&copy,sizeof(copy));
}
int wifi_saved_best(const wifi_saved_t *data,const wifi_network_t *networks,size_t count,unsigned excluded)
{
    int best=-1,signal=-128;
    for(size_t i=0;i<count;i++) {
        int saved=wifi_saved_find(data,networks[i].ssid,networks[i].ssid_len,networks[i].security);
        if(saved>=0&&!(excluded&(1u<<saved))&&networks[i].rssi>signal) { best=saved; signal=networks[i].rssi; }
    }
    return best;
}
static bool valid(const wifi_saved_t *data)
{
    if(data->version!=2||data->enabled>1||data->count>WIFI_SAVED_LIMIT||data->has_profile!=(data->count!=0)) return false;
    for(unsigned i=0;i<data->count;i++) {
        const wifi_profile_t *p=wifi_saved_profile(data,i);
        if(!wifi_profile_valid(p)) return false;
        for(unsigned j=0;j<i;j++) {
            const wifi_profile_t *other=wifi_saved_profile(data,j);
            if(other->ssid_len==p->ssid_len&&wifi_security_compatible(other->security,p->security)&&!memcmp(other->ssid,p->ssid,p->ssid_len)) return false;
        }
    }
    return true;
}
esp_err_t wifi_store_load(wifi_saved_t *out)
{
    memset(out,0,sizeof(*out)); out->version=2; writable=false;
    nvs_handle_t h; esp_err_t err=nvs_open("network",NVS_READWRITE,&h);
    if(err!=ESP_OK) return err;
    wifi_saved_t loaded={0}; size_t size=sizeof(loaded);
    err=nvs_get_blob(h,"config_v2",&loaded,&size);
    if(err==ESP_ERR_NVS_NOT_FOUND) {
        legacy_saved_t old={0}; size=sizeof(old); err=nvs_get_blob(h,"config_v1",&old,&size);
        if(err==ESP_OK) {
            if(size!=sizeof(old)||old.version!=1) err=ESP_ERR_NOT_SUPPORTED;
            else if(old.enabled>1||old.has_profile>1||(old.has_profile&&!wifi_profile_valid(&old.profile))) err=ESP_ERR_INVALID_ARG;
            else { loaded.version=2; loaded.enabled=old.enabled; if(old.has_profile) wifi_saved_remember(&loaded,&old.profile); size=sizeof(loaded); }
        }
        wifi_secret_clear(&old,sizeof(old));
    }
    nvs_close(h);
    if(err==ESP_ERR_NVS_NOT_FOUND) { writable=true; return ESP_OK; }
    if(err!=ESP_OK) { wifi_secret_clear(&loaded,sizeof(loaded)); writable=err==ESP_ERR_INVALID_ARG; return err; }
    if(size!=sizeof(loaded)||loaded.version!=2) { wifi_secret_clear(&loaded,sizeof(loaded)); return ESP_ERR_NOT_SUPPORTED; }
    if(!valid(&loaded)) { wifi_secret_clear(&loaded,sizeof(loaded)); writable=true; return ESP_ERR_INVALID_ARG; }
    for(unsigned i=loaded.count;i<WIFI_SAVED_LIMIT;i++) wifi_secret_clear(entry(&loaded,i),sizeof(wifi_profile_t));
    *out=loaded; wifi_secret_clear(&loaded,sizeof(loaded)); writable=true; return ESP_OK;
}
esp_err_t wifi_store_save(const wifi_saved_t *data)
{
    if(!writable) return ESP_ERR_INVALID_STATE;
    if(!valid(data)) return ESP_ERR_INVALID_ARG;
    nvs_handle_t h; esp_err_t err=nvs_open("network",NVS_READWRITE,&h);
    if(err!=ESP_OK) return err;
    err=nvs_set_blob(h,"config_v2",data,sizeof(*data));
    if(err==ESP_OK) { esp_err_t erased=nvs_erase_key(h,"config_v1"); if(erased!=ESP_OK&&erased!=ESP_ERR_NVS_NOT_FOUND) err=erased; }
    if(err==ESP_OK) err=nvs_commit(h);
    nvs_close(h); return err;
}
