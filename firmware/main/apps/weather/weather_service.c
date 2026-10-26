#include "weather.h"
#include "service_task.h"
#include <stdatomic.h>
#include "wifi_service.h"
#include "settings.h"
#include "nvs.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define RESPONSE_LIMIT 16384
static portMUX_TYPE lock=portMUX_INITIALIZER_UNLOCKED;
static weather_snapshot_t shared={.theme=WEATHER_DEFAULT_THEME,.dynamic_colors=true};
static TaskHandle_t weather_task;
static atomic_bool stop_requested;
static bool started,ntp_started,wanted=true;
static bool app_active,desktop_owner;
static unsigned generation;
static int64_t next_due,last_attempt,blocked_until;
static const char *TAG="weather";
time_t weather_now(void) { return time(NULL); }
static int64_t monotonic(void) { return esp_timer_get_time()/1000000; }
static void synced(struct timeval *tv)
{
    portENTER_CRITICAL(&lock);
    shared.last_sync=tv->tv_sec; shared.time_valid=true; shared.revision++;
    portEXIT_CRITICAL(&lock);
    ESP_LOGI(TAG,"SNTP time synchronized");
}
void weather_snapshot(weather_snapshot_t *out)
{
    portENTER_CRITICAL(&lock); *out=shared; portEXIT_CRITICAL(&lock);
}
static esp_err_t save_config(unsigned city,unsigned theme,bool dynamic)
{
    if(!settings_available()) return ESP_ERR_INVALID_STATE;
    nvs_handle_t h;
    esp_err_t e=nvs_open("app_weather",NVS_READWRITE,&h);
    if(e!=ESP_OK) return e;
    /* One versioned value commits city, theme and number-color preference atomically. */
    e=nvs_set_u32(h,"config_v2",weather_config_encode(city,theme,dynamic));
    if(e==ESP_OK) e=nvs_commit(h);
    nvs_close(h); return e;
}
esp_err_t weather_choose_city(unsigned city)
{
    if(city>=WEATHER_CITIES) return ESP_ERR_INVALID_ARG;
    weather_snapshot_t s; weather_snapshot(&s);
    esp_err_t e=save_config(city,s.theme,s.dynamic_colors);
    portENTER_CRITICAL(&lock);
    shared.save_warning=e!=ESP_OK;
    if(e==ESP_OK&&city!=shared.city) {
        shared.city=city; shared.data=(weather_data_t){0};
        shared.http_status=0; shared.error=ESP_OK;
        shared.phase=WEATHER_IDLE; generation++; wanted=true; next_due=0;
    }
    shared.revision++; portEXIT_CRITICAL(&lock); return e;
}
esp_err_t weather_choose_theme(unsigned theme)
{
    if(theme>=WEATHER_THEMES) return ESP_ERR_INVALID_ARG;
    weather_snapshot_t s; weather_snapshot(&s);
    esp_err_t e=save_config(s.city,theme,s.dynamic_colors);
    portENTER_CRITICAL(&lock); shared.save_warning=e!=ESP_OK;
    if(e==ESP_OK) shared.theme=theme;
    shared.revision++; portEXIT_CRITICAL(&lock); return e;
}
esp_err_t weather_choose_colors(bool enabled)
{
    weather_snapshot_t s; weather_snapshot(&s);
    esp_err_t e=save_config(s.city,s.theme,enabled);
    portENTER_CRITICAL(&lock); shared.save_warning=e!=ESP_OK;
    if(e==ESP_OK) shared.dynamic_colors=enabled;
    shared.revision++; portEXIT_CRITICAL(&lock); return e;
}
void weather_activate(bool active)
{
    portENTER_CRITICAL(&lock); app_active=active; shared.active=app_active||desktop_owner; shared.revision++;
    if(active&&!shared.data.valid) wanted=true;
    portEXIT_CRITICAL(&lock);
}
void weather_desktop_activate(bool active)
{
    portENTER_CRITICAL(&lock); desktop_owner=active; shared.active=app_active||desktop_owner;
    if(active&&!shared.data.valid) wanted=true;
    shared.revision++; portEXIT_CRITICAL(&lock);
}
bool weather_refresh(void)
{
    bool ok;
    portENTER_CRITICAL(&lock);
    ok=shared.network&&shared.time_valid&&shared.phase!=WEATHER_FETCHING&&
        (!last_attempt||monotonic()-last_attempt>=60)&&monotonic()>=blocked_until;
    if(ok) { wanted=true; next_due=0; }
    portEXIT_CRITICAL(&lock); return ok;
}
typedef struct { char *body; size_t used; bool overflow; unsigned retry_after; } response_t;
static esp_err_t http_event(esp_http_client_event_t *e)
{
    response_t *r=e->user_data;
    if(e->event_id==HTTP_EVENT_ON_HEADER&&e->header_key&&e->header_value&&
       strcasecmp(e->header_key,"Retry-After")==0) {
        unsigned long n=strtoul(e->header_value,NULL,10);
        if(n>0&&n<=86400) r->retry_after=(unsigned)n;
    }
    if(e->event_id==HTTP_EVENT_ON_DATA&&e->data_len>0) {
        if((size_t)e->data_len>RESPONSE_LIMIT-r->used) { r->overflow=true; return ESP_FAIL; }
        memcpy(r->body+r->used,e->data,e->data_len); r->used+=e->data_len;
        r->body[r->used]=0;
    }
    return ESP_OK;
}
static esp_err_t fetch(unsigned city,weather_data_t *result,int *status,unsigned *retry_after)
{
    const weather_city_t *c=&weather_cities[city];
    char url[768];
    snprintf(url,sizeof(url),"https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
        "&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m"
        "&daily=weather_code,temperature_2m_max,temperature_2m_min&forecast_days=4"
        "&hourly=temperature_2m,weather_code,precipitation_probability,is_day&forecast_hours=24"
        "&timezone=Asia%%2FShanghai&timeformat=unixtime&wind_speed_unit=ms&temperature_unit=celsius",
        (double)c->latitude,(double)c->longitude);
    response_t response={0};
    response.body=heap_caps_calloc(1,RESPONSE_LIMIT+1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!response.body) return ESP_ERR_NO_MEM;
    esp_http_client_config_t config={.url=url,.timeout_ms=10000,.is_async=true,
        .crt_bundle_attach=esp_crt_bundle_attach,.event_handler=http_event,
        .user_data=&response,.buffer_size=2048,.buffer_size_tx=1024,
        .disable_auto_redirect=true,.keep_alive_enable=false};
    esp_http_client_handle_t client=esp_http_client_init(&config);
    esp_err_t e=ESP_ERR_NO_MEM;
    if(client) {
        esp_http_client_set_header(client,"Accept","application/json");
        esp_http_client_set_header(client,"Accept-Encoding","identity");
        /* Own and clean up the client on this worker. Poll asynchronous HTTPS
         * so app switching can cancel without racing socket/client destruction. */
        int64_t deadline=esp_timer_get_time()+10000000;
        do {
            if(atomic_load(&stop_requested)){e=ESP_ERR_INVALID_STATE;break;}
            e=esp_http_client_perform(client);
            if(e!=ESP_ERR_HTTP_EAGAIN)break;
            if(esp_timer_get_time()>=deadline){e=ESP_ERR_TIMEOUT;break;}
            vTaskDelay(pdMS_TO_TICKS(20));
        }while(true);
        *status=esp_http_client_get_status_code(client);
        *retry_after=response.retry_after;
        if(e==ESP_OK&&(*status!=200||response.overflow)) e=ESP_FAIL;
        if(e==ESP_OK&&!weather_parse(response.body,city,time(NULL),result)) e=ESP_ERR_INVALID_RESPONSE;
        esp_http_client_cleanup(client);
    }
    heap_caps_free(response.body); return e;
}
static void task(void *unused)
{
    (void)unused; unsigned failures=0; bool previous_network=false;
    for(;;) {
        if(atomic_load(&stop_requested)) { for(;;) vTaskSuspend(NULL); }
        wifi_status_t net; wifi_service_status(&net);
        bool network=net.state==NET_CONNECTED;
        if(network&&!ntp_started) {
            esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
            esp_sntp_setservername(0,"ntp.aliyun.com");
            esp_sntp_setservername(1,"time.cloudflare.com");
            esp_sntp_setservername(2,"time.windows.com");
            esp_sntp_set_time_sync_notification_cb(synced);
            esp_sntp_set_sync_interval(6*60*60*1000);
            esp_sntp_init(); ntp_started=true;
        }
        portENTER_CRITICAL(&lock);
        if(shared.network!=network) { shared.network=network; shared.revision++; }
        if(network&&!previous_network&&shared.active&&shared.phase!=WEATHER_LIMITED&&
           (!shared.data.valid||time(NULL)-shared.data.fetched>=900)) { next_due=0; wanted=true; }
        if(shared.active&&!network&&shared.phase!=WEATHER_WAIT_WIFI) { shared.phase=WEATHER_WAIT_WIFI; shared.revision++; }
        if(shared.active&&network&&!shared.time_valid&&shared.phase!=WEATHER_WAIT_TIME) { shared.phase=WEATHER_WAIT_TIME; shared.revision++; }
        bool run=shared.active&&network&&shared.time_valid&&monotonic()>=next_due&&monotonic()>=blocked_until&&
            (wanted||!shared.data.valid||time(NULL)-shared.data.fetched>=900);
        unsigned city=shared.city,version=generation;
        if(run) { shared.phase=WEATHER_FETCHING; shared.revision++; wanted=false; last_attempt=monotonic(); }
        portEXIT_CRITICAL(&lock);
        previous_network=network;
        if(run) {
            weather_data_t result={0}; int status=0; unsigned retry_after=0;
            esp_err_t e=fetch(city,&result,&status,&retry_after);
            if(atomic_load(&stop_requested))continue;
            ESP_LOGI(TAG,"city=%u HTTP=%d result=%s internal=%u",city,status,esp_err_to_name(e),
                (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
            portENTER_CRITICAL(&lock);
            if(version==generation) {
                shared.http_status=status; shared.error=e;
                if(e==ESP_OK) { shared.data=result; shared.phase=WEATHER_OK; failures=0; blocked_until=0; next_due=monotonic()+900; }
                else {
                    static const unsigned delays[]={30,120,300,900};
                    unsigned wait=delays[failures<4?failures:3]; if(failures<4) failures++;
                    if(status==429) { if(wait<900) wait=900; if(retry_after>wait) wait=retry_after; }
                    if(status==400||status==401||status==403) wait=3600;
                    if(status==429||status==400||status==401||status==403) blocked_until=monotonic()+wait;
                    shared.phase=status==429?WEATHER_LIMITED:WEATHER_ERROR;
                    next_due=monotonic()+wait+esp_random()%11; wanted=true;
                }
                shared.revision++;
            } else { next_due=monotonic()+5; wanted=true; failures=0; }
            portEXIT_CRITICAL(&lock);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}
esp_err_t weather_start(void)
{
    if(started) return atomic_load(&stop_requested)?ESP_ERR_INVALID_STATE:ESP_OK;
    atomic_store(&stop_requested,false);
    setenv("TZ","CST-8",1); tzset();
    nvs_handle_t h; uint32_t config=0;
    if(settings_available()&&nvs_open("app_weather",NVS_READONLY,&h)==ESP_OK) {
        if(nvs_get_u32(h,"config_v2",&config)==ESP_OK&&
           weather_config_decode(config,&shared.city,&shared.theme,&shared.dynamic_colors)) {
            ESP_LOGI(TAG,"Appearance restored: theme=%u dynamic=%u",shared.theme,shared.dynamic_colors);
        } else {
            /* Retain legacy city; the first upgrade starts with the requested new default. */
            if(nvs_get_u32(h,"config_v1",&config)==ESP_OK&&
               (config>>16)==1&&(config&255)<WEATHER_CITIES) shared.city=config&255;
        }
        nvs_close(h);
    }
    ESP_LOGI(TAG,"Appearance: theme=%u dynamic=%u",shared.theme,shared.dynamic_colors);
    if(xTaskCreate(task,"weather",12288,NULL,3,&weather_task)!=pdPASS) return ESP_ERR_NO_MEM;
    started=true; return ESP_OK;
}

esp_err_t weather_stop(void) {
 if(!started) return ESP_OK;
 atomic_store(&stop_requested,true);
 esp_err_t e=service_task_join(&weather_task,30000); if(e!=ESP_OK) return e;
 if(ntp_started) { esp_sntp_stop(); esp_sntp_set_time_sync_notification_cb(NULL); ntp_started=false; }
 started=false; app_active=desktop_owner=false;
 portENTER_CRITICAL(&lock); shared.active=shared.network=false; shared.phase=WEATHER_IDLE; shared.revision++; portEXIT_CRITICAL(&lock);
 return ESP_OK;
}
