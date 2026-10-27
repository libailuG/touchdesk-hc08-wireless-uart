/* Minimal HOGP peripheral using ESP-IDF's NimBLE public API. */
#include "ble_mouse.h"
#include "service_task.h"
#include "mouse_flow.h"
#include "keyboard_flow.h"
#include <string.h>
#include <stdatomic.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/ble_sm.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static bool initialized,port_initialized,host_stopped,timer_initialized;
static atomic_bool stopping_ble;
static TaskHandle_t ble_task;
static const char *TAG="ble_mouse";
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static mouse_flow_t flow;
static keyboard_flow_t keys,media;
static SemaphoreHandle_t touch_lock;
static uint16_t media_handle;
static bool media_notify;
static ble_mouse_snapshot_t status;
static uint16_t conn=BLE_HS_CONN_HANDLE_NONE,report_handle,boot_handle;
static uint16_t key_handle,key_boot_handle;
static bool key_notify,key_boot_notify;
static uint8_t last_keys[8],leds;
static unsigned key_failures;
static uint8_t address_type,protocol=1,last_buttons;
static bool report_notify,boot_notify,suspended,synced;
static unsigned consecutive_failures;
static int64_t pairing_until;
static atomic_uint commands;
static struct ble_npl_callout timer;
static int gap_event(struct ble_gap_event *event,void *arg);

