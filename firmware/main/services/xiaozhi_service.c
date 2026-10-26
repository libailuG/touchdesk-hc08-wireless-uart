/* XiaoZhi protocol integration for TouchDesk. No cloud firmware is installed.
 * Protocol reference: 78/xiaozhi-esp32 aab98ab (MIT); see docs/XIAOZHI.md. */
#include "xiaozhi_service.h"
#include "service_task.h"
#include "xiaozhi_protocol.h"
#include "voice_audio.h"
#include "music_service.h"
#include "wifi_service.h"
#include "esp_http_client.h"
#include "esp_websocket_client.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs.h"
#include "mbedtls/aes.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"
#include "esp_opus_enc.h"
#include "esp_opus_dec.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define TAG "xiaozhi"
#define PACKETS 40
typedef struct { size_t size; bool binary; uint8_t data[XZ_PACKET_MAX+1]; } packet_t;
typedef struct {
 char ws_url[512],token[1024],endpoint[192],client_id[256],username[512],password[512];
 char publish[192],subscribe[192];
 unsigned version;
 bool activation_challenge;
} config_t;
static config_t config;
static SemaphoreHandle_t guard;
static TaskHandle_t chat_task;
static StackType_t *chat_stack;
static atomic_bool stop_requested;
static StaticTask_t chat_task_control;
static QueueHandle_t incoming,free_packets;
static packet_t *pool;
static xiaozhi_snapshot_t snapshot;
static atomic_bool continuous,requested,held,cancelled,leaving,ui_request,net_connected,net_fault;
static atomic_uint leave_generation,leave_done;
static char uuid[37],mac[18],session[XZ_SESSION_MAX];
static esp_websocket_client_handle_t ws;
static esp_mqtt_client_handle_t mqtt;
static int udp_fd=-1;
static uint8_t udp_key[16],udp_nonce[16];
static uint32_t local_sequence,remote_sequence;
static bool channel_ready,tts_started,tts_stopped;
static int64_t response_started,last_receive,drain_since;
static void *encoder,*decoder;
static int16_t *input_pcm,*mono_pcm,*play_pcm;
static uint8_t *encoded_pcm;
static int encoder_bytes,encoder_capacity;
static size_t mono_used;
static xz_decimator_t decimator;
/* Reassembly belongs to the transport callback; lifecycle joins it before reset. */
static xz_assembly_t assembly;

