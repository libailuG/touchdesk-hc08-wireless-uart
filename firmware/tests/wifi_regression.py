"""Compile production Wi-Fi policy/store/worker with deterministic ESP-IDF mocks.
No network, USB, serial or real credentials are accessed.
"""
from pathlib import Path
import argparse
import subprocess
import tempfile
from host_regression import HEADERS, ROOT

MOCKS = dict(HEADERS)
MOCKS['esp_err.h'] += '\n#define ESP_ERR_INVALID_ARG 5\n#define ESP_ERR_TIMEOUT 6\n'
MOCKS['stdatomic.h'] += '''
static bool exchange_bool(bool *p,bool v) { bool old=*p; *p=v; return old; }
#define atomic_exchange(p,v) exchange_bool(p,v)
'''
MOCKS['freertos/queue.h'] = '''#include "FreeRTOS.h"
typedef struct { unsigned count,size; unsigned char data[32][256]; } mock_queue_t;
typedef mock_queue_t *QueueHandle_t;
static QueueHandle_t xQueueCreate(unsigned n,unsigned size) {
    assert(n<=32 && size<=256); QueueHandle_t q=calloc(1,sizeof(*q)); q->size=size; return q;
}
static int xQueueSend(QueueHandle_t q,const void *p,unsigned t) {
    if(q->count==32) return 0; memcpy(q->data[q->count++],p,q->size); return pdTRUE;
}
static int xQueueReceive(QueueHandle_t q,void *p,unsigned t) {
    if(!q->count) return 0; memcpy(p,q->data[0],q->size); q->count--;
    memmove(q->data,q->data+1,q->count*sizeof(q->data[0])); return pdTRUE;
}
static void vQueueDelete(QueueHandle_t q) { free(q); }
'''
MOCKS['esp_timer.h'] = '''#include <stdint.h>
#include "freertos/task.h"
static int64_t esp_timer_get_time(void) { return (int64_t)clock_ms*1000; }
'''
MOCKS['nvs.h'] += '''
#include <stddef.h>
int nvs_get_blob(nvs_handle_t,const char *,void *,size_t *);
int nvs_set_blob(nvs_handle_t,const char *,const void *,size_t);
int nvs_erase_key(nvs_handle_t,const char *);
'''
MOCKS['esp_event.h'] = '''#include "esp_err.h"
#include <stdint.h>
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
#define WIFI_EVENT ((const char*)1)
#define IP_EVENT ((const char*)2)
#define ESP_EVENT_ANY_ID -1
static int esp_event_loop_create_default(void) { return ESP_OK; }
static int esp_event_handler_instance_register(esp_event_base_t b,int id,
    void (*cb)(void*,esp_event_base_t,int32_t,void*),void *arg,esp_event_handler_instance_t *h) { *h=(void*)1; return ESP_OK; }
static int esp_event_handler_instance_unregister(esp_event_base_t b,int id,esp_event_handler_instance_t h) { return ESP_OK; }
'''
MOCKS['esp_netif.h'] = '''#include "esp_err.h"
#include <stdint.h>
typedef struct { int id; } esp_netif_t;
typedef struct { int id; } esp_netif_config_t;
#define ESP_NETIF_DEFAULT_WIFI_STA() ((esp_netif_config_t){1})
typedef struct { unsigned addr; } mock_ip_t;
typedef struct { esp_netif_t *esp_netif; struct { mock_ip_t ip; } ip_info; } ip_event_got_ip_t;
static esp_netif_t mock_netif={1};
static bool fail_netif;
#define IP_EVENT_STA_GOT_IP 10
#define IP_EVENT_STA_LOST_IP 11
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) 192u,168u,1u,99u
static int esp_netif_init(void) { return ESP_OK; }
static esp_netif_t *esp_netif_create_default_wifi_sta(void) { return &mock_netif; }
static esp_netif_t *esp_netif_new(const esp_netif_config_t *c) { return fail_netif ? NULL : &mock_netif; }
static void esp_netif_destroy(esp_netif_t *n) {}
static void esp_netif_destroy_default_wifi(esp_netif_t *n) {}
'''
MOCKS['esp_wifi_default.h'] = '''#include "esp_netif.h"
static int esp_netif_attach_wifi_station(esp_netif_t *n) { return ESP_OK; }
static int esp_wifi_set_default_wifi_sta_handlers(void) { return ESP_OK; }
'''
MOCKS['esp_wifi.h'] = '''#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef enum { WIFI_AUTH_OPEN,WIFI_AUTH_WPA2_PSK,WIFI_AUTH_WPA_WPA2_PSK,
    WIFI_AUTH_WPA3_PSK,WIFI_AUTH_WPA2_WPA3_PSK,WIFI_AUTH_WPA2_ENTERPRISE } wifi_auth_mode_t;
typedef struct { int nvs_enable; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){1})
typedef struct {
    uint8_t ssid[32],password[64]; int scan_method,sort_method;
    struct { wifi_auth_mode_t authmode; } threshold;
    struct { bool capable,required; } pmf_cfg; int sae_pwe_h2e;
} mock_sta_config_t;
typedef struct { mock_sta_config_t sta; } wifi_config_t;
typedef struct { bool show_hidden; } wifi_scan_config_t;
typedef struct { uint8_t ssid[33]; wifi_auth_mode_t authmode; int8_t rssi; } wifi_ap_record_t;
typedef struct { uint32_t status; } wifi_event_sta_scan_done_t;
typedef struct { uint8_t ssid[32],ssid_len; } wifi_event_sta_connected_t;
typedef struct { uint16_t reason; } wifi_event_sta_disconnected_t;
#define WIFI_EVENT_STA_START 1
#define WIFI_EVENT_STA_STOP 2
#define WIFI_EVENT_SCAN_DONE 3
#define WIFI_EVENT_STA_CONNECTED 4
#define WIFI_EVENT_STA_DISCONNECTED 5
#define WIFI_REASON_AUTH_FAIL 202
#define WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT 15
#define WIFI_REASON_HANDSHAKE_TIMEOUT 204
#define WIFI_REASON_NO_AP_FOUND 201
#define WIFI_MODE_STA 1
#define WIFI_STORAGE_RAM 1
#define WIFI_IF_STA 1
#define WIFI_ALL_CHANNEL_SCAN 1
#define WIFI_CONNECT_AP_BY_SIGNAL 1
#define WPA3_SAE_PWE_BOTH 3
int esp_wifi_init(const wifi_init_config_t *);
int esp_wifi_deinit(void);
int esp_wifi_set_storage(int);
int esp_wifi_set_mode(int);
int esp_wifi_start(void);
int esp_wifi_stop(void);
int esp_wifi_connect(void);
int esp_wifi_scan_start(const wifi_scan_config_t *,bool);
int esp_wifi_scan_stop(void);
int esp_wifi_clear_ap_list(void);
int esp_wifi_scan_get_ap_records(uint16_t *,wifi_ap_record_t *);
int esp_wifi_set_config(int,const wifi_config_t *);
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *);
'''

