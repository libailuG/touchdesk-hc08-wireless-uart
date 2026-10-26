"""Regression: gateway-provided credentials must reach transport authentication."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'main/services/xiaozhi_service.c').read_text(encoding='utf-8')
config=s[s.index('typedef struct {\n char ws_url'):s.index('static SemaphoreHandle_t')]
get_string=s[s.index('static bool get_string('):s.index('typedef struct {char *buffer;')]
fetch=s[s.index('static int fetch_config('):s.index('static bool udp_open(')]
head=r'''
#include "xiaozhi_protocol.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_8BIT 0
#define portMAX_DELAY 0
#define ESP_LOGI(...) ((void)0)
static int guard;
static struct {char activation[96];} snapshot;
static const char *reply;
static void *heap_caps_malloc(size_t n,int caps){return malloc(n);}
static void xSemaphoreTake(int a,int b){}
static void xSemaphoreGive(int a){}
static char *device_json(void){char *p=malloc(3);strcpy(p,"{}");return p;}
static int http_post(const char*u,const char*b,char*out,size_t n){snprintf(out,n,"%s",reply);return 200;}
'''
tail=r'''
int main(void){
 reply="{\"websocket\":{\"url\":\"wss://example.invalid/\",\"token\":\"test-token\"}}";
 assert(fetch_config()==1&&!strcmp(config.token,"test-token"));
 reply="{\"mqtt\":{\"endpoint\":\"example.invalid\",\"client_id\":\"GID_test@@@device\",\"username\":\"user\",\"password\":\"pass\",\"publish_topic\":\"device-server\",\"subscribe_topic\":\"null\"}}";
 assert(fetch_config()==1&&!strcmp(config.subscribe,"null"));
 reply="{\"mqtt\":{\"endpoint\":\"example.invalid\",\"client_id\":\"device\",\"username\":\"user\",\"password\":\"pass\",\"publish_topic\":\"device-server\"}}";
 assert(fetch_config()==1&&!config.subscribe[0]);
 reply="{\"websocket\":{\"url\":\"ws://example.invalid/\",\"token\":\"test-token\"}}";
 assert(fetch_config()==0);
 reply="{\"websocket\":{\"url\":\"wss://example.invalid/\",\"version\":99}}";
 assert(fetch_config()==0);
 reply="{\"activation\":{\"code\":\"123456\"}}";
 assert(fetch_config()==2&&!strcmp(snapshot.activation,"123456"));
 reply="{}";assert(fetch_config()==0);
 puts("PASS gateway config: token naming, broker push without subscription, TLS/version rejection, activation");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='xiaozhi-config-') as d:
 d=Path(d);(d/'test.c').write_text(head+config+get_string+fetch+tail,encoding='utf-8');exe=d/'test.exe'
 j=Path('C:/play/esp32s3touchscreen/toolchains/idf-5.4.1-cn/components/json/cJSON')
 subprocess.run(['C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe','-I'+str(r/'main/services'),'-I'+str(j),str(d/'test.c'),str(r/'main/services/xiaozhi_protocol.c'),str(j/'cJSON.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
