"""Exercise the production HTTPS body collector, including empty 202 binding responses."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'main/services/xiaozhi_service.c').read_text(encoding='utf-8')
production=s[s.index('typedef struct {char *buffer;'):s.index('static char *device_json(')]
head=r'''#include <stdio.h>
#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#define ESP_OK 0
#define ESP_FAIL 1
#define HTTP_EVENT_ON_DATA 1
#define HTTP_METHOD_POST 1
#define ESP_LOGW(...) ((void)0)
#define XZ_BOARD_TYPE "huiqin-touchdesk-usb"
#define TAG "test"
typedef int esp_err_t;
typedef struct {int event_id,data_len;void *data,*user_data;} esp_http_client_event_t;
typedef struct {const char *url;int method,timeout_ms;int(*crt_bundle_attach)(void*);bool disable_auto_redirect;int buffer_size;esp_err_t(*event_handler)(esp_http_client_event_t*);void *user_data;} esp_http_client_config_t;
static esp_http_client_config_t saved;
typedef void *esp_http_client_handle_t;
static unsigned initialized,cleaned;static int status=202,transport;static const char *reply="";
static const char *mac="00:11:22:33:44:55",*uuid="test-uuid";
static int esp_crt_bundle_attach(void*p){return 0;}
static const char *esp_err_to_name(int e){return "mock";}
typedef struct {char *version;} desc_t;
static desc_t *esp_app_get_description(void){static desc_t d={"0.8.0"};return &d;}
static void *esp_http_client_init(const esp_http_client_config_t *c){saved=*c;initialized++;assert(c->disable_auto_redirect&&c->crt_bundle_attach);return &saved;}
static void esp_http_client_set_header(void*h,const char*k,const char*v){}
static void esp_http_client_set_post_field(void*h,const char*b,int n){assert(n==(int)strlen(b));}
static int esp_http_client_get_status_code(void*h){return status;}
static int esp_http_client_perform(void*h){
 if(transport)return transport;
 for(int pos=0;reply[pos];){int n=strlen(reply+pos);if(n>3)n=3;esp_http_client_event_t e={HTTP_EVENT_ON_DATA,n,(void*)(reply+pos),saved.user_data};if(saved.event_handler(&e))return ESP_FAIL;pos+=n;}
 return ESP_OK;
}
static void esp_http_client_cleanup(void*h){cleaned++;}
'''
tail=r'''int main(void){
 char buffer[9];memset(buffer,'?',sizeof(buffer));
 assert(http_post("http://bad","{}",buffer,sizeof(buffer))==-1&&initialized==0);
 assert(http_post("https://test","{}",buffer,sizeof(buffer))==202&&buffer[0]==0&&cleaned==1);
 status=200;reply="12345678";assert(http_post("https://test","{}",buffer,sizeof(buffer))==200&&!strcmp(buffer,"12345678"));
 reply="123456789";buffer[8]='!';assert(http_post("https://test","{}",buffer,8)==-1&&buffer[8]=='!');
 reply="";status=404;assert(http_post("https://test","{}",buffer,sizeof(buffer))==404);
 status=0;transport=ESP_FAIL;assert(http_post("https://test","{}",buffer,sizeof(buffer))==-1);
 assert(initialized==cleaned);puts("PASS production HTTPS: empty binding 202, chunked body assembly, exact capacity, overflow canary, non-TLS rejection, HTTP error, transport cleanup");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='xiaozhi-http-') as d:
 d=Path(d);(d/'test.c').write_text(head+production+tail,encoding='utf-8');exe=d/'test.exe'
 subprocess.run(['C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe',str(d/'test.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
