#include "wifi_service.h"
#include "wifi_store.h"
#include "service_task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_wifi.h"
#include "esp_wifi_default.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

typedef enum {
    CMD_ENABLE,CMD_DISABLE,CMD_SCAN,CMD_CONNECT,CMD_SAVED,CMD_DISCONNECT,CMD_FORGET,CMD_KNOWN,
    EVT_START,EVT_STOP,EVT_SCAN,EVT_CONNECTED,EVT_DISCONNECTED,EVT_IP,EVT_LOST_IP
} message_kind_t;
typedef struct {
    message_kind_t kind;
    wifi_profile_t *profile;
    uint16_t reason;
    uint8_t ssid[32],ssid_len;
    char ip[16];
} message_t;
typedef enum { AFTER_OFF,AFTER_IDLE,AFTER_CONNECT,AFTER_RETRY,AFTER_AUTH,AFTER_AUTO } after_stop_t;
static TaskHandle_t service_task;
static atomic_bool stop_requested;
static esp_err_t stop_result;
static bool wifi_registered,ip_registered;
static QueueHandle_t commands;
static SemaphoreHandle_t snapshot_lock;
static atomic_bool event_overflow;
static atomic_bool faulted;
static esp_netif_t *sta_netif;
static bool netif_attached;
static esp_event_handler_instance_t wifi_handler,ip_handler;
/* Only worker mutates these; callbacks only enqueue. Published copies contain no credentials. */
static wifi_saved_t saved;
static wifi_profile_t target;
static wifi_status_t state,published;
static wifi_network_t networks[WIFI_NETWORK_LIMIT],published_networks[WIFI_NETWORK_LIMIT];
static size_t network_count,published_count,published_saved_count;
static wifi_network_t published_saved[WIFI_SAVED_LIMIT];
static bool auto_mode,automatic_scan;
static unsigned tried_mask,blocked_mask;
static int target_saved=-1;
static int64_t unblock_at;
static bool initialized,started,waiting_start,want_connection,scan_after_start;
static after_stop_t after_stop;
static int64_t deadline,retry_at,next_rssi;
static unsigned retry_attempt;
static int64_t now_us(void) { return esp_timer_get_time(); }
static size_t bounded_length(const char *s,size_t limit)
{
    size_t n=0; while(n<limit && s[n]) n++; return n;
}
static void publish(void)
{
    state.has_saved=saved.has_profile; state.saved_count=saved.count;
    if(!state.name[0]) { memset(state.ssid,0,32); state.ssid_len=0; }
    wifi_display_name(saved.profile.ssid,saved.has_profile?saved.profile.ssid_len:0,
                      state.saved_name,sizeof(state.saved_name));
    state.retry_seconds=retry_at>now_us() ? (unsigned)((retry_at-now_us()+999999)/1000000) : 0;
    state.revision++;
    xSemaphoreTake(snapshot_lock,portMAX_DELAY);
    published=state;
    published_count=network_count;
    memcpy(published_networks,networks,network_count*sizeof(networks[0]));
    for(size_t i=0;i<network_count;i++) published_networks[i].remembered=wifi_saved_find(&saved,networks[i].ssid,networks[i].ssid_len,networks[i].security)>=0;
    published_saved_count=saved.count;
    memset(published_saved,0,sizeof(published_saved));
    for(unsigned i=0;i<saved.count;i++) {
        const wifi_profile_t *p=wifi_saved_profile(&saved,i); wifi_network_t *n=&published_saved[i];
        memcpy(n->ssid,p->ssid,32); n->ssid_len=p->ssid_len; n->security=p->security; n->remembered=true;
        wifi_display_name(n->ssid,n->ssid_len,n->name,sizeof(n->name));
    }
    xSemaphoreGive(snapshot_lock);
}
void wifi_service_status(wifi_status_t *out)
{
    memset(out,0,sizeof(*out));
    if(!snapshot_lock) return;
    xSemaphoreTake(snapshot_lock,portMAX_DELAY); *out=published; xSemaphoreGive(snapshot_lock);
}
size_t wifi_service_networks(wifi_network_t *out,size_t capacity,unsigned *revision)
{
    if(!snapshot_lock) { if(revision) *revision=0; return 0; }
    xSemaphoreTake(snapshot_lock,portMAX_DELAY);
    size_t n=capacity<published_count ? capacity : published_count;
    if(n && out) memcpy(out,published_networks,n*sizeof(*out));
    if(revision) *revision=published.scan_revision;
    xSemaphoreGive(snapshot_lock); return n;
}
size_t wifi_service_saved_networks(wifi_network_t *out,size_t capacity,unsigned *revision)
{
    if(!snapshot_lock) { if(revision) *revision=0; return 0; }
    xSemaphoreTake(snapshot_lock,portMAX_DELAY);
    size_t n=capacity<published_saved_count?capacity:published_saved_count;
    if(n&&out) memcpy(out,published_saved,n*sizeof(*out));
    if(revision) *revision=published.saved_revision;
    xSemaphoreGive(snapshot_lock); return n;
}
static bool post(message_kind_t kind,wifi_profile_t *profile)
{
    if(!commands || atomic_load(&faulted)) return false;
    message_t msg={.kind=kind,.profile=profile};
    return xQueueSend(commands,&msg,0)==pdTRUE;
}
bool wifi_service_enable(bool enabled) { return post(enabled?CMD_ENABLE:CMD_DISABLE,NULL); }
bool wifi_service_scan(void) { return post(CMD_SCAN,NULL); }
bool wifi_service_connect_saved(void) { return post(CMD_SAVED,NULL); }
bool wifi_service_disconnect(void) { return post(CMD_DISCONNECT,NULL); }
bool wifi_service_forget(void) { return post(CMD_FORGET,NULL); }
bool wifi_service_connect(const wifi_network_t *network,const char *password)
{
    if(!network || !password) return false;
    wifi_profile_t *p=heap_caps_calloc(1,sizeof(*p),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!p) return false;
    memcpy(p->ssid,network->ssid,32); p->ssid_len=network->ssid_len; p->security=network->security;
    size_t n=bounded_length(password,64);
    if(n<64) memcpy(p->password,password,n);
    bool ok=n<64 && wifi_profile_valid(p) && post(CMD_CONNECT,p);
    if(!ok) { wifi_secret_clear(p,sizeof(*p)); heap_caps_free(p); }
    return ok;
}
static bool post_known(message_kind_t command,const wifi_network_t *network)
{
    if(!network||!network->ssid_len||network->ssid_len>32||network->security>=NET_UNSUPPORTED) return false;
    wifi_profile_t *key=heap_caps_calloc(1,sizeof(*key),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!key) return false;
    memcpy(key->ssid,network->ssid,32); key->ssid_len=network->ssid_len; key->security=network->security;
    if(post(command,key)) return true;
    wifi_secret_clear(key,sizeof(*key)); heap_caps_free(key); return false;
}
bool wifi_service_connect_known(const wifi_network_t *network) { return post_known(CMD_KNOWN,network); }
bool wifi_service_forget_known(const wifi_network_t *network) { return post_known(CMD_FORGET,network); }
static void event_callback(void *arg,esp_event_base_t base,int32_t id,void *data)
{
    message_t msg={0};
    if(base==WIFI_EVENT) {
        switch(id) {
        case WIFI_EVENT_STA_START: msg.kind=EVT_START; break;
        case WIFI_EVENT_STA_STOP: msg.kind=EVT_STOP; break;
        case WIFI_EVENT_SCAN_DONE: msg.kind=EVT_SCAN; msg.reason=((wifi_event_sta_scan_done_t*)data)->status; break;
        case WIFI_EVENT_STA_CONNECTED: {
            wifi_event_sta_connected_t *e=data;
            msg.kind=EVT_CONNECTED; msg.ssid_len=e->ssid_len;
            memcpy(msg.ssid,e->ssid,32); break;
        }
        case WIFI_EVENT_STA_DISCONNECTED:
            msg.kind=EVT_DISCONNECTED; msg.reason=((wifi_event_sta_disconnected_t*)data)->reason; break;
        default: return;
        }
    } else if(base==IP_EVENT) {
        if(id==IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *e=data;
            if(e->esp_netif!=sta_netif) return;
            msg.kind=EVT_IP; snprintf(msg.ip,sizeof(msg.ip),IPSTR,IP2STR(&e->ip_info.ip));
        } else if(id==IP_EVENT_STA_LOST_IP) msg.kind=EVT_LOST_IP;
        else return;
    } else return;
    if(commands && xQueueSend(commands,&msg,0)!=pdTRUE) atomic_store(&event_overflow,true);
}
static void persist(void)
{
    if(wifi_store_save(&saved)!=ESP_OK) state.save_warning=true;
    else state.save_warning=false;
}
static bool persist_candidate(const wifi_saved_t *candidate)
{
    if(wifi_store_save(candidate)!=ESP_OK) { state.save_warning=true; return false; }
    saved=*candidate; state.save_warning=false; state.saved_revision++; state.scan_revision++; return true;
}
static void select_saved(unsigned index);
static void finish_auto_scan(bool ok)
{
    if(!automatic_scan) return;
    automatic_scan=false;
    if(unblock_at&&now_us()>=unblock_at) { blocked_mask=0; unblock_at=0; }
    int best=ok?wifi_saved_best(&saved,networks,network_count,tried_mask|blocked_mask):-1;
    if(best>=0) { select_saved((unsigned)best); return; }
    tried_mask=0; state.state=NET_RETRY; deadline=0;
    unsigned wait=wifi_retry_delay(retry_attempt++); if(wait<5) wait=5;
    retry_at=now_us()+(int64_t)wait*1000000;
}
static void operation_error(net_error_t error,esp_err_t err)
{
    /* Never log SSID, config structs, commands or passwords. */
    ESP_LOGE("network","operation failed: %s",esp_err_to_name(err));
    state.state=NET_ERROR; state.error=error; state.scanning=false;
    state.ip[0]=0;
    want_connection=auto_mode=automatic_scan=false; deadline=retry_at=0; scan_after_start=false;
    wifi_secret_clear(&target,sizeof(target));
}
static esp_err_t ensure_driver(void)
{
    if(initialized) return ESP_OK;
    wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();
    config.nvs_enable=0; /* Credentials live only in our explicitly managed NVS blob. */
    esp_err_t err=esp_wifi_init(&config);
    if(err!=ESP_OK) return err;
    initialized=true;
    err=esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if(err==ESP_OK) err=esp_wifi_set_mode(WIFI_MODE_STA);
    if(err!=ESP_OK) { esp_wifi_deinit(); initialized=false; }
    return err;
}
static esp_err_t start_radio(void)
{
    esp_err_t err=ensure_driver();
    if(err==ESP_OK && !started) {
        waiting_start=true;
        err=esp_wifi_start();
        if(err==ESP_OK) { started=true; deadline=now_us()+5000000; }
        else waiting_start=false;
    }
    return err;
}
static void start_connect(void)
{
    if(!want_connection || !state.enabled) return;
    esp_err_t err=esp_wifi_connect();
    if(err!=ESP_OK) operation_error(NET_ERR_DRIVER,err);
    else { state.state=NET_CONNECTING; deadline=now_us()+30000000; }
}
static void start_scan(void)
{
    if(!state.enabled || state.scanning || waiting_start) return;
    wifi_scan_config_t cfg={.show_hidden=false};
    esp_err_t err=esp_wifi_scan_start(&cfg,false);
    if(err==ESP_OK) { state.scanning=true; state.error=NET_ERR_NONE; deadline=now_us()+15000000; }
    else {
        state.error=NET_ERR_SCAN;
        if(state.state!=NET_CONNECTED) state.state=NET_ERROR;
    }
}
static void execute_after_stop(void)
{
    deadline=0; state.ip[0]=0;
    if(initialized) esp_wifi_clear_ap_list();
    if(after_stop==AFTER_OFF) {
        if(initialized) { esp_wifi_clear_ap_list(); esp_wifi_deinit(); initialized=false; }
        state.state=NET_OFF; state.error=NET_ERR_NONE;
        return;
    }
    if(after_stop==AFTER_RETRY) {
        state.state=NET_RETRY;
        retry_at=now_us()+(int64_t)wifi_retry_delay(retry_attempt++)*1000000;
        return;
    }
    esp_err_t err=ensure_driver();
    if(err!=ESP_OK) { operation_error(NET_ERR_INIT,err); return; }
    if(after_stop==AFTER_CONNECT) {
        wifi_config_t cfg={0};
        memcpy(cfg.sta.ssid,target.ssid,target.ssid_len);
        memcpy(cfg.sta.password,target.password,sizeof(cfg.sta.password));
        cfg.sta.scan_method=WIFI_ALL_CHANNEL_SCAN;
        cfg.sta.sort_method=WIFI_CONNECT_AP_BY_SIGNAL;
        cfg.sta.threshold.authmode=target.security==NET_OPEN ? WIFI_AUTH_OPEN :
            (target.security==NET_WPA3 ? WIFI_AUTH_WPA3_PSK : WIFI_AUTH_WPA2_PSK);
        cfg.sta.pmf_cfg.capable=true;
        cfg.sta.pmf_cfg.required=target.security==NET_WPA3;
        cfg.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
        err=esp_wifi_set_config(WIFI_IF_STA,&cfg);
        wifi_secret_clear(&cfg,sizeof(cfg));
        if(err!=ESP_OK) { operation_error(NET_ERR_DRIVER,err); return; }
        state.state=NET_CONNECTING;
    } else {
        wifi_config_t blank={0};
        err=esp_wifi_set_config(WIFI_IF_STA,&blank);
        if(err!=ESP_OK) { operation_error(NET_ERR_DRIVER,err); return; }
        state.state=after_stop==AFTER_AUTH ? NET_ERROR : after_stop==AFTER_AUTO?NET_RETRY:NET_IDLE;
        if(after_stop==AFTER_AUTO) { automatic_scan=true; scan_after_start=true; }
    }
    err=start_radio();
    if(err!=ESP_OK) { operation_error(NET_ERR_INIT,err); return; }
    if(after_stop==AFTER_CONNECT && !waiting_start) start_connect();
    else if(after_stop==AFTER_AUTO&&!waiting_start) { scan_after_start=false; start_scan(); if(!state.scanning) finish_auto_scan(false); }
}
static void stop_then(after_stop_t action)
{
    after_stop=action;
    state.ip[0]=0; state.rssi=0;
    retry_at=0; scan_after_start=false;
    if(state.scanning && initialized) esp_wifi_scan_stop();
    state.scanning=false; automatic_scan=false;
    if(state.state==NET_STOPPING) return; /* The newest user intent wins at the stop barrier. */
    if(started) {
        state.state=NET_STOPPING; deadline=now_us()+5000000;
        esp_err_t err=esp_wifi_stop();
        if(err!=ESP_OK) { operation_error(NET_ERR_DRIVER,err); }
    } else execute_after_stop();
}
static net_security_t security_of(wifi_auth_mode_t mode)
{
    switch(mode) {
    case WIFI_AUTH_OPEN: return NET_OPEN;
    case WIFI_AUTH_WPA2_PSK: case WIFI_AUTH_WPA_WPA2_PSK: return NET_WPA2;
    case WIFI_AUTH_WPA3_PSK: return NET_WPA3;
    case WIFI_AUTH_WPA2_WPA3_PSK: return NET_WPA2_WPA3;
    default: return NET_UNSUPPORTED;
    }
}
static void collect_scan(unsigned result)
{
    state.scanning=false; deadline=0;
    if(result) {
        esp_wifi_clear_ap_list(); state.error=NET_ERR_SCAN;
        if(state.state!=NET_CONNECTED) state.state=NET_ERROR;
        finish_auto_scan(false); return;
    }
    uint16_t count=32;
    wifi_ap_record_t *records=heap_caps_calloc(count,sizeof(*records),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    if(!records) { esp_wifi_clear_ap_list(); state.error=NET_ERR_SCAN; finish_auto_scan(false); return; }
    esp_err_t err=esp_wifi_scan_get_ap_records(&count,records);
    if(err==ESP_OK) {
        network_count=0;
        for(unsigned i=0;i<count;i++) {
            wifi_network_t item={0};
            item.ssid_len=bounded_length((const char*)records[i].ssid,32);
            if(!item.ssid_len) continue;
            memcpy(item.ssid,records[i].ssid,item.ssid_len);
            item.security=security_of(records[i].authmode); item.rssi=records[i].rssi;
            wifi_display_name(item.ssid,item.ssid_len,item.name,sizeof(item.name));
            network_count=wifi_network_merge(networks,network_count,WIFI_NETWORK_LIMIT,&item);
        }
        state.scan_revision++; state.error=NET_ERR_NONE;
        if(state.state==NET_ERROR) state.state=NET_IDLE;
    } else { state.error=NET_ERR_SCAN; esp_wifi_clear_ap_list(); }
    heap_caps_free(records); finish_auto_scan(err==ESP_OK);
}
static void select_profile(const wifi_profile_t *p)
{
    target=*p; want_connection=true; retry_attempt=0;
    memcpy(state.ssid,target.ssid,32); state.ssid_len=target.ssid_len; state.security=target.security;
    state.error=NET_ERR_NONE;
    wifi_display_name(target.ssid,target.ssid_len,state.name,sizeof(state.name));
    stop_then(AFTER_CONNECT);
}
static void select_saved(unsigned index)
{
    const wifi_profile_t *p=wifi_saved_profile(&saved,index); if(!p) return;
    auto_mode=true; target_saved=(int)index; tried_mask|=1u<<index;
    unsigned attempt=retry_attempt; select_profile(p); retry_attempt=attempt;
}
static void begin_auto(void)
{
    auto_mode=want_connection=true; target_saved=-1;
    wifi_secret_clear(&target,sizeof(target)); state.name[0]=0;
    stop_then(AFTER_AUTO);
}
static void connect_saved(void)
{
    tried_mask=blocked_mask=retry_attempt=0; unblock_at=0;
    if(saved.count>1) begin_auto(); else if(saved.count) select_saved(0);
}
static void handle(const message_t *m)
{
    switch(m->kind) {
    case CMD_ENABLE:
        if(state.enabled && state.state!=NET_ERROR) break;
        saved.enabled=state.enabled=true; persist();
        if(saved.has_profile) connect_saved();
        else { want_connection=false; stop_then(AFTER_IDLE); }
        break;
    case CMD_DISABLE:
        saved.enabled=state.enabled=false; persist(); want_connection=auto_mode=automatic_scan=false;
        wifi_secret_clear(&target,sizeof(target));
        network_count=0; state.scan_revision++; state.name[0]=0; stop_then(AFTER_OFF); break;
    case CMD_DISCONNECT:
        want_connection=auto_mode=automatic_scan=false; wifi_secret_clear(&target,sizeof(target));
        state.name[0]=0; state.error=NET_ERR_NONE;
        if(state.enabled) stop_then(AFTER_IDLE);
        break;
    case CMD_FORGET: {
        int index=m->profile?wifi_saved_find(&saved,m->profile->ssid,m->profile->ssid_len,m->profile->security):
            wifi_saved_find(&saved,target.ssid,target.ssid_len,target.security);
        if(index<0&&!m->profile&&saved.count) index=0;
        if(index<0) break;
        const wifi_profile_t *removing=wifi_saved_profile(&saved,(unsigned)index);
        bool current=target.ssid_len==removing->ssid_len&&wifi_security_compatible(target.security,removing->security)&&
            !memcmp(target.ssid,removing->ssid,target.ssid_len);
        wifi_saved_t candidate=saved; wifi_saved_remove(&candidate,(unsigned)index);
        bool removed=persist_candidate(&candidate); wifi_secret_clear(&candidate,sizeof(candidate));
        if(removed) { tried_mask=blocked_mask=0; target_saved=wifi_saved_find(&saved,target.ssid,target.ssid_len,target.security); }
        if(current||!m->profile||!saved.count) {
            want_connection=auto_mode=automatic_scan=false; wifi_secret_clear(&target,sizeof(target));
            state.name[0]=0; state.error=NET_ERR_NONE;
            if(state.enabled) stop_then(AFTER_IDLE);
        }
        break;
    }
    case CMD_CONNECT:
        if(state.enabled&&m->profile) { auto_mode=false; target_saved=-1; select_profile(m->profile); }
        break;
    case CMD_KNOWN:
        if(state.enabled&&m->profile) {
            int index=wifi_saved_find(&saved,m->profile->ssid,m->profile->ssid_len,m->profile->security);
            if(index>=0) { tried_mask=blocked_mask=retry_attempt=0; select_saved((unsigned)index); }
        }
        break;
    case CMD_SAVED:
        if(state.enabled&&saved.has_profile) connect_saved();
        break;
    case CMD_SCAN:
        if(state.enabled && state.state!=NET_CONNECTING && state.state!=NET_GETTING_IP && state.state!=NET_STOPPING) {
            esp_err_t err=start_radio();
            if(err!=ESP_OK) operation_error(NET_ERR_INIT,err);
            else if(waiting_start) scan_after_start=true;
            else start_scan();
        }
        break;
    case EVT_START:
        waiting_start=false;
        if(state.state==NET_STOPPING || !state.enabled) break;
        deadline=0;
        if(state.state==NET_CONNECTING && want_connection) start_connect();
        else if(scan_after_start) { scan_after_start=false; start_scan(); if(!state.scanning) finish_auto_scan(false); }
        break;
    case EVT_STOP:
        started=waiting_start=false;
        if(state.state==NET_STOPPING) execute_after_stop();
        break;
    case EVT_SCAN:
        if(state.scanning && state.state!=NET_STOPPING) collect_scan(m->reason);
        else if(initialized) esp_wifi_clear_ap_list();
        break;
    case EVT_CONNECTED:
        if(state.state==NET_CONNECTING && want_connection && m->ssid_len==target.ssid_len &&
           !memcmp(m->ssid,target.ssid,target.ssid_len)) {
            state.state=NET_GETTING_IP; deadline=now_us()+20000000;
        }
        break;
    case EVT_IP:
        if(state.state==NET_GETTING_IP && want_connection && state.enabled) {
            state.state=NET_CONNECTED; state.error=NET_ERR_NONE; deadline=retry_at=0; retry_attempt=0;
            snprintf(state.ip,sizeof(state.ip),"%s",m->ip);
            if(state.save_warning||!saved.has_profile||memcmp(&saved.profile,&target,sizeof(target))) {
                wifi_saved_t candidate=saved; wifi_saved_remember(&candidate,&target);
                persist_candidate(&candidate); wifi_secret_clear(&candidate,sizeof(candidate));
            }
            auto_mode=true; tried_mask=blocked_mask=0; unblock_at=0;
            target_saved=wifi_saved_find(&saved,target.ssid,target.ssid_len,target.security);
        }
        break;
    case EVT_DISCONNECTED:
        if(state.state==NET_STOPPING || !want_connection || !state.enabled) break;
        if(state.state!=NET_CONNECTING && state.state!=NET_GETTING_IP && state.state!=NET_CONNECTED) break;
        state.ip[0]=0;
        if(m->reason==WIFI_REASON_AUTH_FAIL || m->reason==WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
           m->reason==WIFI_REASON_HANDSHAKE_TIMEOUT) {
            state.error=NET_ERR_AUTH;
            if(auto_mode&&saved.count>1) {
                if(target_saved>=0) blocked_mask|=1u<<target_saved;
                unblock_at=now_us()+300000000; stop_then(AFTER_RETRY);
            } else {
                want_connection=auto_mode=false; wifi_secret_clear(&target,sizeof(target)); stop_then(AFTER_AUTH);
            }
        } else { state.error=m->reason==WIFI_REASON_NO_AP_FOUND?NET_ERR_NOT_FOUND:NET_ERR_NONE; stop_then(AFTER_RETRY); }
        break;
    case EVT_LOST_IP:
        if(state.state==NET_CONNECTED && want_connection) { state.error=NET_ERR_TIMEOUT; stop_then(AFTER_RETRY); }
        break;
    }
}
static void service_tick(void)
{
    int64_t now=now_us();
    if(deadline && now>=deadline) {
        if(waiting_start && state.state!=NET_STOPPING) {
            state.error=NET_ERR_TIMEOUT; stop_then(want_connection?AFTER_RETRY:AFTER_IDLE);
        } else if(state.scanning) {
            /* STOP is a barrier for late SCAN_DONE events before any new scan can start. */
            state.error=NET_ERR_SCAN; stop_then(want_connection?AFTER_RETRY:AFTER_IDLE);
        } else if(state.state==NET_CONNECTING || state.state==NET_GETTING_IP) {
            state.error=NET_ERR_TIMEOUT; stop_then(AFTER_RETRY);
        } else if(state.state==NET_STOPPING) {
            operation_error(NET_ERR_DRIVER,ESP_ERR_TIMEOUT);
            /* A missing STOP acknowledgement makes event ordering unsafe: disable further commands. */
            atomic_store(&faulted,true); state.available=false;
            state.enabled=false;
            if(started) esp_wifi_stop();
            if(initialized) esp_wifi_deinit();
            started=initialized=waiting_start=false;
        }
    }
    if(retry_at&&now>=retry_at&&!state.scanning&&state.state==NET_RETRY&&want_connection) {
        if(auto_mode&&(saved.count>1||!target.ssid_len)) begin_auto(); else stop_then(AFTER_CONNECT);
    }
    if(state.state==NET_CONNECTED && now>=next_rssi) {
        wifi_ap_record_t ap;
        if(esp_wifi_sta_get_ap_info(&ap)==ESP_OK) state.rssi=ap.rssi;
        next_rssi=now+5000000;
    }
}
static void service_startup(void)
{
    state.available=true;
    state.save_warning=wifi_store_load(&saved)!=ESP_OK;
    state.enabled=saved.enabled; state.saved_revision++;
    ESP_LOGI("network","Saved networks=%u enabled=%u",saved.count,state.enabled);
    if(state.enabled) {
        if(saved.has_profile) connect_saved();
        else { after_stop=AFTER_IDLE; execute_after_stop(); }
    }
    publish();
}
static void worker_step(void)
{
        message_t m={0};
        if(xQueueReceive(commands,&m,pdMS_TO_TICKS(100))==pdTRUE) {
            if(!atomic_load(&faulted)) handle(&m);
            if(m.profile) { wifi_secret_clear(m.profile,sizeof(*m.profile)); heap_caps_free(m.profile); }
        }
        if(atomic_exchange(&event_overflow,false)) {
            want_connection=false; state.enabled=false; state.available=false;
            atomic_store(&faulted,true); stop_then(AFTER_OFF);
            state.error=NET_ERR_OVERFLOW;
            /* No further radio activity after an event stream overflow. */
            if(started) { esp_wifi_stop(); started=false; }
            if(initialized) { esp_wifi_deinit(); initialized=false; }
            wifi_secret_clear(&target,sizeof(target)); deadline=retry_at=0;
        }
        if(!atomic_load(&faulted)) service_tick();
        publish();
}
static void worker(void *arg)
{
    service_startup();
    for(;;) {
        if(atomic_load(&stop_requested)) {
            want_connection=false; deadline=retry_at=0;
            stop_result=ESP_OK;
            if(initialized) {
                if(started) { stop_result=esp_wifi_stop(); if(stop_result==ESP_OK) started=false; }
                if(stop_result==ESP_OK) { stop_result=esp_wifi_deinit(); if(stop_result==ESP_OK) initialized=false; }
            }
            /* Keep credentials/settings intact: lifecycle shutdown is not CMD_DISABLE. */
            vTaskSuspend(NULL);
            continue;
        }
        worker_step();
    }
}
esp_err_t wifi_service_init(void)
{
    if(service_task) return atomic_load(&stop_requested)?ESP_ERR_INVALID_STATE:ESP_OK;
    atomic_store(&stop_requested,false); atomic_store(&faulted,false); atomic_store(&event_overflow,false);
    state=(wifi_status_t){0}; published=state; initialized=started=waiting_start=false;
    want_connection=scan_after_start=auto_mode=automatic_scan=false;
    network_count=published_count=published_saved_count=0; deadline=retry_at=next_rssi=0;
    tried_mask=blocked_mask=retry_attempt=0; unblock_at=0; target_saved=-1;
    snapshot_lock=xSemaphoreCreateMutex(); commands=xQueueCreate(32,sizeof(message_t));
    if(!snapshot_lock || !commands) goto no_memory;
    esp_err_t err=esp_netif_init();
    if(err!=ESP_OK) goto failed;
    err=esp_event_loop_create_default();
    if(err!=ESP_OK && err!=ESP_ERR_INVALID_STATE) goto failed;
    /* The convenience creator asserts on allocation failure. Use checked APIs so
     * a failed network initialization does not take down the offline desktop. */
    esp_netif_config_t netif_config=ESP_NETIF_DEFAULT_WIFI_STA();
    sta_netif=esp_netif_new(&netif_config);
    if(!sta_netif) { err=ESP_ERR_NO_MEM; goto failed; }
    err=esp_netif_attach_wifi_station(sta_netif);
    if(err!=ESP_OK) goto failed;
    netif_attached=true;
    err=esp_wifi_set_default_wifi_sta_handlers();
    if(err!=ESP_OK) goto failed;
    err=esp_event_handler_instance_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event_callback,NULL,&wifi_handler);
    if(err!=ESP_OK) goto failed;
    wifi_registered=true;
    err=esp_event_handler_instance_register(IP_EVENT,ESP_EVENT_ANY_ID,event_callback,NULL,&ip_handler);
    if(err!=ESP_OK) {
        goto failed;
    }
    ip_registered=true;
    if(xTaskCreate(worker,"system_wifi",8192,NULL,3,&service_task)==pdPASS) return ESP_OK;
    err=ESP_ERR_NO_MEM;
failed:
    if(wifi_registered) { esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_handler); wifi_registered=false; }
    if(ip_registered) { esp_event_handler_instance_unregister(IP_EVENT,ESP_EVENT_ANY_ID,ip_handler); ip_registered=false; }
    if(sta_netif) {
        if(netif_attached) esp_netif_destroy_default_wifi(sta_netif);
        else esp_netif_destroy(sta_netif);
        sta_netif=NULL; netif_attached=false;
    }
    vQueueDelete(commands); vSemaphoreDelete(snapshot_lock); commands=NULL; snapshot_lock=NULL;
    return err;