TEST = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "@ROOT@/main/services/wifi_policy.c"
#include "@ROOT@/main/services/wifi_store.c"
#include "@ROOT@/main/services/wifi_service.c"
static unsigned driver_inits,driver_starts,driver_stops,driver_connects,clears,commits;
static int driver_error,scan_error,commit_error;
static bool stop_ack=true,have_blob,staged_valid;
static wifi_config_t applied;
static wifi_saved_t disk,staged;
static legacy_saved_t legacy_disk;
static bool have_legacy,erase_legacy;
static unsigned scan_scene;
int nvs_open(const char *name,int mode,nvs_handle_t *h) { assert(!strcmp(name,"network")); *h=1; return ESP_OK; }
void nvs_close(nvs_handle_t h) { staged_valid=erase_legacy=false; }
int nvs_get_blob(nvs_handle_t h,const char *key,void *out,size_t *n) {
    if(!strcmp(key,"config_v1")) { if(!have_legacy) return ESP_ERR_NVS_NOT_FOUND; assert(*n>=sizeof(legacy_disk)); *n=sizeof(legacy_disk); memcpy(out,&legacy_disk,*n); return ESP_OK; }
    assert(!strcmp(key,"config_v2")); if(!have_blob) return ESP_ERR_NVS_NOT_FOUND;
    assert(*n>=sizeof(disk)); *n=sizeof(disk); memcpy(out,&disk,sizeof(disk)); return ESP_OK;
}
int nvs_erase_key(nvs_handle_t h,const char *key) { assert(!strcmp(key,"config_v1")); erase_legacy=true; return have_legacy?ESP_OK:ESP_ERR_NVS_NOT_FOUND; }
int nvs_set_blob(nvs_handle_t h,const char *key,const void *p,size_t n) {
    assert(!strcmp(key,"config_v2") && n==sizeof(staged)); memcpy(&staged,p,n); staged_valid=true; return ESP_OK;
}
int nvs_commit(nvs_handle_t h) {
    if(commit_error) return commit_error;
    if(staged_valid) { disk=staged; have_blob=true; commits++; if(erase_legacy) { have_legacy=false; wifi_secret_clear(&legacy_disk,sizeof(legacy_disk)); } } return ESP_OK;
}
int esp_wifi_init(const wifi_init_config_t *cfg) { assert(cfg->nvs_enable==0); driver_inits++; return driver_error; }
int esp_wifi_deinit(void) { memset(&applied,0,sizeof(applied)); return ESP_OK; }
int esp_wifi_set_storage(int s) { assert(s==WIFI_STORAGE_RAM); return ESP_OK; }
int esp_wifi_set_mode(int m) { assert(m==WIFI_MODE_STA); return ESP_OK; }
int esp_wifi_start(void) {
    driver_starts++; event_callback(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL); return ESP_OK;
}
int esp_wifi_stop(void) {
    driver_stops++;
    /* Old disconnect/IP events can arrive before STOP; neither may activate the new target. */
    wifi_event_sta_disconnected_t d={201}; event_callback(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&d);
    ip_event_got_ip_t ip={.esp_netif=sta_netif}; event_callback(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&ip);
    if(stop_ack) event_callback(NULL,WIFI_EVENT,WIFI_EVENT_STA_STOP,NULL);
    return ESP_OK;
}
int esp_wifi_connect(void) { driver_connects++; return ESP_OK; }
int esp_wifi_scan_start(const wifi_scan_config_t *cfg,bool block) { assert(!block && !cfg->show_hidden); return scan_error; }
int esp_wifi_scan_stop(void) { return ESP_OK; }
int esp_wifi_clear_ap_list(void) { clears++; return ESP_OK; }
int esp_wifi_scan_get_ap_records(uint16_t *n,wifi_ap_record_t *out) {
    if(scan_scene) {
        assert(*n>=2); *n=scan_scene==1?2:1;
        strcpy((char*)out[0].ssid,scan_scene==3?"Office":"Home");
        out[0].authmode=scan_scene==4?WIFI_AUTH_OPEN:WIFI_AUTH_WPA2_PSK; out[0].rssi=-65;
        if(*n==2) { strcpy((char*)out[1].ssid,"Office"); out[1].authmode=WIFI_AUTH_WPA2_PSK; out[1].rssi=-35; }
        return ESP_OK;
    }
    assert(*n>=4); *n=4;
    strcpy((char*)out[0].ssid,"Home"); out[0].authmode=WIFI_AUTH_WPA2_PSK; out[0].rssi=-40;
    strcpy((char*)out[1].ssid,"Home"); out[1].authmode=WIFI_AUTH_WPA2_PSK; out[1].rssi=-60;
    strcpy((char*)out[2].ssid,"Home"); out[2].authmode=WIFI_AUTH_OPEN; out[2].rssi=-50;
    strcpy((char*)out[3].ssid,"Office"); out[3].authmode=WIFI_AUTH_WPA2_ENTERPRISE; out[3].rssi=-70;
    return ESP_OK;
}
int esp_wifi_set_config(int iface,const wifi_config_t *cfg) { applied=*cfg; return ESP_OK; }
int esp_wifi_sta_get_ap_info(wifi_ap_record_t *out) { memset(out,0,sizeof(*out)); out->rssi=-45; return ESP_OK; }
static void pump(void) {
    unsigned guard=0;
    while(commands->count) { assert(guard++<100); worker_step(); }
}
static void connected(const char *name) {
    wifi_event_sta_connected_t e={0}; e.ssid_len=strlen(name); memcpy(e.ssid,name,e.ssid_len);
    event_callback(NULL,WIFI_EVENT,WIFI_EVENT_STA_CONNECTED,&e); pump();
}
static void got_ip(void) { ip_event_got_ip_t e={.esp_netif=sta_netif}; event_callback(NULL,IP_EVENT,IP_EVENT_STA_GOT_IP,&e); pump(); }
static void dropped(unsigned reason) { wifi_event_sta_disconnected_t e={reason}; event_callback(NULL,WIFI_EVENT,WIFI_EVENT_STA_DISCONNECTED,&e); pump(); }
static wifi_network_t network(const char *name,unsigned security) {
    wifi_network_t n={0}; n.ssid_len=strlen(name); memcpy(n.ssid,name,n.ssid_len); n.security=security;
    wifi_display_name(n.ssid,n.ssid_len,n.name,sizeof(n.name)); return n;
}
int main(void) {
    /* Policy validates byte limits, printable ASCII, and does not mutate SSIDs. */
    wifi_profile_t p={0}; memcpy(p.ssid,"x",1); p.ssid_len=1; p.security=NET_WPA2;
    strcpy(p.password,"short"); assert(!wifi_profile_valid(&p));
    strcpy(p.password,"test pass+1"); assert(wifi_profile_valid(&p));
    memset(p.password,'x',63); p.password[63]=0; assert(wifi_profile_valid(&p));
    memset(p.password,'x',64); assert(!wifi_profile_valid(&p));
    memset(p.password,0,64); p.security=NET_OPEN; assert(wifi_profile_valid(&p));
    p.security=NET_UNSUPPORTED; assert(!wifi_profile_valid(&p));
    p.security=NET_OPEN; p.ssid_len=32; memset(p.ssid,'x',32); assert(wifi_profile_valid(&p));
    p.ssid_len=33; assert(!wifi_profile_valid(&p));
    char name[100]; uint8_t invalid[]={0xff,'A',0xe0,0x80,0x80,0,'B'};
    wifi_display_name(invalid,sizeof(invalid),name,sizeof(name)); assert(!strcmp(name,"?A????B"));
    wifi_display_name((const uint8_t*)"家庭网络",12,name,sizeof(name)); assert(!strcmp(name,"家庭网络"));
    assert(wifi_retry_delay(0)==2 && wifi_retry_delay(1)==5 && wifi_retry_delay(100)==60);
    wifi_secret_clear(&p,sizeof(p)); for(size_t i=0;i<sizeof(p);i++) assert(!((unsigned char*)&p)[i]);

    wifi_saved_t bank={.version=2};
    for(unsigned i=0;i<10;i++) {
        wifi_profile_t item={.ssid_len=2,.security=NET_WPA2}; item.ssid[0]='N'; item.ssid[1]='0'+i; strcpy(item.password,"test pass+1");
        wifi_saved_remember(&bank,&item);
    }
    assert(bank.count==8&&bank.profile.ssid[1]=='9'&&wifi_saved_profile(&bank,7)->ssid[1]=='2');
    wifi_profile_t update=*wifi_saved_profile(&bank,3); strcpy(update.password,"updated+1"); wifi_saved_remember(&bank,&update);
    assert(bank.count==8&&!strcmp(bank.profile.password,"updated+1"));
    wifi_saved_remove(&bank,4); assert(bank.count==7);
    wifi_saved_t modes={.version=2};
    wifi_profile_t mixed=update; mixed.security=NET_WPA2; wifi_saved_remember(&modes,&mixed);
    mixed.security=NET_WPA3; wifi_saved_remember(&modes,&mixed); assert(modes.count==2);
    mixed.security=NET_WPA2_WPA3; wifi_saved_remember(&modes,&mixed); assert(modes.count==1&&valid(&modes));
    legacy_disk=(legacy_saved_t){.version=1,.enabled=0,.has_profile=1,.profile=update}; have_legacy=true;
    wifi_saved_t migrated; assert(wifi_store_load(&migrated)==ESP_OK&&migrated.count==1&&migrated.version==2);
    assert(!memcmp(&migrated.profile,&update,sizeof(update)));
    commit_error=9; assert(wifi_store_save(&migrated)==9&&have_legacy&&!have_blob);
    commit_error=0; assert(wifi_store_save(&migrated)==ESP_OK&&!have_legacy&&disk.count==1);
    have_blob=false; wifi_secret_clear(&disk,sizeof(disk));
    puts("PASS multiple-profile storage: legacy migration, atomic failure, dedup/update, 8-network LRU limit and individual deletion");
    fail_netif=true; assert(wifi_service_init()==ESP_ERR_NO_MEM && !commands && !snapshot_lock);
    fail_netif=false; fail_task=true;
    assert(wifi_service_init()==ESP_ERR_NO_MEM && !commands && !snapshot_lock && !sta_netif);
    fail_task=false;
    assert(wifi_service_init()==ESP_OK); service_startup(); pump();
    assert(state.available && !state.enabled && driver_inits==0); /* First boot stays offline. */
    assert(wifi_service_enable(true)); pump();
    assert(state.enabled && state.state==NET_IDLE && driver_connects==0 && disk.enabled==1);
    assert(wifi_service_scan()); pump(); assert(state.scanning);
    wifi_event_sta_scan_done_t scan_done={0}; event_callback(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&scan_done); pump();
    assert(!state.scanning && network_count==3 && allocations==0);
    assert(networks[0].rssi==-40 && networks[1].security==NET_OPEN && networks[2].security==NET_UNSUPPORTED);

    wifi_network_t home=network("Home",NET_WPA2);
    assert(!wifi_service_connect(&home,"short") && allocations==0);
    assert(wifi_service_connect(&home,"test pass+1")); pump();
    assert(state.state==NET_CONNECTING && allocations==0 && !disk.has_profile);
    assert(applied.sta.threshold.authmode==WIFI_AUTH_WPA2_PSK);
    assert(!memcmp(applied.sta.password,"test pass+1",11));
    connected("Other"); assert(state.state==NET_CONNECTING); /* Mismatched old AP event. */
    got_ip(); assert(state.state==NET_CONNECTING && !disk.has_profile); /* IP before association ignored. */
    connected("Home"); assert(state.state==NET_GETTING_IP && !disk.has_profile);
    got_ip(); assert(state.state==NET_CONNECTED && disk.has_profile && !strcmp(state.ip,"192.168.1.99"));
    unsigned old_commits=commits;
    got_ip(); assert(commits==old_commits);
    dropped(200); assert(state.state==NET_RETRY && retry_at-now_us()==2000000);
    clock_ms+=2000; worker_step(); pump(); assert(state.state==NET_CONNECTING);
    dropped(201); assert(state.state==NET_RETRY && retry_at-now_us()==5000000);
    clock_ms+=5000; worker_step(); pump(); connected("Home"); got_ip(); assert(state.state==NET_CONNECTED);
    unsigned before=driver_connects;
    assert(wifi_service_disconnect()); pump(); clock_ms+=600000; worker_step(); pump();
    assert(state.state==NET_IDLE && driver_connects==before && applied.sta.password[0]==0 && disk.has_profile);

    wifi_network_t wrong=network("Wrong",NET_WPA3);
    assert(wifi_service_connect(&wrong,"wrongpass")); pump();
    assert(applied.sta.pmf_cfg.required && applied.sta.threshold.authmode==WIFI_AUTH_WPA3_PSK);
    dropped(WIFI_REASON_AUTH_FAIL); assert(state.state==NET_ERROR && state.error==NET_ERR_AUTH && !want_connection);
    assert(!strcmp((char*)disk.profile.ssid,"Home")); /* Failed credentials never replace saved profile. */
    assert(target.password[0]==0 && applied.sta.password[0]==0);
    assert(wifi_service_connect_saved()); pump(); connected("Home"); got_ip();
    assert(state.state==NET_CONNECTED);
    commit_error=9; assert(wifi_service_forget()); pump();
    assert(state.save_warning && saved.has_profile && disk.has_profile && !want_connection);
    commit_error=0; assert(wifi_service_forget()); pump();
    assert(!state.save_warning && !saved.has_profile && !disk.has_profile);
    for(size_t i=0;i<sizeof(disk.profile);i++) assert(!((unsigned char*)&disk.profile)[i]);

    wifi_network_t open=network("Guest",NET_OPEN);
    assert(wifi_service_connect(&open,"")); pump(); connected("Guest"); got_ip();
    assert(state.state==NET_CONNECTED && saved.profile.security==NET_OPEN);
    assert(wifi_service_enable(false)); pump();
    assert(!state.enabled && state.state==NET_OFF && !initialized && !disk.enabled);
    unsigned before_boot=driver_inits;
    memset(&state,0,sizeof(state)); service_startup(); pump();
    assert(!state.enabled && driver_inits==before_boot); /* Disabled persists across reboot. */
    wifi_saved_t config=disk; config.enabled=1; assert(wifi_store_save(&config)==ESP_OK);
    memset(&state,0,sizeof(state)); service_startup(); pump(); /* Simulated reboot from persisted profile. */
    assert(state.enabled && state.state==NET_CONNECTING && want_connection);
    connected("Guest"); got_ip();
    assert(wifi_service_enable(false)); pump();
    driver_error=7; assert(wifi_service_enable(true)); pump();
    assert(state.state==NET_ERROR && state.error==NET_ERR_INIT && !initialized);
    driver_error=0; assert(wifi_service_enable(true)); pump();
    assert(state.state==NET_CONNECTING); clock_ms+=30001; worker_step(); pump();
    assert(state.state==NET_RETRY); /* Connection timeout is cancellable and backs off. */
    assert(wifi_service_disconnect()); pump();
    scan_error=8; assert(wifi_service_scan()); pump(); assert(state.error==NET_ERR_SCAN);
    scan_error=0; assert(wifi_service_scan()); pump(); clock_ms+=15001; worker_step(); pump();
    assert(!state.scanning && state.error==NET_ERR_SCAN && clears>0);

    assert(wifi_service_disconnect()); pump();
    assert(wifi_service_connect(&home,"test pass+1")); pump(); connected("Home"); got_ip();
    wifi_network_t office=network("Office",NET_WPA2);
    assert(wifi_service_connect(&office,"office pass")); pump(); connected("Office"); got_ip();
    assert(saved.count==3&&disk.count==3); /* Guest, Home and Office coexist. */
    scan_scene=2; dropped(WIFI_REASON_NO_AP_FOUND); assert(state.state==NET_RETRY);
    clock_ms+=2000; worker_step(); pump(); assert(state.scanning&&automatic_scan);
    event_callback(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&scan_done); pump();
    assert(state.state==NET_CONNECTING&&!memcmp(target.ssid,"Home",4)); connected("Home"); got_ip();
    scan_scene=3; dropped(WIFI_REASON_NO_AP_FOUND); clock_ms+=2000; worker_step(); pump();
    event_callback(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&scan_done); pump();
    assert(!memcmp(target.ssid,"Office",6)); connected("Office"); got_ip();
    scan_scene=1; dropped(WIFI_REASON_AUTH_FAIL); assert(want_connection&&blocked_mask);
    clock_ms+=2000; worker_step(); pump(); event_callback(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&scan_done); pump();
    assert(!memcmp(target.ssid,"Home",4)); connected("Home"); got_ip(); assert(saved.count==3);
    /* Forget another saved network without dropping the current connection. */
    assert(wifi_service_forget_known(&open)); pump(); assert(saved.count==2&&state.state==NET_CONNECTED);
    unsigned saved_count=saved.count;
    assert(wifi_service_connect(&home,"corrected pass")); pump(); connected("Home"); got_ip();
    assert(saved.count==saved_count&&!strcmp(saved.profile.password,"corrected pass"));
    commit_error=9; wifi_network_t temp=network("Temporary",NET_WPA2);
    assert(wifi_service_connect(&temp,"temporary pass")); pump(); connected("Temporary"); got_ip();
    assert(state.save_warning&&saved.count==saved_count&&wifi_saved_find(&saved,temp.ssid,temp.ssid_len,temp.security)<0);
    commit_error=0; assert(wifi_service_connect_known(&home)); pump(); connected("Home"); got_ip(); assert(!state.save_warning);
    scan_scene=1; memset(&state,0,sizeof(state)); service_startup(); pump(); assert(state.scanning);
    event_callback(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&scan_done); pump();
    assert(!memcmp(target.ssid,"Office",6)); connected("Office"); got_ip();
    /* Never use a protected network's password on an open AP with the same SSID. */
    scan_scene=4; dropped(WIFI_REASON_NO_AP_FOUND); clock_ms+=2000; worker_step(); pump();
    unsigned connections_before_scan=driver_connects;
    event_callback(NULL,WIFI_EVENT,WIFI_EVENT_SCAN_DONE,&scan_done); pump();
    assert(state.state==NET_RETRY&&driver_connects==connections_before_scan);
    assert(wifi_service_disconnect()); pump(); clock_ms+=600000; worker_step(); pump();
    assert(state.state==NET_IDLE&&driver_connects==connections_before_scan);
    config=disk;
    puts("PASS multi-location worker: Home/Office failover, auth fallback, per-network forget, password update, commit failure, reboot selection, security match and manual disconnect");
    /* Future schema is preserved; it cannot be silently overwritten. */
    disk.version=99; wifi_saved_t loaded;
    assert(wifi_store_load(&loaded)==ESP_ERR_NOT_SUPPORTED && !loaded.has_profile);
    assert(wifi_store_save(&config)==ESP_ERR_INVALID_STATE && disk.version==99);
    disk=config; assert(wifi_store_load(&loaded)==ESP_OK);
    /* A missing STOP acknowledgement fails the service offline without blocking UI. */
    stop_ack=false; assert(wifi_service_disconnect()); pump(); clock_ms+=5001; worker_step(); pump();
    assert(atomic_load(&faulted) && !state.available && !initialized);
    assert(!wifi_service_enable(true));
    /* Reset the mock fault to represent a fresh boot for the independent overflow case. */
    atomic_store(&faulted,false); stop_ack=true; state.available=true;
    /* Fill callback queue: event loss fails offline rather than replaying commands. */
    for(int i=0;i<33;i++) event_callback(NULL,WIFI_EVENT,WIFI_EVENT_STA_START,NULL);
    assert(atomic_load(&event_overflow)); pump();
    assert(atomic_load(&faulted) && !state.available && !initialized);
    assert(!wifi_service_enable(true) && !wifi_service_connect(&home,"test pass+1"));
    assert(allocations==0);
    puts("PASS WIFI: validation, UTF-8, lazy startup, scan dedup, stop barrier, stale events, IP gating, persistence, authentication, retry, disconnect, forget, reboot, failures, timeouts, overflow, credential cleanup");
    return 0;
}
'''

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--cc',default='cc')
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='touchdesk-wifi-') as temp:
        temp=Path(temp)
        for name,text in MOCKS.items():
            path=temp/name; path.parent.mkdir(parents=True,exist_ok=True)
            guard='TD_WIFI_TEST_'+name.upper().replace('/','_').replace('.','_')
            path.write_text(f'#ifndef {guard}\n#define {guard}\n{text}\n#endif\n',encoding='utf-8')
        src=temp/'test.c'; src.write_text(TEST.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8')
        exe=temp/'test.exe'
        subprocess.run([args.cc,'-I'+str(temp),'-I'+str(ROOT/'main/services'),str(src),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)

if __name__=='__main__':
    main()