static void state(xiaozhi_state_t value,const char *message) {
 xSemaphoreTake(guard,portMAX_DELAY);snapshot.state=value;
 xz_display_text(snapshot.status,sizeof(snapshot.status),message);xSemaphoreGive(guard);
 ESP_LOGI(TAG,"state=%u %s",value,message);
}
void xiaozhi_snapshot(xiaozhi_snapshot_t *out) {
 if(!guard){memset(out,0,sizeof(*out));return;}
 xSemaphoreTake(guard,portMAX_DELAY);*out=snapshot;xSemaphoreGive(guard);
 out->held=atomic_load(&held);out->continuous=atomic_load(&continuous);
}
void xiaozhi_continuous_start(void) {if(!guard)return;atomic_store(&continuous,true);atomic_store(&held,true);atomic_store(&requested,true);}
void xiaozhi_connect(void) {if(guard)atomic_store(&requested,true);}
void xiaozhi_hold(bool on) {if(!guard)return;atomic_store(&held,on);if(on)atomic_store(&requested,true);}
void xiaozhi_cancel(void) {atomic_store(&continuous,false);atomic_store(&held,false);atomic_store(&cancelled,true);}
void xiaozhi_leave(void) {atomic_fetch_add(&leave_generation,1);atomic_store(&continuous,false);atomic_store(&held,false);atomic_store(&leaving,true);}
bool xiaozhi_usb_released(void) {return !chat_task || (atomic_load(&leave_done)==atomic_load(&leave_generation) && !voice_audio_busy());}
bool xiaozhi_take_ui_request(void) {return atomic_exchange(&ui_request,false);}
bool xiaozhi_console(const char *line) {
 if(!strcmp(line,"chat ui"))atomic_store(&ui_request,true);
 else if(!strcmp(line,"chat connect"))xiaozhi_connect();
 else if(!strcmp(line,"chat auto"))xiaozhi_continuous_start();
 else if(!strcmp(line,"chat start"))xiaozhi_hold(true);
 else if(!strcmp(line,"chat stop"))xiaozhi_hold(false);
 else if(!strcmp(line,"chat cancel"))xiaozhi_cancel();
 else if(!strcmp(line,"chat identity")){ESP_LOGI(TAG,"identity mac=%s uuid=%s",mac,uuid);}
 else if(!strcmp(line,"chat pairing")){
  if(guard){xSemaphoreTake(guard,portMAX_DELAY);ESP_LOGI(TAG,"pairing state=%u code=%s",snapshot.state,snapshot.activation);xSemaphoreGive(guard);}
 }
 else if(!strcmp(line,"chat status")) {
  /* The UART task has a small stack; never put the full transcript snapshot here. */
  if(guard){
   xSemaphoreTake(guard,portMAX_DELAY);
   ESP_LOGI(TAG,"state=%u available=%u tx=%u rx=%u seconds=%u internal=%u stack=%u chat_stack=%u status=%s",snapshot.state,snapshot.available,snapshot.tx_frames,snapshot.rx_frames,snapshot.seconds,(unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),(unsigned)uxTaskGetStackHighWaterMark(NULL),chat_task?(unsigned)uxTaskGetStackHighWaterMark(chat_task):0,snapshot.status);
   xSemaphoreGive(guard);
  }
 } else return false;
 return true;
}
static bool interrupted(void) {return atomic_load(&cancelled)||atomic_load(&leaving);}
static void enqueue(const uint8_t *data,size_t length,bool binary) {
 if(!length||length>XZ_PACKET_MAX){atomic_store(&net_fault,true);return;}
 packet_t *p=NULL;
 if(xQueueReceive(free_packets,&p,0)!=pdTRUE){atomic_store(&net_fault,true);return;}
 memcpy(p->data,data,length);p->data[length]=0;p->size=length;p->binary=binary;
 if(xQueueSend(incoming,&p,0)!=pdTRUE){xQueueSend(free_packets,&p,0);atomic_store(&net_fault,true);}
}
static void ws_event(void *arg,esp_event_base_t base,int32_t id,void *event) {
 esp_websocket_event_data_t *e=event;
 if(id==WEBSOCKET_EVENT_CONNECTED){atomic_store(&net_connected,true);return;}
 if(id==WEBSOCKET_EVENT_DISCONNECTED||id==WEBSOCKET_EVENT_ERROR){atomic_store(&net_fault,true);return;}
 if(id!=WEBSOCKET_EVENT_DATA||e->op_code>2)return;
 int result=xz_assemble_ws(&assembly,e->op_code,e->fin,e->payload_len,e->payload_offset,e->data_ptr,e->data_len);
 if(result<0)atomic_store(&net_fault,true);
 if(result==1)enqueue(assembly.data,assembly.used,assembly.type==2);
}
static void mqtt_event(void *arg,esp_event_base_t base,int32_t id,void *event) {
 esp_mqtt_event_handle_t e=event;
 if(id==MQTT_EVENT_CONNECTED){
  if(!*config.subscribe||!strcmp(config.subscribe,"null"))atomic_store(&net_connected,true);
  else if(esp_mqtt_client_subscribe(mqtt,config.subscribe,1)<0)atomic_store(&net_fault,true);
 } else if(id==MQTT_EVENT_SUBSCRIBED)atomic_store(&net_connected,true);
 else if(id==MQTT_EVENT_DISCONNECTED||id==MQTT_EVENT_ERROR)atomic_store(&net_fault,true);
 else if(id==MQTT_EVENT_DATA){
  int result=xz_assemble_mqtt(&assembly,e->total_data_len,e->current_data_offset,e->data,e->data_len);
  if(result<0)atomic_store(&net_fault,true);
  if(result==1)enqueue(assembly.data,assembly.used,false);
 }
}
static void disconnect(void) {
 atomic_store(&continuous,false);
 channel_ready=false;
 if(ws){esp_websocket_client_stop(ws);esp_websocket_client_destroy(ws);ws=NULL;}
 if(mqtt){esp_mqtt_client_stop(mqtt);esp_mqtt_client_destroy(mqtt);mqtt=NULL;}
 if(udp_fd>=0){close(udp_fd);udp_fd=-1;}
 packet_t *p;while(xQueueReceive(incoming,&p,0)==pdTRUE)xQueueSend(free_packets,&p,0);
 memset(&assembly,0,sizeof(assembly));
 memset(udp_key,0,sizeof(udp_key));memset(udp_nonce,0,sizeof(udp_nonce));
 session[0]=0;atomic_store(&net_connected,false);atomic_store(&net_fault,false);
}
static bool send_text(const char *text) {
 size_t n=strlen(text);
 if(ws)return esp_websocket_client_send_text(ws,text,n,pdMS_TO_TICKS(500))==(int)n;
 return mqtt&&esp_mqtt_client_publish(mqtt,config.publish,text,n,0,0)>=0;
}
static bool send_json(cJSON *obj) {
 if(!obj){return false;}
 char *s=cJSON_PrintUnformatted(obj);cJSON_Delete(obj);
 if(!s){return false;}
 bool ok=send_text(s);cJSON_free(s);return ok;
}
static cJSON *message(const char *type) {
 cJSON *o=cJSON_CreateObject();if(o){cJSON_AddStringToObject(o,"type",type);cJSON_AddStringToObject(o,"session_id",session);}return o;
}
static bool xz_listen(bool start) {
 cJSON *o=message("listen");if(!o)return false;
 cJSON_AddStringToObject(o,"state",start?"start":"stop");if(start)cJSON_AddStringToObject(o,"mode",atomic_load(&continuous)?"auto":"manual");return send_json(o);
}
static bool xz_crypt(const uint8_t *input,size_t n,uint8_t nonce[16],uint8_t *out) {
 mbedtls_aes_context aes;mbedtls_aes_init(&aes);size_t offset=0;uint8_t stream[16]={0};
 int err=mbedtls_aes_setkey_enc(&aes,udp_key,128);
 if(!err)err=mbedtls_aes_crypt_ctr(&aes,n,&offset,nonce,stream,input,out);
 mbedtls_aes_free(&aes);return err==0;
}
static bool send_audio(const uint8_t *p,size_t n) {
 if(!n||n>XZ_PACKET_MAX)return false;
 uint8_t data[XZ_PACKET_MAX+16];
 if(ws){
  size_t header=config.version==2?16:config.version==3?4:0;
  if(header==16){memset(data,0,16);data[1]=2;uint32_t len=htonl(n);memcpy(data+12,&len,4);}
  if(header==4){memset(data,0,4);data[2]=n>>8;data[3]=n;}
  memcpy(data+header,p,n);return esp_websocket_client_send_bin(ws,(char*)data,n+header,pdMS_TO_TICKS(500))==(int)(n+header);
 }
 if(udp_fd<0||local_sequence==UINT32_MAX)return false;
 memcpy(data,udp_nonce,16);data[2]=n>>8;data[3]=n;
 uint32_t seq=htonl(++local_sequence);memcpy(data+12,&seq,4);memset(data+8,0,4);
 uint8_t nonce[16];memcpy(nonce,data,16);
 return xz_crypt(p,n,nonce,data+16)&&send(udp_fd,data,n+16,0)==(int)n+16;
}
static bool get_string(const cJSON *o,const char *key,char *out,size_t cap,bool required) {
 const char *s=xz_string(o,key);return (!required||*s)&&xz_copy(out,cap,s);
}
typedef struct {char *buffer;size_t capacity,used;bool overflow;} http_body_t;
static esp_err_t http_event(esp_http_client_event_t *e) {
 http_body_t *b=e->user_data;
 if(e->event_id==HTTP_EVENT_ON_DATA&&e->data_len>0){
  if((size_t)e->data_len>b->capacity-1-b->used){b->overflow=true;return ESP_FAIL;}
  memcpy(b->buffer+b->used,e->data,e->data_len);b->used+=e->data_len;b->buffer[b->used]=0;
 }
 return ESP_OK;
}
static int http_post(const char *url,const char *body,char *response,size_t cap) {
 if(strncmp(url,"https://",8))return -1;
 http_body_t payload={.buffer=response,.capacity=cap};response[0]=0;
 esp_http_client_config_t c={.url=url,.method=HTTP_METHOD_POST,.timeout_ms=8000,.crt_bundle_attach=esp_crt_bundle_attach,.disable_auto_redirect=true,.buffer_size=2048,.event_handler=http_event,.user_data=&payload};
 esp_http_client_handle_t h=esp_http_client_init(&c);if(!h)return -1;
 char agent[128];snprintf(agent,sizeof(agent),"%s/%s",XZ_BOARD_TYPE,esp_app_get_description()->version);
 esp_http_client_set_header(h,"Device-Id",mac);esp_http_client_set_header(h,"Client-Id",uuid);
 esp_http_client_set_header(h,"Activation-Version","1");esp_http_client_set_header(h,"User-Agent",agent);
 esp_http_client_set_header(h,"Accept-Language","zh-CN");esp_http_client_set_header(h,"Content-Type","application/json");
 esp_http_client_set_post_field(h,body,strlen(body));
 esp_err_t err=esp_http_client_perform(h);int status_code=esp_http_client_get_status_code(h);
 int result=err==ESP_OK&&!payload.overflow?status_code:-1;
 if(result!=200&&result!=202)ESP_LOGW(TAG,"Cloud HTTP=%d transport=%s bytes=%u overflow=%u",status_code,esp_err_to_name(err),(unsigned)payload.used,payload.overflow);
 esp_http_client_cleanup(h);return result;
}
static char *device_json(void) {
 cJSON *o=cJSON_CreateObject();if(!o)return NULL;
 cJSON_AddNumberToObject(o,"version",2);cJSON_AddStringToObject(o,"language","zh-CN");
 cJSON_AddNumberToObject(o,"flash_size",16777216);cJSON_AddNumberToObject(o,"minimum_free_heap_size",esp_get_minimum_free_heap_size());
 cJSON_AddStringToObject(o,"mac_address",mac);cJSON_AddStringToObject(o,"uuid",uuid);cJSON_AddStringToObject(o,"chip_model_name","esp32s3");
 cJSON *app=cJSON_AddObjectToObject(o,"application");cJSON_AddStringToObject(app,"name","touchdesk");cJSON_AddStringToObject(app,"version",esp_app_get_description()->version);cJSON_AddStringToObject(app,"idf_version",esp_app_get_description()->idf_ver);
 cJSON *board=cJSON_AddObjectToObject(o,"board");cJSON_AddStringToObject(board,"type",XZ_BOARD_TYPE);cJSON_AddStringToObject(board,"manufacturer","huiqin");cJSON_AddStringToObject(board,"mac",mac);cJSON_AddStringToObject(board,"name",XZ_BOARD_TYPE);
 cJSON *d=cJSON_AddObjectToObject(o,"display");cJSON_AddBoolToObject(d,"monochrome",false);cJSON_AddNumberToObject(d,"width",320);cJSON_AddNumberToObject(d,"height",480);
 char *s=cJSON_PrintUnformatted(o);cJSON_Delete(o);return s;
}
/* 1: usable config; 2: user binding required; 0: rejected/malformed. */
static int fetch_config(void) {
 char *body=device_json(),*response=heap_caps_malloc(16384,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!body||!response){cJSON_free(body);free(response);return 0;}
 int code=http_post(XZ_OTA_URL,body,response,16384);cJSON_free(body);
 if(code!=200){free(response);return 0;}
 cJSON *o=cJSON_Parse(response);memset(response,0,16384);free(response);if(!o)return 0;
 memset(&config,0,sizeof(config));config.version=1;
 cJSON *a=cJSON_GetObjectItemCaseSensitive(o,"activation");
 const char *binding=xz_string(a,"code");
 config.activation_challenge=*xz_string(a,"challenge")!=0;
 xSemaphoreTake(guard,portMAX_DELAY);xz_display_text(snapshot.activation,sizeof(snapshot.activation),binding);xSemaphoreGive(guard);
 if(*binding||config.activation_challenge){cJSON_Delete(o);return 2;}
 cJSON *w=cJSON_GetObjectItemCaseSensitive(o,"websocket");
 bool websocket=get_string(w,"url",config.ws_url,sizeof(config.ws_url),true)&&!strncmp(config.ws_url,"wss://",6)&&get_string(w,"token",config.token,sizeof(config.token),false);
 cJSON *v=cJSON_GetObjectItemCaseSensitive(w,"version");if(cJSON_IsNumber(v))config.version=v->valueint;
 /* The official gateway can return test-token even for a listed device.
  * Authenticate through the gateway; token naming is not an auth result. */
 websocket=websocket&&config.version>=1&&config.version<=3;
 cJSON *m=cJSON_GetObjectItemCaseSensitive(o,"mqtt");
 bool mq=get_string(m,"endpoint",config.endpoint,sizeof(config.endpoint),true)&&get_string(m,"client_id",config.client_id,sizeof(config.client_id),true)&&get_string(m,"username",config.username,sizeof(config.username),true)&&get_string(m,"password",config.password,sizeof(config.password),true)&&get_string(m,"publish_topic",config.publish,sizeof(config.publish),true)&&get_string(m,"subscribe_topic",config.subscribe,sizeof(config.subscribe),false);
 ESP_LOGI(TAG,"Config ws=%u version=%u dummy_ws=%u mqtt=%u dummy_mqtt=%u subscribe_len=%u",websocket,config.version,!strcmp(config.token,"test-token"),mq,!strncmp(config.client_id,"GID_test",8),(unsigned)strlen(config.subscribe));
 cJSON_Delete(o);if(websocket)return 1;config.ws_url[0]=0;return mq?1:0;
}
static bool udp_open(const cJSON *o) {
 const char *host=xz_string(o,"server");cJSON *p=cJSON_GetObjectItemCaseSensitive(o,"port");
 if(!*host||strlen(host)>253||!cJSON_IsNumber(p)||p->valuedouble!=p->valueint||p->valueint<1||p->valueint>65535||!xz_hex16(xz_string(o,"key"),udp_key)||!xz_hex16(xz_string(o,"nonce"),udp_nonce))return false;
 struct addrinfo hints={.ai_family=AF_INET,.ai_socktype=SOCK_DGRAM},*address=NULL;char port[8];snprintf(port,sizeof(port),"%d",p->valueint);
 if(getaddrinfo(host,port,&hints,&address)!=0)return false;
 udp_fd=socket(address->ai_family,address->ai_socktype,0);
 bool ok=udp_fd>=0&&connect(udp_fd,address->ai_addr,address->ai_addrlen)==0;
 freeaddrinfo(address);
 if(!ok){if(udp_fd>=0)close(udp_fd);udp_fd=-1;return false;}
 struct timeval timeout={.tv_usec=1000};setsockopt(udp_fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
 local_sequence=remote_sequence=0;return true;
}
static void mcp_reply(const cJSON *request) {
 const cJSON *id=cJSON_GetObjectItemCaseSensitive(request,"id");if(!id)return;
 const char *method=xz_string(request,"method");cJSON *out=message("mcp");if(!out)return;
 cJSON *p=cJSON_AddObjectToObject(out,"payload");cJSON_AddStringToObject(p,"jsonrpc","2.0");cJSON_AddItemToObject(p,"id",cJSON_Duplicate(id,true));
 if(!strcmp(method,"initialize")){
  cJSON *r=cJSON_AddObjectToObject(p,"result");cJSON_AddStringToObject(r,"protocolVersion","2024-11-05");
  cJSON *cap=cJSON_AddObjectToObject(r,"capabilities");cJSON_AddObjectToObject(cap,"tools");
  cJSON *s=cJSON_AddObjectToObject(r,"serverInfo");cJSON_AddStringToObject(s,"name",XZ_BOARD_TYPE);cJSON_AddStringToObject(s,"version","0.8.0");
 }else if(!strcmp(method,"tools/list")){
  cJSON *r=cJSON_AddObjectToObject(p,"result");cJSON_AddArrayToObject(r,"tools");
 }else{cJSON *e=cJSON_AddObjectToObject(p,"error");cJSON_AddNumberToObject(e,"code",-32601);cJSON_AddStringToObject(e,"message","Method not supported");}
 if(!send_json(out))atomic_store(&net_fault,true);
}
static bool handle_json(packet_t *packet) {
 cJSON *o=cJSON_ParseWithLength((char*)packet->data,packet->size+1);if(!o)return false;
 const char *type=xz_string(o,"type");bool ok=true;
 if(!strcmp(type,"hello")){
  const char *transport=xz_string(o,"transport");cJSON *audio=cJSON_GetObjectItemCaseSensitive(o,"audio_params");
  cJSON *rate=cJSON_GetObjectItemCaseSensitive(audio,"sample_rate"),*duration=cJSON_GetObjectItemCaseSensitive(audio,"frame_duration"),*channels=cJSON_GetObjectItemCaseSensitive(audio,"channels");
  ok=!channel_ready&&xz_copy(session,sizeof(session),xz_string(o,"session_id"))&&*session&&(!*xz_string(audio,"format")||!strcmp(xz_string(audio,"format"),"opus"));
  if(cJSON_IsNumber(rate))ok=ok&&(rate->valueint==16000||rate->valueint==24000||rate->valueint==48000);
  if(cJSON_IsNumber(duration))ok=ok&&duration->valueint==60;
  if(cJSON_IsNumber(channels))ok=ok&&channels->valueint==1;
  ok=ok&&(ws?!strcmp(transport,"websocket"):!strcmp(transport,"udp")&&udp_open(cJSON_GetObjectItemCaseSensitive(o,"udp")));
  channel_ready=ok;
 }else if(!xz_valid_session(o,session)){/* Ignore messages from another channel. */}
 else if(!strcmp(type,"tts")){
  const char *s=xz_string(o,"state");
  if(!strcmp(s,"start")&&response_started){
   if(atomic_load(&continuous)&&!tts_started){if(!voice_audio_mode(VOICE_AUDIO_PLAY)){cJSON_Delete(o);return false;}atomic_store(&held,false);response_started=esp_timer_get_time();}
   tts_started=true;tts_stopped=false;state(XZ_SPEAKING,"正在说话 · 点击停止可打断");}
  else if(!strcmp(s,"stop")&&tts_started){tts_stopped=true;drain_since=esp_timer_get_time();}
  else if(!strcmp(s,"sentence_start")&&response_started){xSemaphoreTake(guard,portMAX_DELAY);xz_display_text(snapshot.answer,sizeof(snapshot.answer),xz_string(o,"text"));xSemaphoreGive(guard);}
 }else if(!strcmp(type,"stt")&&response_started){xSemaphoreTake(guard,portMAX_DELAY);xz_display_text(snapshot.user,sizeof(snapshot.user),xz_string(o,"text"));xSemaphoreGive(guard);}
 else if(!strcmp(type,"llm")){xSemaphoreTake(guard,portMAX_DELAY);xz_display_text(snapshot.emotion,sizeof(snapshot.emotion),xz_string(o,"emotion"));xSemaphoreGive(guard);}
 else if(!strcmp(type,"mcp"))mcp_reply(cJSON_GetObjectItemCaseSensitive(o,"payload"));
 else if(!strcmp(type,"goodbye")||!strcmp(type,"error"))ok=false;
 /* Remote system/reboot/upgrade/custom commands deliberately have no handler. */
 cJSON_Delete(o);return ok;
}
static bool decode_audio(const uint8_t *p,size_t size) {
 if(!tts_started||!response_started)return true;
 esp_audio_dec_in_raw_t raw={.buffer=(uint8_t*)p,.len=size};
 esp_audio_dec_out_frame_t out={.buffer=(uint8_t*)play_pcm,.len=11520};esp_audio_dec_info_t info={0};
 if(esp_opus_dec_decode(decoder,&raw,&out,&info)!=ESP_AUDIO_ERR_OK||raw.consumed!=size||out.decoded_size>11520||out.decoded_size%2||info.channel!=1||info.bits_per_sample!=16||info.sample_rate!=48000)return false;
 size_t count=out.decoded_size/2,used=0;int64_t deadline=esp_timer_get_time()+1500000;
 while(used<count&&!interrupted()&&esp_timer_get_time()<deadline){size_t n=voice_audio_write(play_pcm+used,count-used,20);used+=n;if(!voice_audio_ok())return false;}
 if(interrupted())return true;
 if(used!=count)return false;
 if(tts_stopped)drain_since=esp_timer_get_time();
 xSemaphoreTake(guard,portMAX_DELAY);snapshot.rx_frames++;xSemaphoreGive(guard);return true;
}
static bool pump(void) {
 packet_t *p;unsigned processed=0;
 while(processed++<8&&xQueueReceive(incoming,&p,0)==pdTRUE){
  bool ok;
  if(p->binary){const uint8_t *data=p->data;size_t n=p->size;ok=channel_ready&&xz_ws_audio(config.version,&data,&n)&&decode_audio(data,n);}
  else ok=handle_json(p);
  xQueueSend(free_packets,&p,0);last_receive=esp_timer_get_time();if(!ok)return false;
  if(interrupted())return true;
 }
 if(udp_fd>=0){
  uint8_t data[XZ_PACKET_MAX+17],plain[XZ_PACKET_MAX],nonce[16];
  for(unsigned i=0;i<8;i++){
   int n=recv(udp_fd,data,sizeof(data),0);if(n<=0)break;uint32_t seq;
   if(!xz_udp_header(data,n,remote_sequence,&seq)||memcmp(data+4,udp_nonce+4,4))continue;
   memcpy(nonce,data,16);if(!xz_crypt(data+16,n-16,nonce,plain))return false;
   remote_sequence=seq;last_receive=esp_timer_get_time();if(!decode_audio(plain,n-16))return false;
   if(interrupted())break;
  }
 }
 return !atomic_load(&net_fault);
}
static bool open_channel(void) {
 bool resume_auto=atomic_load(&continuous);disconnect();atomic_store(&continuous,resume_auto);
 if(*config.ws_url){
  esp_websocket_client_config_t c={.uri=config.ws_url,.disable_auto_reconnect=true,.crt_bundle_attach=esp_crt_bundle_attach,.task_stack=8192,.buffer_size=2048,.network_timeout_ms=5000,.ping_interval_sec=20,.pingpong_timeout_sec=15};
  ws=esp_websocket_client_init(&c);if(!ws)return false;
  char auth[1040];snprintf(auth,sizeof(auth),"%s%s",strchr(config.token,' ')?"":"Bearer ",config.token);
  char version[8];snprintf(version,sizeof(version),"%u",config.version);
  if(esp_websocket_client_append_header(ws,"Authorization",auth)!=ESP_OK||esp_websocket_client_append_header(ws,"Device-Id",mac)!=ESP_OK||esp_websocket_client_append_header(ws,"Client-Id",uuid)!=ESP_OK||esp_websocket_client_append_header(ws,"Protocol-Version",version)!=ESP_OK)return false;
  esp_websocket_register_events(ws,WEBSOCKET_EVENT_ANY,ws_event,NULL);if(esp_websocket_client_start(ws)!=ESP_OK)return false;
 }else{
  char endpoint[208];if(strstr(config.endpoint,"://")||strchr(config.endpoint,'/')||strchr(config.endpoint,'@'))return false;
  const char *colon=strchr(config.endpoint,':');if(colon&&strcmp(colon,":8883"))return false;
  snprintf(endpoint,sizeof(endpoint),"mqtts://%s%s",config.endpoint,colon?"":":8883");
  esp_mqtt_client_config_t c={.broker.address.uri=endpoint,.broker.verification.crt_bundle_attach=esp_crt_bundle_attach,.credentials.client_id=config.client_id,.credentials.username=config.username,.credentials.authentication.password=config.password,.session.keepalive=60,.network.disable_auto_reconnect=true,.network.timeout_ms=5000,.buffer.size=4096,.task.stack_size=8192};
  mqtt=esp_mqtt_client_init(&c);if(!mqtt)return false;
  esp_mqtt_client_register_event(mqtt,ESP_EVENT_ANY_ID,mqtt_event,NULL);if(esp_mqtt_client_start(mqtt)!=ESP_OK)return false;
 }
 int64_t deadline=esp_timer_get_time()+12000000;
 while(!atomic_load(&net_connected)&&!atomic_load(&net_fault)&&!interrupted()&&esp_timer_get_time()<deadline)vTaskDelay(pdMS_TO_TICKS(20));
 if(!atomic_load(&net_connected)||interrupted()||atomic_load(&net_fault))return false;
 cJSON *o=cJSON_CreateObject();if(!o)return false;
 cJSON_AddStringToObject(o,"type","hello");cJSON_AddNumberToObject(o,"version",ws?config.version:3);cJSON_AddStringToObject(o,"transport",ws?"websocket":"udp");
 cJSON *f=cJSON_AddObjectToObject(o,"features");cJSON_AddBoolToObject(f,"mcp",true);
 cJSON *a=cJSON_AddObjectToObject(o,"audio_params");cJSON_AddStringToObject(a,"format","opus");cJSON_AddNumberToObject(a,"sample_rate",16000);cJSON_AddNumberToObject(a,"channels",1);cJSON_AddNumberToObject(a,"frame_duration",60);
 if(!send_json(o))return false;
 deadline=esp_timer_get_time()+10000000;
 while(!channel_ready&&!interrupted()&&esp_timer_get_time()<deadline){if(!pump())return false;vTaskDelay(pdMS_TO_TICKS(10));}
 last_receive=esp_timer_get_time();return channel_ready&&!interrupted();
}
static bool codecs_open(void) {
 if(encoder){esp_opus_enc_close(encoder);}
 if(decoder){esp_opus_dec_close(decoder);}
 encoder=decoder=NULL;
 esp_opus_enc_config_t enc={.sample_rate=16000,.channel=1,.bits_per_sample=16,.bitrate=24000,.frame_duration=ESP_OPUS_ENC_FRAME_DURATION_60_MS,.application_mode=ESP_OPUS_ENC_APPLICATION_VOIP,.complexity=0,.enable_fec=false,.enable_dtx=false,.enable_vbr=true};
 esp_opus_dec_cfg_t dec={.sample_rate=48000,.channel=1,.frame_duration=ESP_OPUS_DEC_FRAME_DURATION_60_MS,.self_delimited=false};
 if(esp_opus_enc_open(&enc,sizeof(enc),&encoder)!=ESP_AUDIO_ERR_OK||esp_opus_dec_open(&dec,sizeof(dec),&decoder)!=ESP_AUDIO_ERR_OK)return false;
 if(esp_opus_enc_get_frame_size(encoder,&encoder_bytes,&encoder_capacity)!=ESP_AUDIO_ERR_OK)return false;
 return encoder_bytes==1920&&encoder_capacity>0&&encoder_capacity<=XZ_PACKET_MAX;
}
static bool encode_frame(void) {
 esp_audio_enc_in_frame_t in={.buffer=(uint8_t*)mono_pcm,.len=1920};
 esp_audio_enc_out_frame_t out={.buffer=encoded_pcm,.len=encoder_capacity};
 bool first=snapshot.tx_frames==0;
 if(first)ESP_LOGI(TAG,"First Opus encode: stack=%u heap_ok=%u",(unsigned)uxTaskGetStackHighWaterMark(NULL),heap_caps_check_integrity_all(true));
 if(esp_opus_enc_process(encoder,&in,&out)!=ESP_AUDIO_ERR_OK||!out.encoded_bytes||out.encoded_bytes>encoder_capacity)return false;
 if(first)ESP_LOGI(TAG,"First Opus encoded: bytes=%u stack=%u heap_ok=%u",(unsigned)out.encoded_bytes,(unsigned)uxTaskGetStackHighWaterMark(NULL),heap_caps_check_integrity_all(true));
 if(!send_audio(encoded_pcm,out.encoded_bytes))return false;
 xSemaphoreTake(guard,portMAX_DELAY);snapshot.tx_frames++;xSemaphoreGive(guard);mono_used=0;return true;
}
static bool capture_samples(size_t n) {
 int16_t output[320];size_t got=xz_decimate(&decimator,input_pcm,n,output,320);
 for(size_t i=0;i<got;i++){mono_pcm[mono_used++]=output[i];if(mono_used==960&&!encode_frame())return false;}
 return true;
}
static bool capture(void) {
 state(XZ_CONNECTING,"正在打开麦克风，请稍候");
 if(!codecs_open())return false;
 /* This UAC headset can acknowledge start yet deliver no isochronous PCM.
  * Confirm input before announcing listening; permit one bounded restart. */
 size_t first_samples=0;
 for(unsigned attempt=0;attempt<2&&!first_samples;attempt++){
  if(!voice_audio_mode(VOICE_AUDIO_CAPTURE))return false;
  int64_t ready_deadline=esp_timer_get_time()+2200000;
  while(atomic_load(&held)&&!interrupted()&&voice_audio_ok()&&esp_timer_get_time()<ready_deadline){
   first_samples=voice_audio_read(input_pcm,960,20);
   if(first_samples)break;
   if(!pump())return false;
  }
  if(interrupted())return true;
  if(!atomic_load(&held)){
   if(!voice_audio_mode(VOICE_AUDIO_IDLE))return false;
   state(XZ_READY,"请按住，看到正在听后再说话");return true;
  }
  if(!first_samples){
   ESP_LOGW(TAG,"USB microphone startup produced no PCM, attempt=%u",attempt+1);
   if(!voice_audio_mode(VOICE_AUDIO_IDLE))return false;
  }
 }
 if(!first_samples||!xz_listen(true))return false;
 mono_used=0;xz_decimator_reset(&decimator);response_started=esp_timer_get_time();tts_started=tts_stopped=false;
 xSemaphoreTake(guard,portMAX_DELAY);snapshot.user[0]=snapshot.answer[0]=snapshot.emotion[0]=0;snapshot.seconds=0;xSemaphoreGive(guard);
 state(XZ_LISTENING,atomic_load(&continuous)?"正在听 · 说完稍停，自动回答":"正在听 · 松开发送，最长30秒");int64_t start=esp_timer_get_time(),last_pcm=start;
 if(!capture_samples(first_samples))return false;
 while(atomic_load(&held)&&!interrupted()&&esp_timer_get_time()-start<(atomic_load(&continuous)?60000000:30000000)){
  size_t n=voice_audio_read(input_pcm,960,20);
  if(n){last_pcm=esp_timer_get_time();if(!capture_samples(n))return false;}
  if(!voice_audio_ok()||esp_timer_get_time()-last_pcm>2000000||!pump())return false;
  xSemaphoreTake(guard,portMAX_DELAY);snapshot.seconds=(esp_timer_get_time()-start)/1000000;xSemaphoreGive(guard);
 }
 atomic_store(&held,false);if(interrupted())return true;
 if(atomic_load(&continuous)){
  if(tts_started)return true; /* Server auto endpoint already switched USB to playback. */
  xiaozhi_cancel();return true; /* No reply within 60 s: stop instead of listening indefinitely. */
 }
 if(!voice_audio_mode(VOICE_AUDIO_IDLE))return false;
 /* Capture is now stopped; drain its bounded ring before the final Opus frame. */
 for(unsigned i=0;i<70;i++){size_t n=voice_audio_read(input_pcm,960,0);if(!n)break;if(!capture_samples(n))return false;}
 if(esp_timer_get_time()-start<100000){send_json(message("abort"));response_started=0;state(XZ_READY,"录音太短，请按住后再说话");return true;}
 /* Only an utterance with captured PCM is submitted; final short frame is padded. */
 if(mono_used){memset(mono_pcm+mono_used,0,(960-mono_used)*2);if(!encode_frame())return false;}
 if(!xz_listen(false)||!voice_audio_mode(VOICE_AUDIO_PLAY))return false;
 response_started=esp_timer_get_time();state(XZ_THINKING,"正在思考 · 点击停止可取消");return true;
}
static void release_audio(void) {voice_audio_mode(VOICE_AUDIO_OFF);response_started=0;tts_started=tts_stopped=false;}
static bool prepare(void) {
 wifi_status_t w;wifi_service_status(&w);if(w.state!=NET_CONNECTED){state(XZ_ERROR,"请先连接 Wi-Fi");return false;}
 if(time(NULL)<1704067200){state(XZ_ERROR,"等待网络校时后，再点连接");return false;}
 music_snapshot_t music;music_snapshot(&music);microphone_snapshot_t mic;microphone_snapshot(&mic);
 if(!music.host||!music.connected||!mic.connected){state(XZ_ERROR,"请切换音频模式并接好 USB 耳机");return false;}
 if(!voice_audio_mode(VOICE_AUDIO_IDLE)){state(XZ_ERROR,"USB 音频忙，请稍后重试");return false;}
 state(XZ_CONNECTING,"正在连接小智服务");int result=fetch_config();
 if(result==2){
  atomic_store(&continuous,false);atomic_store(&held,false);
  state(XZ_ACTIVATING,"打开 xiaozhi.me，添加设备并输入绑定码");
  int64_t deadline=esp_timer_get_time()+180000000;
  while(result==2&&!interrupted()&&esp_timer_get_time()<deadline){
   for(int i=0;i<50&&!interrupted();i++){vTaskDelay(pdMS_TO_TICKS(100));}
   if(interrupted())break;
   if(config.activation_challenge){
    char response[2048];int code=http_post(XZ_OTA_URL "activate","{}",response,sizeof(response));memset(response,0,sizeof(response));
    if(code==202)continue; /* Keep the original code/challenge until binding completes. */
    if(code!=200)continue; /* Bound retry window; no paid service is enabled. */
   }
   result=fetch_config();
  }
 }
 if(interrupted())return false;
 if(result!=1){state(XZ_ERROR,"未取得有效连接配置，请检查账号绑定");return false;}
 state(XZ_CONNECTING,"正在建立语音通道");if(!open_channel()){state(XZ_ERROR,"语音连接失败，请检查网络后重试");return false;}
 state(XZ_READY,"已连接 · 点击开始连续聊天");return true;
}
static void worker(void *arg) {
 for(;;){
  if(atomic_load(&stop_requested)) {
   disconnect();
   if(!voice_audio_mode(VOICE_AUDIO_OFF)) { vTaskDelay(pdMS_TO_TICKS(20)); continue; }
   if(encoder) { esp_opus_enc_close(encoder); }
   if(decoder) { esp_opus_dec_close(decoder); }
   encoder=decoder=NULL;
   for(;;) vTaskSuspend(NULL);
  }
  unsigned leaving_generation=atomic_load(&leave_generation);
  if(atomic_exchange(&leaving,false)||atomic_exchange(&cancelled,false)){
   if(channel_ready){send_json(message("abort"));}
   disconnect();release_audio();atomic_store(&held,false);atomic_store(&requested,false);state(XZ_OFFLINE,"已停止 · 点连接后可继续");
   atomic_store(&leave_done,leaving_generation);
  }
  if(atomic_exchange(&requested,false)&&!channel_ready){
   if(!prepare()){disconnect();release_audio();atomic_store(&held,false);}
  }
  if(channel_ready){
   if(atomic_load(&held)){
    if(response_started){atomic_store(&cancelled,true);continue;}
    if(!capture()){state(XZ_ERROR,"录音或网络中断，请重连后重说");disconnect();release_audio();atomic_store(&held,false);}
   }
   if(channel_ready&&!pump()){state(XZ_ERROR,"连接已中断，请点连接重试");disconnect();release_audio();}
   if(channel_ready&&response_started){
    int64_t now=esp_timer_get_time();
    if(!voice_audio_ok()||now-response_started>120000000||(!tts_started&&now-response_started>30000000)||now-last_receive>30000000){state(XZ_ERROR,"等待超时或 USB 中断，请重连后重说");disconnect();release_audio();}
    else if(tts_stopped&&!voice_audio_pending()&&now-drain_since>500000){
     if(!voice_audio_mode(VOICE_AUDIO_IDLE)){state(XZ_ERROR,"USB 音频停止失败");disconnect();release_audio();}
     else{response_started=0;tts_started=tts_stopped=false;state(XZ_READY,atomic_load(&continuous)?"回答结束 · 准备继续听":"回答结束 · 点击开始聊天");if(atomic_load(&continuous))atomic_store(&held,true);}
    }
   }else if(channel_ready&&esp_timer_get_time()-last_receive>60000000){disconnect();release_audio();state(XZ_OFFLINE,"会话已休息 · 按住可重新连接");}
  }
  vTaskDelay(pdMS_TO_TICKS(10));
 }
}
static void xiaozhi_free_resources(void) {
 free(pool);pool=NULL; free(input_pcm);input_pcm=NULL;free(mono_pcm);mono_pcm=NULL;
 free(play_pcm);play_pcm=NULL;free(encoded_pcm);encoded_pcm=NULL;free(chat_stack);chat_stack=NULL;
 if(incoming) { vQueueDelete(incoming); }
 if(free_packets) { vQueueDelete(free_packets); }
 incoming=free_packets=NULL;
 if(guard) { vSemaphoreDelete(guard); }
 guard=NULL;memset(&config,0,sizeof(config));snapshot=(xiaozhi_snapshot_t){0};
}
esp_err_t xiaozhi_deinit(void) {
 if(chat_task) {
  atomic_store(&stop_requested,true); xiaozhi_leave();
  esp_err_t e=service_task_join(&chat_task,30000); if(e!=ESP_OK)return e;
 }
 xiaozhi_free_resources();return ESP_OK;
}
esp_err_t xiaozhi_init(void) {
 esp_err_t e=ESP_ERR_NO_MEM;
 if(chat_task) return atomic_load(&stop_requested)?ESP_ERR_INVALID_STATE:ESP_OK;
 atomic_store(&stop_requested,false); atomic_store(&leaving,false); atomic_store(&cancelled,false);
 atomic_store(&continuous,false); atomic_store(&held,false); atomic_store(&requested,false);
 snapshot=(xiaozhi_snapshot_t){0};
 guard=xSemaphoreCreateMutex();if(!guard)goto failed;
 incoming=xQueueCreate(PACKETS,sizeof(packet_t*));free_packets=xQueueCreate(PACKETS,sizeof(packet_t*));
 pool=heap_caps_calloc(PACKETS,sizeof(packet_t),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 input_pcm=heap_caps_malloc(1920,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);mono_pcm=heap_caps_malloc(1920,MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
 play_pcm=heap_caps_malloc(11520,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);encoded_pcm=heap_caps_malloc(XZ_PACKET_MAX,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!incoming||!free_packets||!pool||!input_pcm||!mono_pcm||!play_pcm||!encoded_pcm||!voice_audio_init())goto failed;
 for(unsigned i=0;i<PACKETS;i++){packet_t *p=pool+i;xQueueSend(free_packets,&p,0);}
 uint8_t address[6];e=esp_read_mac(address,ESP_MAC_WIFI_STA);if(e!=ESP_OK)goto failed;
 snprintf(mac,sizeof(mac),"%02x:%02x:%02x:%02x:%02x:%02x",address[0],address[1],address[2],address[3],address[4],address[5]);xz_copy(snapshot.device_id,sizeof(snapshot.device_id),mac);
 nvs_handle_t n;e=nvs_open("xiaozhi",NVS_READWRITE,&n);if(e!=ESP_OK)goto failed;
 size_t len=sizeof(uuid);if(nvs_get_str(n,"uuid",uuid,&len)!=ESP_OK||strlen(uuid)!=36){
  uint8_t bytes[16];esp_fill_random(bytes,sizeof(bytes));bytes[6]=(bytes[6]&15)|64;bytes[8]=(bytes[8]&63)|128;
  snprintf(uuid,sizeof(uuid),"%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",bytes[0],bytes[1],bytes[2],bytes[3],bytes[4],bytes[5],bytes[6],bytes[7],bytes[8],bytes[9],bytes[10],bytes[11],bytes[12],bytes[13],bytes[14],bytes[15]);
  e=nvs_set_str(n,"uuid",uuid);if(e==ESP_OK)e=nvs_commit(n);
 }nvs_close(n);if(e!=ESP_OK)goto failed;
 /* Opus has large transient stack use. Keep its stack in PSRAM instead of
  * requiring one large contiguous internal block after USB/Wi-Fi startup.
  * This worker does not write flash; external stacks are enabled in sdkconfig. */
 chat_stack=heap_caps_malloc(49152,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!chat_stack){e=ESP_ERR_NO_MEM;goto failed;}
 chat_task=xTaskCreateStaticPinnedToCore(worker,"xiaozhi",49152,NULL,3,chat_stack,&chat_task_control,1);
 if(!chat_task){e=ESP_ERR_NO_MEM;goto failed;}
 snapshot.available=true;state(XZ_OFFLINE,"点连接，首次使用需绑定小智账号");return ESP_OK;
failed:
 xiaozhi_free_resources(); return e;
}