static const uint8_t report_map[]={
    0x05,0x01,0x09,0x02,0xa1,0x01,0x85,0x01,0x09,0x01,0xa1,0x00,
    0x05,0x09,0x19,0x01,0x29,0x03,0x15,0x00,0x25,0x01,0x95,0x03,
    0x75,0x01,0x81,0x02,0x95,0x01,0x75,0x05,0x81,0x01,0x05,0x01,
    0x09,0x30,0x09,0x31,0x16,0x01,0x80,0x26,0xff,0x7f,0x75,0x10,0x95,0x02,0x81,0x06,
    0x09,0x38,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x01,0x81,0x06,0xc0,0xc0,
    0x05,0x01,0x09,0x06,0xa1,0x01,0x85,0x02,
    0x05,0x07,0x19,0xe0,0x29,0xe7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,
    0x95,0x01,0x75,0x08,0x81,0x01,
    0x05,0x08,0x19,0x01,0x29,0x05,0x95,0x05,0x75,0x01,0x91,0x02,
    0x95,0x01,0x75,0x03,0x91,0x01,
    0x05,0x07,0x19,0x00,0x29,0xdd,0x15,0x00,0x26,0xdd,0x00,0x75,0x08,0x95,0x06,0x81,0x00,0xc0,
    0x05,0x0c,0x09,0x01,0xa1,0x01,0x85,0x03,0x15,0x00,0x26,0xff,0x03,0x19,0x00,0x2a,0xff,0x03,0x75,0x10,0x95,0x01,0x81,0x00,0xc0
};
enum { VALUE_INFO,VALUE_MAP,VALUE_PROTOCOL,VALUE_CONTROL,VALUE_INPUT,VALUE_REF,VALUE_MANUFACTURER,VALUE_MODEL,VALUE_PNP,VALUE_KEYS,VALUE_KEY_REF,VALUE_LEDS,VALUE_LED_REF,VALUE_MEDIA,VALUE_MEDIA_REF };
static int access_value(uint16_t ch,uint16_t ah,struct ble_gatt_access_ctxt *ctxt,void *arg)
{
    unsigned kind=(uintptr_t)arg;
    if(ctxt->op==BLE_GATT_ACCESS_OP_WRITE_CHR) {
        uint8_t value;
        if(OS_MBUF_PKTLEN(ctxt->om)!=1||ble_hs_mbuf_to_flat(ctxt->om,&value,1,NULL))return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        if(kind==VALUE_LEDS){leds=value&31;return 0;}
        if(value>1)return BLE_ATT_ERR_UNLIKELY;
        if(kind==VALUE_PROTOCOL){protocol=value;ESP_LOGI(TAG,"Protocol=%s",value?"REPORT":"BOOT");}
        else if(kind==VALUE_CONTROL)suspended=(value==0);
        else return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        portENTER_CRITICAL(&mux);mouse_flow_reset(&flow);keyboard_flow_reset(&keys);keyboard_flow_reset(&media);portEXIT_CRITICAL(&mux);
        return 0;
    }
    const void *data=NULL;size_t len=0;
    static const uint8_t info[]={0x11,0x01,0x00,0x02},ref[]={1,1},pnp[]={1,0xff,0xff,1,0,1,0};
    uint8_t input[6]={0};
    static const uint8_t key_ref[]={2,1},led_ref[]={2,2},media_ref[]={3,1};
    switch(kind) {
    case VALUE_MEDIA:data=media.physical+2;len=2;break;
    case VALUE_MEDIA_REF:data=media_ref;len=2;break;
    case VALUE_INFO:data=info;len=sizeof(info);break;
    case VALUE_MAP:data=report_map;len=sizeof(report_map);break;
    case VALUE_PROTOCOL:data=&protocol;len=1;break;
    case VALUE_INPUT:input[0]=last_buttons;data=input;len=ah==boot_handle?3:6;break;
    case VALUE_REF:data=ref;len=sizeof(ref);break;
    case VALUE_KEYS:data=last_keys;len=8;break;
    case VALUE_KEY_REF:data=key_ref;len=2;break;
    case VALUE_LEDS:data=&leds;len=1;break;
    case VALUE_LED_REF:data=led_ref;len=2;break;
    case VALUE_MANUFACTURER:data="TouchDesk Prototype";len=strlen(data);break;
    case VALUE_MODEL:data="TouchDesk HID";len=strlen(data);break;
    case VALUE_PNP:data=pnp;len=sizeof(pnp);break;
    default:return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }
    return os_mbuf_append(ctxt->om,data,len)==0?0:BLE_ATT_ERR_INSUFFICIENT_RES;
}
#define CHR(u,k,f) .uuid=BLE_UUID16_DECLARE(u),.access_cb=access_value,.arg=(void *)(uintptr_t)(k),.flags=(f)
static const struct ble_gatt_svc_def services[]={
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=BLE_UUID16_DECLARE(0x1812),.characteristics=(struct ble_gatt_chr_def[]){
        {CHR(0x2a4d,VALUE_MEDIA,BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY|BLE_GATT_CHR_F_READ_ENC),.val_handle=&media_handle,
         .descriptors=(struct ble_gatt_dsc_def[]){
            {.uuid=BLE_UUID16_DECLARE(0x2908),.att_flags=BLE_ATT_F_READ,.access_cb=access_value,.arg=(void *)VALUE_MEDIA_REF},{0}}},
        {CHR(0x2a4a,VALUE_INFO,BLE_GATT_CHR_F_READ)},
        {CHR(0x2a4b,VALUE_MAP,BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_READ_ENC)},
        {CHR(0x2a4e,VALUE_PROTOCOL,BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_WRITE_ENC)},
        {CHR(0x2a4c,VALUE_CONTROL,BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_WRITE_ENC)},
        {CHR(0x2a4d,VALUE_INPUT,BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY|BLE_GATT_CHR_F_READ_ENC),.val_handle=&report_handle,
         .descriptors=(struct ble_gatt_dsc_def[]){
            {.uuid=BLE_UUID16_DECLARE(0x2908),.att_flags=BLE_ATT_F_READ,.access_cb=access_value,.arg=(void *)VALUE_REF},{0}}},
        {CHR(0x2a33,VALUE_INPUT,BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY|BLE_GATT_CHR_F_READ_ENC),.val_handle=&boot_handle},
        {CHR(0x2a4d,VALUE_KEYS,BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY|BLE_GATT_CHR_F_READ_ENC),.val_handle=&key_handle,
         .descriptors=(struct ble_gatt_dsc_def[]){
            {.uuid=BLE_UUID16_DECLARE(0x2908),.att_flags=BLE_ATT_F_READ,.access_cb=access_value,.arg=(void *)VALUE_KEY_REF},{0}}},
        {CHR(0x2a22,VALUE_KEYS,BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_NOTIFY|BLE_GATT_CHR_F_READ_ENC),.val_handle=&key_boot_handle},
        {CHR(0x2a4d,VALUE_LEDS,BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_WRITE_ENC),
         .descriptors=(struct ble_gatt_dsc_def[]){
            {.uuid=BLE_UUID16_DECLARE(0x2908),.att_flags=BLE_ATT_F_READ,.access_cb=access_value,.arg=(void *)VALUE_LED_REF},{0}}},
        {CHR(0x2a32,VALUE_LEDS,BLE_GATT_CHR_F_READ|BLE_GATT_CHR_F_WRITE|BLE_GATT_CHR_F_WRITE_NO_RSP|BLE_GATT_CHR_F_READ_ENC|BLE_GATT_CHR_F_WRITE_ENC)},{0}}},
    {.type=BLE_GATT_SVC_TYPE_PRIMARY,.uuid=BLE_UUID16_DECLARE(0x180a),.characteristics=(struct ble_gatt_chr_def[]){
        {CHR(0x2a29,VALUE_MANUFACTURER,BLE_GATT_CHR_F_READ)},
        {CHR(0x2a24,VALUE_MODEL,BLE_GATT_CHR_F_READ)},
        {CHR(0x2a50,VALUE_PNP,BLE_GATT_CHR_F_READ)},{0}}},{0}
};
#undef CHR
static int bonds(ble_addr_t *peers) {
    int n=0,rc=ble_store_util_bonded_peers(peers,&n,1);
    return rc==0?n:0;
}
static bool known_peer(const ble_addr_t *address) {
    ble_addr_t peer[1];int n=bonds(peer);
    return n==1&&(peer[0].type&1)==(address->type&1)&&!memcmp(peer[0].val,address->val,6);
}
static bool pairing(void){return esp_timer_get_time()<pairing_until;}
static void advertise(void)
{
    if(!synced||conn!=BLE_HS_CONN_HANDLE_NONE||(atomic_load(&commands)&2)||ble_gap_adv_active())return;
    ble_addr_t peer[1];if(!pairing()&&!bonds(peer))return;
    static const ble_uuid16_t uuid=BLE_UUID16_INIT(0x1812);
    struct ble_hs_adv_fields fields={0};
    fields.flags=BLE_HS_ADV_F_DISC_GEN|BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name=(uint8_t *)"TouchDesk KeyMouse";fields.name_len=strlen((char *)fields.name);fields.name_is_complete=1;
    fields.uuids16=&uuid;fields.num_uuids16=1;fields.uuids16_is_complete=1;
    fields.appearance=0x03c0;fields.appearance_is_present=1;
    int rc=ble_gap_adv_set_fields(&fields);
    struct ble_gap_adv_params params={.conn_mode=BLE_GAP_CONN_MODE_UND,.disc_mode=BLE_GAP_DISC_MODE_GEN,.itvl_min=0x30,.itvl_max=0x60};
    if(!rc)rc=ble_gap_adv_start(address_type,NULL,BLE_HS_FOREVER,&params,gap_event,NULL);
    if(rc)ESP_LOGW(TAG,"Advertising failed=%d",rc);
    else ESP_LOGI(TAG,"Advertising TouchDesk KeyMouse; pairing=%u",pairing());
}
static void reset_link(void) {
    conn=BLE_HS_CONN_HANDLE_NONE;report_notify=boot_notify=suspended=false;protocol=1;last_buttons=0;
    key_notify=key_boot_notify=media_notify=false;memset(last_keys,0,8);
    portENTER_CRITICAL(&mux);status.connected=status.encrypted=false;mouse_flow_ready(&flow,false);keyboard_flow_ready(&keys,false);keyboard_flow_ready(&media,false);memset(media.physical,0,8);portEXIT_CRITICAL(&mux);
}
static int gap_event(struct ble_gap_event *e,void *arg)
{
    struct ble_gap_conn_desc desc;
    switch(e->type) {
    case BLE_GAP_EVENT_CONNECT:
        if(e->connect.status){ESP_LOGW(TAG,"Connect failed=%d",e->connect.status);advertise();break;}
        conn=e->connect.conn_handle;
        if(ble_gap_conn_find(conn,&desc)!=0){ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);break;}
        if(!pairing()&&!known_peer(&desc.peer_id_addr)) {
            ESP_LOGW(TAG,"Unknown peer outside pairing window rejected");ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);break;
        }
        protocol=1;suspended=report_notify=boot_notify=false;
        key_notify=key_boot_notify=false;
        portENTER_CRITICAL(&mux);status.connected=true;status.encrypted=false;status.interval=desc.conn_itvl;status.reconnections++;mouse_flow_ready(&flow,false);portEXIT_CRITICAL(&mux);
        ESP_LOGI(TAG,"Connected; requesting encryption");
        portENTER_CRITICAL(&mux);keyboard_flow_ready(&keys,false);keyboard_flow_ready(&media,false);memset(media.physical,0,8);portEXIT_CRITICAL(&mux);
        int rc=ble_gap_security_initiate(conn);if(rc)ESP_LOGW(TAG,"Security initiate=%d",rc);
        struct ble_gap_upd_params upd={.itvl_min=6,.itvl_max=12,.latency=0,.supervision_timeout=400};
        rc=ble_gap_update_params(conn,&upd);if(rc)ESP_LOGW(TAG,"Connection parameter request=%d",rc);
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG,"Disconnected reason=%d",e->disconnect.reason);reset_link();advertise();break;
    case BLE_GAP_EVENT_ENC_CHANGE:
        if(!ble_gap_conn_find(e->enc_change.conn_handle,&desc)) {
            portENTER_CRITICAL(&mux);status.encrypted=desc.sec_state.encrypted;portEXIT_CRITICAL(&mux);
            if(desc.sec_state.encrypted&&desc.sec_state.bonded)pairing_until=0;
            ESP_LOGI(TAG,"Encryption status=%d encrypted=%u bonded=%u",e->enc_change.status,desc.sec_state.encrypted,desc.sec_state.bonded);
        }
        break;
    case BLE_GAP_EVENT_SUBSCRIBE:
        if(e->subscribe.attr_handle==report_handle)report_notify=e->subscribe.cur_notify;
        if(e->subscribe.attr_handle==boot_handle)boot_notify=e->subscribe.cur_notify;
        if(e->subscribe.attr_handle==media_handle)media_notify=e->subscribe.cur_notify;
        if(e->subscribe.attr_handle==key_handle)key_notify=e->subscribe.cur_notify;
        if(e->subscribe.attr_handle==key_boot_handle)key_boot_notify=e->subscribe.cur_notify;
        ESP_LOGI(TAG,"Subscribe handle=%u enabled=%u (report=%u boot=%u)",e->subscribe.attr_handle,e->subscribe.cur_notify,report_handle,boot_handle);break;
    case BLE_GAP_EVENT_CONN_UPDATE:
        if(!ble_gap_conn_find(e->conn_update.conn_handle,&desc)) {
            portENTER_CRITICAL(&mux);status.interval=desc.conn_itvl;portEXIT_CRITICAL(&mux);
            ESP_LOGI(TAG,"Connection interval=%u x 1.25ms",desc.conn_itvl);
        }
        break;
    case BLE_GAP_EVENT_NOTIFY_TX:
        if(e->notify_tx.attr_handle==report_handle||e->notify_tx.attr_handle==boot_handle) {
            portENTER_CRITICAL(&mux);if(!e->notify_tx.status)status.completed++;else {status.failed++;mouse_flow_reset(&flow);}portEXIT_CRITICAL(&mux);
        }
        if(e->notify_tx.attr_handle==media_handle && e->notify_tx.status) {
            portENTER_CRITICAL(&mux);keyboard_flow_reset(&media);portEXIT_CRITICAL(&mux);
        }
        if(e->notify_tx.attr_handle==key_handle||e->notify_tx.attr_handle==key_boot_handle) {
            portENTER_CRITICAL(&mux);if(!e->notify_tx.status)status.key_completed++;else {status.key_failed++;keyboard_flow_reset(&keys);}portEXIT_CRITICAL(&mux);
        }break;
    case BLE_GAP_EVENT_REPEAT_PAIRING:
        if(pairing()&&!ble_gap_conn_find(e->repeat_pairing.conn_handle,&desc)) {
            ble_store_util_delete_peer(&desc.peer_id_addr);return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
        ESP_LOGW(TAG,"Press Pair to allow pairing again");return BLE_GAP_REPEAT_PAIRING_IGNORE;
    case BLE_GAP_EVENT_ADV_COMPLETE:advertise();break;
    default:break;
    }
    return 0;
}
static void tick(struct ble_npl_event *ev)
{
    if(atomic_load(&stopping_ble))return;
    xSemaphoreTake(touch_lock,portMAX_DELAY);
    unsigned cmd=atomic_exchange(&commands,0);
    if(cmd&2) {
        if(conn!=BLE_HS_CONN_HANDLE_NONE) {
            portENTER_CRITICAL(&mux);mouse_flow_ready(&flow,false);keyboard_flow_ready(&keys,false);keyboard_flow_ready(&media,false);memset(media.physical,0,8);portEXIT_CRITICAL(&mux);
            ble_gap_terminate(conn,BLE_ERR_REM_USER_CONN_TERM);atomic_fetch_or(&commands,2);
        } else {
            if(ble_gap_adv_active())ble_gap_adv_stop();
            int rc=ble_store_clear();ESP_LOGI(TAG,"Clear BLE bonds=%d",rc);
            pairing_until=esp_timer_get_time()+120000000;
        }
    }
    if(cmd&1)pairing_until=esp_timer_get_time()+120000000;
    ble_addr_t peer[1];
    if(!pairing()&&conn==BLE_HS_CONN_HANDLE_NONE&&!bonds(peer)&&ble_gap_adv_active())ble_gap_adv_stop();
    advertise();
    bool ready,adv_active=ble_gap_adv_active();
    portENTER_CRITICAL(&mux);
    ready=status.connected&&status.encrypted&&!suspended&&(protocol?report_notify:boot_notify)&&!(cmd&2);
    mouse_flow_ready(&flow,ready);status.ready=ready;
    status.pairing=pairing();status.advertising=adv_active;
    mouse_packet_t p;bool send=mouse_flow_peek_limit(&flow,&p,protocol?32767:127);
    bool key_ready=status.connected&&status.encrypted&&!suspended&&(protocol?key_notify:key_boot_notify)&&!(cmd&2);
    keyboard_flow_ready(&keys,key_ready);status.keyboard_ready=key_ready;
    keyboard_flow_ready(&media,key_ready&&protocol&&media_notify);
    keyboard_packet_t kp;bool send_key=keyboard_flow_peek(&keys,&kp);
    portEXIT_CRITICAL(&mux);
    if(send) {
        uint8_t bytes[6]={p.buttons,(uint8_t)p.x,(uint8_t)((uint16_t)p.x>>8),(uint8_t)p.y,(uint8_t)((uint16_t)p.y>>8),(uint8_t)p.wheel};
        if(!protocol){bytes[1]=(uint8_t)p.x;bytes[2]=(uint8_t)p.y;}
        struct os_mbuf *om=ble_hs_mbuf_from_flat(bytes,protocol?6:3);
        int rc=om?ble_gatts_notify_custom(conn,protocol?report_handle:boot_handle,om):BLE_HS_ENOMEM;
        portENTER_CRITICAL(&mux);
        if(!rc){mouse_flow_commit(&flow,&p);status.sent++;last_buttons=p.buttons;consecutive_failures=0;}
        else {
            status.failed++;
            if(++consecutive_failures>=10){mouse_flow_reset(&flow);consecutive_failures=0;}
        }
        portEXIT_CRITICAL(&mux);
    }
    if(send_key) {
        struct os_mbuf *om=ble_hs_mbuf_from_flat(kp.data,8);
        int rc=om?ble_gatts_notify_custom(conn,protocol?key_handle:key_boot_handle,om):BLE_HS_ENOMEM;
        portENTER_CRITICAL(&mux);
        if(!rc){keyboard_flow_commit(&keys,&kp);memcpy(last_keys,kp.data,8);status.key_sent++;key_failures=0;}
        else {status.key_failed++;if(++key_failures>=10){keyboard_flow_reset(&keys);key_failures=0;}}
        portEXIT_CRITICAL(&mux);
    }
    portENTER_CRITICAL(&mux);
    keyboard_packet_t cp;bool send_media=keyboard_flow_peek(&media,&cp);
    portEXIT_CRITICAL(&mux);
    if(send_media) {
        struct os_mbuf *om=ble_hs_mbuf_from_flat(cp.data+2,2);
        int rc=om?ble_gatts_notify_custom(conn,media_handle,om):BLE_HS_ENOMEM;
        if(!rc){portENTER_CRITICAL(&mux);keyboard_flow_commit(&media,&cp);portEXIT_CRITICAL(&mux);}
    }
    xSemaphoreGive(touch_lock);
    if(!atomic_load(&stopping_ble))ble_npl_callout_reset(&timer,ble_npl_time_ms_to_ticks32(10));
}
static void sync_host(void) {
    int rc=ble_hs_util_ensure_addr(0);if(!rc)rc=ble_hs_id_infer_auto(0,&address_type);
    if(rc){ESP_LOGE(TAG,"Identity error=%d",rc);return;}
    synced=true;ble_addr_t peer[1];if(!bonds(peer))pairing_until=esp_timer_get_time()+120000000;
    advertise();ble_npl_callout_reset(&timer,1);
}
static void reset_host(int reason){ESP_LOGW(TAG,"Host reset=%d",reason);synced=false;reset_link();}
static void host_task(void *arg){nimble_port_run();for(;;)vTaskSuspend(NULL);}
void ble_store_config_init(void);
esp_err_t ble_mouse_init(void)
{
    if(initialized)return atomic_load(&stopping_ble)?ESP_ERR_INVALID_STATE:ESP_OK;
    if(port_initialized)return ESP_ERR_INVALID_STATE;
    if(!touch_lock)touch_lock=xSemaphoreCreateMutex();
    if(!touch_lock)return ESP_ERR_NO_MEM;
    atomic_store(&stopping_ble,false);host_stopped=false;
    protocol=1;suspended=false;consecutive_failures=key_failures=0;memset(last_keys,0,sizeof(last_keys));last_buttons=leds=0;
    esp_err_t err=nvs_flash_init();if(err!=ESP_OK)return err; /* Never erase existing settings. */
    err=nimble_port_init();if(err!=ESP_OK)return err;
    port_initialized=true;
    ble_hs_cfg.sync_cb=sync_host;ble_hs_cfg.reset_cb=reset_host;
    ble_hs_cfg.store_status_cb=ble_store_util_status_rr;
    ble_hs_cfg.sm_io_cap=BLE_SM_IO_CAP_NO_IO;ble_hs_cfg.sm_bonding=1;ble_hs_cfg.sm_sc=1;ble_hs_cfg.sm_mitm=0;
    ble_hs_cfg.sm_our_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist=BLE_SM_PAIR_KEY_DIST_ENC|BLE_SM_PAIR_KEY_DIST_ID;
    ble_svc_gap_init();ble_svc_gatt_init();
    int rc=ble_svc_gap_device_name_set("TouchDesk KeyMouse");if(!rc)rc=ble_svc_gap_device_appearance_set(0x03c0);
    if(!rc)rc=ble_gatts_count_cfg(services);
    if(!rc)rc=ble_gatts_add_svcs(services);
    if(rc){ESP_LOGE(TAG,"GATT init=%d",rc);err=ESP_FAIL;goto failed;}
    ble_store_config_init();
    /* A changed report map must not be sent using the PC's cached mouse-only
     * profile. Migrate only BLE keys, once; never erase the NVS partition. */
    nvs_handle_t meta;
    err=nvs_open("bridge_hid",NVS_READWRITE,&meta);if(err!=ESP_OK)goto failed;
    uint32_t schema=0;nvs_get_u32(meta,"schema",&schema);
    if(schema!=0x00080503) {
        rc=ble_store_clear();
        if(rc){nvs_close(meta);ESP_LOGE(TAG,"BLE profile reset=%d",rc);err=ESP_FAIL;goto failed;}
        err=nvs_set_u32(meta,"schema",0x00080503);
        if(err==ESP_OK)err=nvs_commit(meta);
        if(err!=ESP_OK){nvs_close(meta);goto failed;}
        ESP_LOGI(TAG,"HID schema upgraded; BLE bonds reset once. Remove old Windows pairing.");
    }
    nvs_close(meta);
    if(ble_npl_callout_init(&timer,nimble_port_get_dflt_eventq(),tick,NULL)!=0){err=ESP_ERR_NO_MEM;goto failed;}
    timer_initialized=true;
    if(xTaskCreatePinnedToCore(host_task,"nimble_host",CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE,NULL,configMAX_PRIORITIES-4,&ble_task,CONFIG_BT_NIMBLE_PINNED_TO_CORE)!=pdPASS) {
      ble_npl_callout_deinit(&timer);timer_initialized=false;err=ESP_ERR_NO_MEM;goto failed;
    }
    initialized=true;
    ESP_LOGI(TAG,"HOGP mouse started; name=TouchDesk KeyMouse; first pairing window 120s");return ESP_OK;
failed:
    if(nimble_port_deinit()==ESP_OK)port_initialized=false;
    return err;
}
void ble_mouse_input(uint8_t buttons,int x,int y){portENTER_CRITICAL(&mux);mouse_flow_feed(&flow,buttons,x,y);portEXIT_CRITICAL(&mux);}
void ble_mouse_wheel_input(uint8_t buttons,int x,int y,int wheel){portENTER_CRITICAL(&mux);mouse_flow_feed_wheel(&flow,buttons,x,y,wheel);portEXIT_CRITICAL(&mux);}
void ble_mouse_usb_gone(void){portENTER_CRITICAL(&mux);flow.physical=0;mouse_flow_reset(&flow);portEXIT_CRITICAL(&mux);}
void ble_mouse_pair(void){atomic_fetch_or(&commands,1);}
void ble_mouse_forget(void){atomic_fetch_or(&commands,2);}
void ble_mouse_snapshot(ble_mouse_snapshot_t *out) {
    portENTER_CRITICAL(&mux);*out=status;out->dropped=flow.dropped;out->overflows=flow.overflows;out->queued=flow.count;portEXIT_CRITICAL(&mux);
    portENTER_CRITICAL(&mux);out->key_queued=keys.count;out->key_dropped=keys.dropped;out->key_overflows=keys.overflows;out->key_rollovers=keys.rollovers;portEXIT_CRITICAL(&mux);
}
void ble_keyboard_input(const uint8_t report[8]){portENTER_CRITICAL(&mux);keyboard_flow_feed(&keys,report);portEXIT_CRITICAL(&mux);}
void ble_keyboard_usb_gone(void){portENTER_CRITICAL(&mux);memset(keys.physical,0,8);keyboard_flow_reset(&keys);keyboard_flow_reset(&media);portEXIT_CRITICAL(&mux);}

