from pathlib import Path
import tempfile,subprocess
root=Path('outputs/TouchDeskWireless').resolve();s=(root/'main/apps/weather/weather_service.c').read_text(encoding='utf-8');fetch=s[s.index('static esp_err_t fetch('):s.index('static void task(')]
prefix=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <assert.h>
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_ERR_NO_MEM 2
#define ESP_ERR_INVALID_STATE 3
#define ESP_ERR_TIMEOUT 4
#define ESP_ERR_INVALID_RESPONSE 5
#define ESP_ERR_HTTP_EAGAIN 6
#define RESPONSE_LIMIT 16384
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define pdMS_TO_TICKS(x) (x)
#define atomic_load(x) (*(x))
typedef int esp_err_t;
typedef struct {float latitude,longitude;} weather_city_t;
typedef struct {bool valid;} weather_data_t;
static const weather_city_t weather_cities[]={{1,2}};
static bool stop_requested;static int64_t now;static unsigned mode,calls,cleaned,allocs,frees,parsed;
static int64_t esp_timer_get_time(void){return now;}
static void vTaskDelay(unsigned ms){now+=(int64_t)ms*1000;}
static void *heap_caps_calloc(size_t n,size_t size,unsigned caps){allocs++;return calloc(n,size);}
static void heap_caps_free(void *p){frees++;free(p);}
typedef struct {char *body;size_t used;bool overflow;unsigned retry_after;} response_t;
typedef struct {const char *url;int timeout_ms;bool is_async;int crt_bundle_attach;int (*event_handler)(void *);void *user_data;int buffer_size,buffer_size_tx;bool disable_auto_redirect,keep_alive_enable;} esp_http_client_config_t;
typedef void *esp_http_client_handle_t;
static int http_event(void *p){return 0;}
#define esp_crt_bundle_attach 0
static int ctx;
static esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *c){assert(c->is_async);return &ctx;}
static void esp_http_client_set_header(void *c,const char *k,const char *v){}
static int esp_http_client_perform(void *c){calls++;if(mode==1){stop_requested=true;return ESP_ERR_HTTP_EAGAIN;}if(mode==2){now+=1000000;return ESP_ERR_HTTP_EAGAIN;}if(mode==3)return ESP_FAIL;return calls<=2?ESP_ERR_HTTP_EAGAIN:ESP_OK;}
static int esp_http_client_get_status_code(void *c){return 200;}
static void esp_http_client_cleanup(void *c){cleaned++;}
static bool weather_parse(const char *b,unsigned city,time_t t,weather_data_t *out){parsed++;out->valid=true;return true;}
'''
main=r'''
int main(void){
 weather_data_t result;int status;unsigned retry;
 for(mode=0;mode<4;mode++){
  stop_requested=false;now=0;calls=cleaned=allocs=frees=parsed=0;
  int rc=fetch(0,&result,&status,&retry);
  assert(cleaned==1&&allocs==1&&frees==1);
  if(mode==0)assert(rc==ESP_OK&&calls==3&&parsed==1);
  if(mode==1)assert(rc==ESP_ERR_INVALID_STATE&&calls==1&&parsed==0&&now<=20000);
  if(mode==2)assert(rc==ESP_ERR_TIMEOUT&&calls<=10&&parsed==0);
  if(mode==3)assert(rc==ESP_FAIL&&calls==1&&parsed==0);
 }
 puts("PASS production HTTPS fetch: EAGAIN progress, cancellation at next poll, bounded timeout, failures, and client/body cleanup");
}
'''
with tempfile.TemporaryDirectory(prefix='weather-cancel-') as tmp:
 p=Path(tmp);(p/'test.c').write_text(prefix+fetch+main,encoding='utf-8');cc='C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe';subprocess.run([cc,str(p/'test.c'),'-o',str(p/'test.exe')],check=True);subprocess.run([str(p/'test.exe')],check=True)
