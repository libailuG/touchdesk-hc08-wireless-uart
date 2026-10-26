"""Run the production service/navigation transition with mocks."""
from pathlib import Path
import subprocess, tempfile
from host_regression import HEADERS, ROOT
headers=dict(HEADERS)
headers['esp_log.h'] += '\n#define ESP_LOGW(...) ((void)0)\n'
headers['stdatomic.h'] = headers['stdatomic.h'].replace('typedef bool atomic_bool;','typedef unsigned atomic_bool;') + '\nstatic unsigned test_exchange(unsigned *p,unsigned v){unsigned old=*p;*p=v;return old;}\n#define atomic_exchange(p,v) test_exchange(p,v)\n'
headers['esp_timer.h']='static long long now; static long long esp_timer_get_time(void){return now;}'
source=r'''
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include "@ROOT@/main/services/usb_app_mode.c"
static const app_desc_t *opened,*prepared;
static int err;
const app_desc_t app_keysim={.id="keysim"},app_keymouse={.id="bridge"};
const app_desc_t *const app_registry[]={&app_keysim,&app_keymouse};
const size_t app_registry_count=2;
esp_err_t app_services_prepare(const app_desc_t *next){prepared=next;return err;}
void app_request(const app_desc_t *next){opened=next;}
int main(void){
 assert(!usb_app_mode_pending());
 assert(usb_app_mode_prepare(&app_keysim)==ESP_OK&&prepared==&app_keysim);
 err=ESP_ERR_NO_MEM;assert(usb_app_mode_prepare(NULL)==ESP_ERR_NO_MEM&&!prepared);
 assert(!usb_app_mode_console("unknown"));
 assert(usb_app_mode_console("app ui keysim"));usb_app_mode_poll();assert(opened==&app_keysim);
 assert(usb_app_mode_console("app ui bridge"));usb_app_mode_poll();assert(opened==&app_keymouse);
 assert(usb_app_mode_console("keysim ui"));usb_app_mode_poll();assert(opened==&app_keysim);
 assert(usb_app_mode_console("bridge ui"));usb_app_mode_poll();assert(opened==&app_keymouse);
 assert(usb_app_mode_console("system home"));usb_app_mode_poll();assert(!opened);
 puts("PASS service lifecycle delegation, error propagation and deferred UART navigation");
}

'''
with tempfile.TemporaryDirectory(prefix='usb-app-mode-') as d:
 d=Path(d)
 for name,content in headers.items():
  path=d/name;path.parent.mkdir(parents=True,exist_ok=True)
  guard='TEST_'+name.upper().replace('/','_').replace('.','_')
  path.write_text(f'#ifndef {guard}\n#define {guard}\n{content}\n#endif\n',encoding='utf-8')
 src=d/'test.c';src.write_text(source.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8');exe=d/'test.exe'
 cc='C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe'
 subprocess.run([cc,'-I'+str(d),'-I'+str(ROOT/'main/platform'),'-I'+str(ROOT/'main/services'),str(src),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