esp_err_t ble_mouse_deinit(void) {
 if(!initialized) {
  if(!port_initialized)return ESP_OK;
  esp_err_t e=nimble_port_deinit();if(e==ESP_OK)port_initialized=false;return e;
 }
 atomic_store(&stopping_ble,true);
 if(timer_initialized)ble_npl_callout_stop(&timer);
 if(!host_stopped){int rc=nimble_port_stop();if(rc)return ESP_FAIL;host_stopped=true;}
 esp_err_t e=service_task_join(&ble_task,5000);if(e!=ESP_OK)return e;
 if(timer_initialized){ble_npl_callout_deinit(&timer);timer_initialized=false;}
 e=nimble_port_deinit();if(e!=ESP_OK)return e;port_initialized=false;
 initialized=false;synced=false;conn=BLE_HS_CONN_HANDLE_NONE;
 report_notify=boot_notify=key_notify=key_boot_notify=false;pairing_until=0;
 portENTER_CRITICAL(&mux);memset(&status,0,sizeof(status));memset(&flow,0,sizeof(flow));memset(&keys,0,sizeof(keys));memset(&media,0,sizeof(media));media_notify=false;portEXIT_CRITICAL(&mux);
 atomic_store(&commands,0);return ESP_OK;
}

bool ble_touch_ready(void) {
    portENTER_CRITICAL(&mux);
    bool ready=flow.ready&&keys.ready&&media.ready;
    portEXIT_CRITICAL(&mux);return ready;
}
void ble_touch_cancel(void) {
    if(!touch_lock)return;
    xSemaphoreTake(touch_lock,portMAX_DELAY);portENTER_CRITICAL(&mux);
    flow.physical=0;memset(keys.physical,0,8);memset(media.physical,0,8);
    mouse_flow_reset(&flow);keyboard_flow_reset(&keys);keyboard_flow_reset(&media);
    portEXIT_CRITICAL(&mux);xSemaphoreGive(touch_lock);
}
bool ble_touch_report(unsigned id,const void *data) {
    if(!touch_lock)return false;
    xSemaphoreTake(touch_lock,portMAX_DELAY);portENTER_CRITICAL(&mux);
    bool ok=false;
    if(id==1) {
        const int16_t *r=data;
        ok=flow.ready&&flow.armed&&!flow.release&&flow.count<MOUSE_FLOW_CAP;
        if(ok)mouse_flow_feed_wheel(&flow,r[0],r[1],r[2],r[3]);
    } else {
        keyboard_flow_t *f=id==2?&keys:&media;uint8_t r[8]={0};
        if(id==2)memcpy(r,data,8);else memcpy(r+2,data,2);
        ok=f->ready&&f->armed&&!f->release&&f->count<KEYBOARD_FLOW_CAP;
        if(ok)keyboard_flow_feed(f,r);
    }
    portEXIT_CRITICAL(&mux);xSemaphoreGive(touch_lock);return ok;
}
