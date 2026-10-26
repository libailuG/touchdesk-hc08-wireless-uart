"""Exercise production dependency ordering, rollback boundaries and task joining.

Uses deterministic service mocks; does not access hardware or the network.
"""
from pathlib import Path
import subprocess
import tempfile
from host_regression import HEADERS, ROOT

headers=dict(HEADERS)
headers['esp_log.h']+='\n#define ESP_LOGW(...) ((void)0)\n'
source=r'''
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "@ROOT@/main/services/app_services.c"
#include "@ROOT@/main/services/service_task.h"
static char calls[256];static unsigned used;static char fail_at;
static int call(char id) {calls[used++]=id;calls[used]=0;return id==fail_at?ESP_ERR_TIMEOUT:ESP_OK;}
esp_err_t xiaozhi_deinit(void){return call('x');}
esp_err_t music_deinit(void){return call('m');}
esp_err_t keymouse_deinit(void){return call('b');}
esp_err_t usb_service_deinit(void){return call('u');}
esp_err_t weather_stop(void){return call('t');}
esp_err_t wifi_service_deinit(void){return call('w');}
esp_err_t wifi_service_init(void){return call('W');}
esp_err_t weather_start(void){return call('T');}
void music_configure(bool audio,bool catalog){assert(!catalog||audio);}
esp_err_t music_init(void){return call('M');}
esp_err_t music_set_role(bool audio){assert(audio);return call('H');}
esp_err_t keymouse_init(void){return call('B');}
esp_err_t keymouse_host_start(void){return call('K');}
esp_err_t usb_service_start(void){return call('U');}
esp_err_t xiaozhi_init(void){return call('X');}
static bool uart_paused;
esp_err_t serial_uart_pause(void){assert(!uart_paused);uart_paused=true;return ESP_OK;}
void serial_uart_resume(void){assert(uart_paused);uart_paused=false;}
esp_err_t serial_uart_stop(void){assert(uart_paused);return ESP_OK;}
esp_err_t serial_uart_start(void){assert(uart_paused);return call('S');}
esp_err_t serial_ble_stop(void){assert(uart_paused);return call('q');}
esp_err_t serial_ble_start(void){assert(uart_paused);return call('Q');}
static void reset(void){used=0;calls[0]=0;fail_at=0;}
static void expect(const app_desc_t *app,const char *wanted){reset();assert(app_services_prepare(app)==ESP_OK);assert(!strcmp(calls,wanted));assert(!*lifecycle_lock);}
int main(void) {
 assert(app_services_init()==ESP_OK);SemaphoreHandle_t original=lifecycle_lock;
 assert(app_services_init()==ESP_OK&&original==lifecycle_lock);
 const app_desc_t wireless={.caps=APP_CAP_SERIAL|APP_CAP_WIRELESS},serial={.caps=APP_CAP_SERIAL|APP_CAP_FULLSCREEN},key={.caps=APP_CAP_HID},music={.caps=APP_CAP_USB_AUDIO},
  bridge={.caps=APP_CAP_USB_HOST_HID},wifi={.caps=APP_CAP_NETWORK},
  weather={.caps=APP_CAP_NETWORK|APP_CAP_WEATHER},
  chat={.caps=APP_CAP_NETWORK|APP_CAP_USB_AUDIO|APP_CAP_VOICE};
 for(unsigned i=0;i<500;i++) {
  expect(&wireless,"qxmbutwQ");expect(&serial,"qxmbutwS");expect(NULL,"qxmbuWT");expect(&key,"qxmbutwU");expect(&music,"qxmbutwMH");
  expect(&bridge,"qxmbutwMHBK");expect(&wifi,"qxmbuWT");
  expect(&weather,"qxmbuWT");expect(&chat,"qxmbuWTMHX");
 }
 /* Every teardown error prevents subsequent services starting and unlocks UART access. */
 const char *stop="qxmbutw";
 for(unsigned i=0;i<strlen(stop);i++) {
  reset();fail_at=stop[i];assert(app_services_prepare(&bridge)==ESP_ERR_TIMEOUT);
  assert(used==i+1&&!strncmp(calls,stop,used));assert(!*lifecycle_lock);
 }
 /* Partial initialization returns an error; app platform requests desktop rollback. */
 const char *start="WTMHX";
 for(unsigned i=0;i<strlen(start);i++) {
  reset();fail_at=start[i];assert(app_services_prepare(&chat)==ESP_ERR_TIMEOUT);
  assert(calls[used-1]==start[i]&&!strchr(calls,'U'));assert(!*lifecycle_lock);
  expect(NULL,"qxmbuWT");
 }
 /* Desktop stays usable offline when either network startup allocation fails. */
 reset();fail_at='W';assert(app_services_prepare(NULL)==ESP_OK);assert(!strcmp(calls,"qxmbuWtw"));assert(active_caps==0);
 reset();fail_at='T';assert(app_services_prepare(NULL)==ESP_OK);assert(!strcmp(calls,"qxmbuWTtw"));assert(active_caps==0);
 expect(NULL,"qxmbuWT");assert(active_caps==(APP_CAP_NETWORK|APP_CAP_WEATHER));
 TaskHandle_t task=(TaskHandle_t)1;
 task_state=eRunning;int before=deleted_tasks;
 assert(service_task_join(&task,20)==ESP_ERR_TIMEOUT&&task&&deleted_tasks==before);
 task_state=eSuspended;
 assert(service_task_join(&task,20)==ESP_OK&&!task&&deleted_tasks==before+1);
 assert(service_task_join(&task,20)==ESP_OK&&deleted_tasks==before+1);
 puts("PASS repeated production dependency transitions, all teardown/start failure boundaries, network sharing, UART mutex release, live-task timeout and suspended-task deletion");
}
'''
with tempfile.TemporaryDirectory(prefix='service-lifecycle-') as temp:
    temp=Path(temp)
    for name,content in headers.items():
        path=temp/name
        path.parent.mkdir(parents=True,exist_ok=True)
        guard='TEST_'+name.upper().replace('/','_').replace('.','_')
        path.write_text(f'#ifndef {guard}\n#define {guard}\n{content}\n#endif\n',encoding='utf-8')
    src=temp/'test.c'
    src.write_text(source.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8')
    exe=temp/'test.exe'
    cc='C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe'
    subprocess.run([cc,'-I'+str(temp),'-I'+str(ROOT/'main/platform'),'-I'+str(ROOT/'main/services'),'-I'+str(ROOT/'main/apps/weather'),str(src),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
