"""Compile the production host lifecycle and role handover against failure-injection mocks."""
from pathlib import Path
import tempfile, subprocess
r=Path(__file__).resolve().parents[1]
s=(r/'main/services/music_service.c').read_text(encoding='utf-8-sig')
production=s[s.index('static esp_err_t stop_host_library(void) {'):s.index('static void worker(void *arg) {')]
head=r'''
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#define atomic_store(p,v) (*(p)=(v))
#define atomic_load(p) (*(p))
#define atomic_exchange(p,v) ((*(p)) ? ((*(p))=(v),true) : ((*(p))=(v),false))
#define ESP_OK 0
#define ESP_ERR_TIMEOUT 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define ESP_INTR_FLAG_LEVEL1 1
#define NVS_READWRITE 1
#define portMAX_DELAY 0
#define pdPASS 1
#define pdMS_TO_TICKS(n) (n)
#define MALLOC_CAP_SPIRAM 1
#define MALLOC_CAP_8BIT 2
#define tskNO_AFFINITY 0
#define ESP_LOGI(...)
#define ESP_LOGW(...)
#define ESP_LOGE(...)
typedef int esp_err_t;
typedef int nvs_handle_t;
typedef int StackType_t;
static int host_task_control;
static StackType_t *host_stack;
static void *host_task;
static bool host_installed,uac_installed,host_stopping,host_stopped,accept_devices,host_mode=true;
static int host_stop_result;
static bool disconnected,transfer_error,mic_disconnected,mic_error,mic_stream,stream;
static int microphone=1,speaker=2,guard,commands,queued;
static bool host_hw=true,hid_hw;
static int close_error,uac_error,host_error,start_error,stop_error,task_error,commits;
static bool bridge_hw=true,root_on=true;
static bool role_save_requested;
static bool audio_enabled=true;
static int bridge_stop_error,root_error;
static struct {bool connected,host,active;char device[80];unsigned rate;} state,mic_state;
typedef struct {bool skip_phy_setup;int intr_flags;} usb_host_config_t;
typedef struct {bool create_background_task;int task_priority,stack_size,core_id;void (*callback)(void);} uac_host_driver_config_t;
static void driver_event(void){}
static void host_events(void *a){}
static void vTaskDelay(int n){}
static void xSemaphoreTake(int n,int t){}
static void xSemaphoreGive(int n){}
static void mic_stop(void){mic_stream=false;}
static void stop(void){stream=false;}
static int uac_host_device_close(int h){if(close_error)return close_error;return 0;}
static int uac_host_uninstall(void){assert(!speaker&&!microphone);return 0;}
static int keymouse_host_start(void){bridge_hw=true;return 0;}
static int keymouse_host_prepare_stop(void){assert(root_on);return 0;}
static int keymouse_host_stop(void){assert(!root_on);if(bridge_stop_error)return bridge_stop_error;bridge_hw=false;return 0;}
static int usb_host_lib_set_root_port_power(bool on){if(root_error)return root_error;root_on=on;return 0;}
static int usb_host_lib_unblock(void){assert(!uac_installed&&!bridge_hw);host_hw=false;host_stop_result=0;host_stopped=true;return 0;}
static int usb_host_install(const usb_host_config_t *c){assert(!hid_hw&&!host_hw);if(host_error)return host_error;host_hw=true;root_on=true;return 0;}
static int usb_host_device_free_all(void){return 0;}
static int usb_host_uninstall(void){host_hw=false;return 0;}
static int xTaskCreate(void (*f)(void *),const char *n,int size,void *a,int priority,void *h){return task_error?0:1;}
static void *xTaskCreateStaticPinnedToCore(void (*f)(void *),const char *n,int size,void *a,int priority,void *stack,void *control,int core){return task_error?NULL:(void*)1;}
static void xTaskNotifyGive(void *task){assert(task);}
static void *heap_caps_malloc(unsigned size,int caps){return malloc(size);}
static int uac_host_install(const uac_host_driver_config_t *c){if(uac_error)return uac_error;queued+=2;return 0;}
static int usb_service_stop(void){if(stop_error)return stop_error;hid_hw=false;return 0;}
static int usb_service_start(void){assert(!host_hw);if(start_error)return start_error;hid_hw=true;return 0;}
static void xQueueReset(int q){queued=0;}
static void status(const char *s){}
static const char *esp_err_to_name(int e){return "error";}
static long long esp_timer_get_time(void){return 123;}
static int nvs_open(const char *k,int m,nvs_handle_t *n){*n=1;return 0;}
static int nvs_set_u8(int n,const char *k,bool b){return 0;}
static int nvs_commit(int n){commits++;return 0;}
static void nvs_close(int n){}
'''
tail=r'''
static void audio_initial(void){host_mode=true;host_hw=true;hid_hw=false;host_installed=true;uac_installed=true;microphone=1;speaker=2;accept_devices=true;bridge_hw=true;root_on=true;}
int main(void){
 audio_initial();queued=4;
 assert(change_role(false)==0&&!host_mode&&hid_hw&&!host_hw&&!host_installed&&!uac_installed);
 assert(!microphone&&!speaker&&!queued);
 assert(!bridge_hw&&!root_on);
 assert(role_save_requested);music_role_poll();assert(commits==1&&!role_save_requested);
 assert(change_role(true)==0&&host_mode&&host_hw&&!hid_hw&&host_installed&&uac_installed);
 assert(queued==2); /* New-role connection callbacks must survive handover. */
 assert(!bridge_hw&&root_on);
 int before=commits;assert(change_role(true)==0&&commits==before);
 audio_initial();close_error=7;
 assert(change_role(false)==7&&host_hw&&!hid_hw&&host_mode);close_error=0;
 audio_initial();start_error=8;
 assert(change_role(false)==8&&host_hw&&!hid_hw&&host_mode&&host_installed&&uac_installed);start_error=0;
 assert(change_role(false)==0);host_error=9;
 assert(change_role(true)==9&&hid_hw&&!host_hw&&!host_mode);host_error=0;
 uac_error=10;assert(change_role(true)==10&&hid_hw&&!host_hw&&!host_mode&&!host_installed);uac_error=0;
 task_error=1;host_task=NULL;assert(change_role(true)==ESP_ERR_NO_MEM&&hid_hw&&!host_hw&&!host_installed);task_error=0;
 assert(change_role(true)==0&&host_mode);
 root_error=11;assert(change_role(false)==11&&host_mode&&host_hw&&bridge_hw&&root_on);root_error=0;
 bridge_stop_error=12;assert(change_role(false)==12&&host_mode&&host_hw&&bridge_hw&&root_on);bridge_stop_error=0;
 for(int i=0;i<100;i++){assert(change_role(false)==0);assert(change_role(true)==0);}
 puts("PASS production USB host lifecycle: 100 bidirectional cycles, device close order, controller exclusivity, new events retained, close/start/task failures and restoration");
}
'''
with tempfile.TemporaryDirectory(prefix='usb-live-role-') as d:
 d=Path(d);src=d/'test.c';src.write_text(head+production+tail,encoding='utf-8');exe=d/'test.exe'
 subprocess.run(['C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe',str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
