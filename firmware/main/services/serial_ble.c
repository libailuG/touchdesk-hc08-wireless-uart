/* HC-08 central transport. Profile confirmed on F8:2E:0C:ED:E7:E8,
 * V3.3: FFE0 / FFE1 notify + write-without-response. Handles are discovered.
 * One host, exclusive with ble_mouse via app_services. No AT configuration. */
#include "serial_ble.h"
#include "service_task.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "host/ble_uuid.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <string.h>
static const char *TAG="hc08";
static const uint8_t peer_mac[6]={0xe8,0xe7,0xed,0x0c,0x2e,0xf8};
static const ble_uuid16_t svc_uuid=BLE_UUID16_INIT(0xffe0),chr_uuid=BLE_UUID16_INIT(0xffe1);
typedef enum {WAIT_SYNC,SCANNING,CONNECTING,DISCOVERING,READY,RETRY,STOPPED} link_state_t;
typedef struct {uint32_t epoch;uint16_t n;uint8_t bytes[256];} rx_item_t;
typedef struct {uint32_t epoch;uint8_t bytes[48];} tx_item_t;
typedef struct {serial_parser_t parser;serial_uart_snapshot_t snap;serial_history_t history;} session_t;
static session_t *session;
static SemaphoreHandle_t data_lock;
static QueueHandle_t rxq,txq;
static TaskHandle_t host_task_handle,worker_handle;
static struct ble_npl_callout timer;
static bool port_initialized,timer_initialized,host_stopped;
static atomic_bool stopping,active;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static link_state_t state;
static uint16_t conn=BLE_HS_CONN_HANDLE_NONE,svc_end,value_handle,chr_end,cccd_handle;
static uint8_t own_addr;
static uint32_t epoch,overflows,send_errors,reconnects;
static unsigned backoff;
static int64_t retry_at;
static void set_state(link_state_t x){portENTER_CRITICAL(&mux);state=x;portEXIT_CRITICAL(&mux);}
static void fail_link(int rc);
static int gap_event(struct ble_gap_event *e,void *arg);
static bool current(uint16_t c){return !atomic_load(&stopping)&&c==conn;}
static void schedule_retry(void){
 portENTER_CRITICAL(&mux);state=RETRY;retry_at=esp_timer_get_time()+(int64_t)(1u<<backoff)*1000000;if(backoff<4)backoff++;portEXIT_CRITICAL(&mux);
}
static void fail_link(int rc){
 ESP_LOGW(TAG,"link stage failed rc=%d",rc);
 if(conn!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);
 else schedule_retry();
}
static int subscribed(uint16_t c,const struct ble_gatt_error *err,struct ble_gatt_attr *attr,void *arg){
 if(!current(c))return 0;
 if(err->status){fail_link(err->status);return 0;}
 portENTER_CRITICAL(&mux);state=READY;backoff=0;portEXIT_CRITICAL(&mux);
 ESP_LOGI(TAG,"ready FFE0/FFE1 value=%u cccd=%u, payload=20",value_handle,cccd_handle);return 0;
}
static int descriptor(uint16_t c,const struct ble_gatt_error *err,uint16_t chr,const struct ble_gatt_dsc *d,void *arg){
 if(!current(c))return 0;
 if(err->status==0){if(ble_uuid_u16(&d->uuid.u)==0x2902)cccd_handle=d->handle;return 0;}
 if(err->status!=BLE_HS_EDONE||!cccd_handle){fail_link(err->status);return 0;}
 uint8_t enable[2]={1,0};int rc=ble_gattc_write_flat(c,cccd_handle,enable,2,subscribed,NULL);if(rc)fail_link(rc);return 0;
}
static int characteristic(uint16_t c,const struct ble_gatt_error *err,const struct ble_gatt_chr *ch,void *arg){
 if(!current(c))return 0;
 if(!err->status){
  if(value_handle&&ch->def_handle>value_handle&&chr_end==svc_end)chr_end=ch->def_handle-1;
  if(ble_uuid_cmp(&ch->uuid.u,&chr_uuid.u)==0){
   if((ch->properties&(BLE_GATT_CHR_PROP_NOTIFY|BLE_GATT_CHR_PROP_WRITE_NO_RSP))!=(BLE_GATT_CHR_PROP_NOTIFY|BLE_GATT_CHR_PROP_WRITE_NO_RSP)){fail_link(BLE_HS_ENOTSUP);return 0;}
   value_handle=ch->val_handle;chr_end=svc_end;
  }return 0;
 }
 if(err->status!=BLE_HS_EDONE||!value_handle||chr_end<=value_handle){fail_link(err->status);return 0;}
 int rc=ble_gattc_disc_all_dscs(c,value_handle,chr_end,descriptor,NULL);if(rc)fail_link(rc);return 0;
}
static uint16_t svc_start;
static int service(uint16_t c,const struct ble_gatt_error *err,const struct ble_gatt_svc *s,void *arg){
 if(!current(c))return 0;
 if(!err->status){svc_start=s->start_handle;svc_end=s->end_handle;return 0;}
 if(err->status!=BLE_HS_EDONE||!svc_start){fail_link(err->status);return 0;}
 int rc=ble_gattc_disc_all_chrs(c,svc_start,svc_end,characteristic,NULL);if(rc)fail_link(rc);return 0;
}
static int gap_event(struct ble_gap_event *e,void *arg){
 if(atomic_load(&stopping))return 0;
 switch(e->type){
 case BLE_GAP_EVENT_DISC:
  if(memcmp(e->disc.addr.val,peer_mac,6)==0){
   set_state(CONNECTING);
   if(ble_gap_disc_cancel()!=0){schedule_retry();break;}
   int rc=ble_gap_connect(own_addr,&e->disc.addr,10000,NULL,gap_event,NULL);if(rc)fail_link(rc);
  }break;
 case BLE_GAP_EVENT_DISC_COMPLETE:
  /* A cancelled scan is immediately followed by connect; natural expiry retries. */
  portENTER_CRITICAL(&mux);bool scanning=state==SCANNING;portEXIT_CRITICAL(&mux);
  if(scanning)schedule_retry();
  break;
 case BLE_GAP_EVENT_CONNECT:
  if(e->connect.status){schedule_retry();break;}
  portENTER_CRITICAL(&mux);conn=e->connect.conn_handle;epoch++;state=DISCOVERING;portEXIT_CRITICAL(&mux);
  svc_start=svc_end=value_handle=chr_end=cccd_handle=0;
  {int rc=ble_gattc_disc_svc_by_uuid(conn,&svc_uuid.u,service,NULL);if(rc)fail_link(rc);}break;
 case BLE_GAP_EVENT_DISCONNECT:
  portENTER_CRITICAL(&mux);conn=BLE_HS_CONN_HANDLE_NONE;epoch++;reconnects++;portEXIT_CRITICAL(&mux);
  schedule_retry();break;
 case BLE_GAP_EVENT_NOTIFY_RX:
  if(e->notify_rx.attr_handle==value_handle&&current(e->notify_rx.conn_handle)){
   rx_item_t item;item.n=OS_MBUF_PKTLEN(e->notify_rx.om);
   portENTER_CRITICAL(&mux);item.epoch=epoch;portEXIT_CRITICAL(&mux);
   if(item.n>sizeof(item.bytes)||os_mbuf_copydata(e->notify_rx.om,0,item.n,item.bytes)!=0||xQueueSend(rxq,&item,0)!=pdTRUE){
    portENTER_CRITICAL(&mux);overflows++;epoch++;portEXIT_CRITICAL(&mux);
   }
  }break;
 default:break;
 }return 0;
}
static void tick(struct ble_npl_event *e){
 if(atomic_load(&stopping))return;
 portENTER_CRITICAL(&mux);bool retry=state==RETRY&&esp_timer_get_time()>=retry_at;portEXIT_CRITICAL(&mux);
 if(retry){
  struct ble_gap_disc_params p={.passive=0,.filter_duplicates=1,.itvl=0x60,.window=0x30};
  set_state(SCANNING);int rc=ble_gap_disc(own_addr,5000,&p,gap_event,NULL);if(rc)fail_link(rc);
 }
 ble_npl_callout_reset(&timer,ble_npl_time_ms_to_ticks32(200));
}
static void synced(void){
 if(atomic_load(&stopping))return;
 int rc=ble_hs_util_ensure_addr(0);if(!rc)rc=ble_hs_id_infer_auto(0,&own_addr);
 if(rc){fail_link(rc);return;}
 backoff=0;schedule_retry();ble_npl_callout_reset(&timer,1);
}
static void reset_host(int reason){
 portENTER_CRITICAL(&mux);conn=BLE_HS_CONN_HANDLE_NONE;epoch++;state=WAIT_SYNC;portEXIT_CRITICAL(&mux);
 ESP_LOGW(TAG,"host reset=%d",reason);
}
static void host_run(void *arg){nimble_port_run();for(;;)vTaskSuspend(NULL);}
static void received(const serial_frame_t *f,void *arg){
 session->snap.last=*f;session->snap.has_frame=true;session->snap.rx++;
 serial_history_push_timed(&session->history,f,esp_timer_get_time());
}
static void worker(void *arg){
 uint32_t seen=0;rx_item_t item;tx_item_t tx;
 while(!atomic_load(&stopping)){
  portENTER_CRITICAL(&mux);uint32_t gen=epoch;portEXIT_CRITICAL(&mux);
  if(gen!=seen){xSemaphoreTake(data_lock,portMAX_DELAY);session->parser.used=0;xSemaphoreGive(data_lock);seen=gen;}
  for(unsigned batch=0;batch<32&&xQueueReceive(rxq,&item,0)==pdTRUE;batch++){
   portENTER_CRITICAL(&mux);gen=epoch;portEXIT_CRITICAL(&mux);
   if(item.epoch!=gen)continue;
   xSemaphoreTake(data_lock,portMAX_DELAY);
   if(seen!=gen){session->parser.used=0;seen=gen;}
   serial_parser_feed(&session->parser,item.bytes,item.n,received,NULL);
   session->snap.discarded=session->parser.discarded;session->snap.bad_tail=session->parser.bad_tail;
   xSemaphoreGive(data_lock);
  }
  if(xQueueReceive(txq,&tx,pdMS_TO_TICKS(5))==pdTRUE){
   portENTER_CRITICAL(&mux);bool stale=epoch!=tx.epoch;portEXIT_CRITICAL(&mux);
   if(stale){portENTER_CRITICAL(&mux);send_errors++;portEXIT_CRITICAL(&mux);continue;}
   bool ok=true;
   for(unsigned off=0;off<48;off+=20){
    portENTER_CRITICAL(&mux);uint16_t c=conn;bool usable=state==READY&&epoch==tx.epoch;portEXIT_CRITICAL(&mux);
    if(atomic_load(&stopping)||!usable){ok=false;break;}
    unsigned n=48-off;if(n>20)n=20;
    int rc=ble_gattc_write_no_rsp_flat(c,value_handle,tx.bytes+off,n);
    if(rc){ok=false;break;}
    vTaskDelay(pdMS_TO_TICKS(10));
   }
   xSemaphoreTake(data_lock,portMAX_DELAY);if(ok)session->snap.tx++;xSemaphoreGive(data_lock);
   if(!ok){portENTER_CRITICAL(&mux);send_errors++;portEXIT_CRITICAL(&mux);
    /* A partial frame must not be concatenated with a later command. */
    portENTER_CRITICAL(&mux);uint16_t c=epoch==tx.epoch?conn:BLE_HS_CONN_HANDLE_NONE;portEXIT_CRITICAL(&mux);
    if(c!=BLE_HS_CONN_HANDLE_NONE)ble_gap_terminate(c,BLE_ERR_REM_USER_CONN_TERM);
   }
  }
 }
 for(;;)vTaskSuspend(NULL);
}
esp_err_t serial_ble_start(void){
 if(atomic_load(&active))return ESP_OK;
 if(port_initialized||session)return ESP_ERR_INVALID_STATE;
 atomic_store(&stopping,false);host_stopped=false;epoch=overflows=send_errors=reconnects=0;backoff=0;conn=BLE_HS_CONN_HANDLE_NONE;state=WAIT_SYNC;
 session=heap_caps_calloc(1,sizeof(*session),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 data_lock=xSemaphoreCreateMutex();rxq=xQueueCreate(32,sizeof(rx_item_t));txq=xQueueCreate(8,sizeof(tx_item_t));
 esp_err_t err=ESP_ERR_NO_MEM;
 if(!session||!data_lock||!rxq||!txq)goto failed;
 err=nvs_flash_init();if(err!=ESP_OK)goto failed;
 err=nimble_port_init();if(err!=ESP_OK)goto failed;port_initialized=true;
 ble_hs_cfg.sync_cb=synced;ble_hs_cfg.reset_cb=reset_host;
 ble_hs_cfg.store_status_cb=NULL;ble_hs_cfg.sm_bonding=0;ble_hs_cfg.sm_mitm=0;
 ble_svc_gap_init();ble_svc_gatt_init();ble_svc_gap_device_name_set("TouchDesk UART");
 if(ble_npl_callout_init(&timer,nimble_port_get_dflt_eventq(),tick,NULL)!=0){err=ESP_ERR_NO_MEM;goto failed;}timer_initialized=true;
 if(xTaskCreatePinnedToCore(host_run,"hc08_host",CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,NULL,configMAX_PRIORITIES-4,&host_task_handle,CONFIG_BT_NIMBLE_PINNED_TO_CORE)!=pdPASS){err=ESP_ERR_NO_MEM;goto failed;}
 if(xTaskCreate(worker,"hc08_stream",4096,NULL,5,&worker_handle)!=pdPASS){err=ESP_ERR_NO_MEM;goto failed;}
 atomic_store(&active,true);return ESP_OK;
failed:
 {esp_err_t cleanup=serial_ble_stop();if(cleanup!=ESP_OK)return cleanup;}return err;
}
esp_err_t serial_ble_stop(void){
 atomic_store(&stopping,true);
 if(timer_initialized)ble_npl_callout_stop(&timer);
 /* Join the writer before stopping host: no API call after NimBLE teardown. */
 esp_err_t e=service_task_join(&worker_handle,5000);if(e!=ESP_OK)return e;
 if(host_task_handle&&!host_stopped){int rc=nimble_port_stop();if(rc)return ESP_FAIL;host_stopped=true;}
 e=service_task_join(&host_task_handle,5000);if(e!=ESP_OK)return e;
 if(timer_initialized){ble_npl_callout_deinit(&timer);timer_initialized=false;}
 if(port_initialized){e=nimble_port_deinit();if(e!=ESP_OK)return e;port_initialized=false;}
 if(txq){vQueueDelete(txq);txq=NULL;}if(rxq){vQueueDelete(rxq);rxq=NULL;}
 if(data_lock){vSemaphoreDelete(data_lock);data_lock=NULL;}heap_caps_free(session);session=NULL;
 atomic_store(&active,false);set_state(STOPPED);return ESP_OK;
}
bool serial_ble_active(void){return atomic_load(&active);}
bool serial_ble_send(const serial_frame_t *f){
 if(!serial_ble_active()||atomic_load(&stopping))return false;
 tx_item_t tx;portENTER_CRITICAL(&mux);bool ready=state==READY;tx.epoch=epoch;portEXIT_CRITICAL(&mux);
 if(!ready)return false;
 serial_frame_encode(f,tx.bytes);bool ok=xQueueSend(txq,&tx,0)==pdTRUE;
 if(!ok){portENTER_CRITICAL(&mux);send_errors++;portEXIT_CRITICAL(&mux);}return ok;
}
void serial_ble_snapshot(serial_uart_snapshot_t *out){
 memset(out,0,sizeof(*out));if(!serial_ble_active())return;
 xSemaphoreTake(data_lock,portMAX_DELAY);*out=session->snap;xSemaphoreGive(data_lock);
 portENTER_CRITICAL(&mux);out->buffer_errors=overflows;out->line_errors=send_errors;out->uart_errors=overflows+send_errors;out->fifo_errors=reconnects;portEXIT_CRITICAL(&mux);
}
size_t serial_ble_history(uint32_t *cursor,serial_sample_t *out,size_t cap,uint32_t *lost){
 if(!serial_ble_active())return 0;
 xSemaphoreTake(data_lock,portMAX_DELAY);size_t n=serial_history_read(&session->history,cursor,out,cap,lost);xSemaphoreGive(data_lock);return n;
}
const char *serial_ble_status(void){
 portENTER_CRITICAL(&mux);link_state_t s=state;portEXIT_CRITICAL(&mux);
 switch(s){case SCANNING:return "BLE scanning";case CONNECTING:return "BLE connecting";case DISCOVERING:return "BLE discovering";case READY:return "BLE ready";case RETRY:return "BLE reconnecting";default:return "BLE starting";}
}