no_memory:
    if(commands) vQueueDelete(commands);
    if(snapshot_lock) vSemaphoreDelete(snapshot_lock);
    commands=NULL; snapshot_lock=NULL; return ESP_ERR_NO_MEM;
}

esp_err_t wifi_service_deinit(void) {
 if(!service_task) return ESP_OK;
 atomic_store(&stop_requested,true);
 if(eTaskGetState(service_task)==eSuspended&&stop_result!=ESP_OK)vTaskResume(service_task);
 /* Wait for the worker to stop the driver, without deleting a live task. */
 TickType_t start=xTaskGetTickCount();
 while(eTaskGetState(service_task)!=eSuspended) {
  if(xTaskGetTickCount()-start>pdMS_TO_TICKS(15000)) return ESP_ERR_TIMEOUT;
  vTaskDelay(pdMS_TO_TICKS(10));
 }
 if(stop_result!=ESP_OK) return stop_result;
 if(wifi_registered) { esp_event_handler_instance_unregister(WIFI_EVENT,ESP_EVENT_ANY_ID,wifi_handler); wifi_registered=false; }
 if(ip_registered) { esp_event_handler_instance_unregister(IP_EVENT,ESP_EVENT_ANY_ID,ip_handler); ip_registered=false; }
 service_task_join(&service_task,100);
 if(sta_netif) { if(netif_attached) esp_netif_destroy_default_wifi(sta_netif); else esp_netif_destroy(sta_netif); sta_netif=NULL; }
 netif_attached=false;
 message_t m; while(xQueueReceive(commands,&m,0)==pdTRUE) if(m.profile) { wifi_secret_clear(m.profile,sizeof(*m.profile)); heap_caps_free(m.profile); }
 vQueueDelete(commands); commands=NULL;
 vSemaphoreDelete(snapshot_lock); snapshot_lock=NULL;
 wifi_secret_clear(&target,sizeof(target)); wifi_secret_clear(&saved,sizeof(saved));
 published=(wifi_status_t){0}; return ESP_OK;
}
