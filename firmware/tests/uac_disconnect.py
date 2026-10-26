"""Compile the real local UAC disconnect handler against owner/lifetime interleavings."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'components/usb_host_uac/uac_host.c').read_text(encoding='utf-8')
a=s.index('static esp_err_t _uac_host_device_disconnected(');b=s.index('\n/**',a)
head=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define ESP_OK 0
#define UAC_ENTER_CRITICAL() do{assert(!locked);locked=true;}while(0)
#define UAC_EXIT_CRITICAL() do{assert(locked);locked=false;}while(0)
#define FLAG_INTERFACE_WAIT_USER_DELETE (1u<<16)
#define FLAG_INTERFACE_DISCONNECT_NOTIFIED (1u<<17)
#define UAC_RETURN_ON_ERROR(exp,msg) do{int e=(exp);if(e)return e;}while(0)
#define ESP_LOGW(...)
#define STAILQ_FOREACH(v,h,f) for((v)=(h)->first;(v);(v)=(v)->next)
typedef int esp_err_t;
typedef void *usb_device_handle_t;
typedef struct {unsigned addr,remaining;bool live;} uac_device_t;
typedef struct iface {struct iface *next;uac_device_t *parent;unsigned flags,closes,notifies;} uac_iface_t;
static struct {struct {uac_iface_t *first;} uac_ifaces_tailq;} driver,*s_uac_driver=&driver;
static bool locked,immediate,close_error;static unsigned operations;
static uac_device_t *get_uac_device_by_handle(void *h){uac_device_t *d=h;return d&&d->live?d:NULL;}
static void owner_delete(uac_iface_t *f){
 uac_iface_t **p=&driver.uac_ifaces_tailq.first;while(*p&&*p!=f)p=&(*p)->next;
 assert(*p==f);*p=f->next;
 if(f->parent&&!--f->parent->remaining){f->parent->live=false;f->parent->addr=0;}
 f->parent=NULL;
}
static int uac_host_device_close(uac_iface_t *f){assert(!locked&&++operations<16);assert(f->flags&FLAG_INTERFACE_WAIT_USER_DELETE);f->closes++;return close_error?1:0;}
static int uac_host_interface_shutdown(uac_iface_t *f){assert(!locked);f->flags&=~FLAG_INTERFACE_WAIT_USER_DELETE;assert(++f->notifies==1);if(immediate)owner_delete(f);return 0;}
'''
tail=r'''
int main(void){
 for(unsigned sync=0;sync<2;sync++)for(unsigned err=0;err<2;err++){
  uac_device_t a={4,2,true},b={9,1,true};
  uac_iface_t two={NULL,&a},empty={&two,NULL},one={&empty,&a},other={&one,&b};
  uac_iface_t deleting={&other,(uac_device_t*)1,FLAG_INTERFACE_DISCONNECT_NOTIFIED};
  driver.uac_ifaces_tailq.first=&deleting;immediate=sync;close_error=err;operations=0;
  assert(_uac_host_device_disconnected(&a)==0);
  assert(!other.closes&&!other.notifies&&one.closes==1&&one.notifies==1&&two.closes==1&&two.notifies==1);
  assert(_uac_host_device_disconnected(&a)==0); /* Queued duplicate event is harmless. */
  assert(operations==2&&!locked&&driver.uac_ifaces_tailq.first==&deleting);
  if(!sync){owner_delete(&one);owner_delete(&two);}
  assert(!a.live&&b.live&&other.parent==&b);
 }
 assert(_uac_host_device_disconnected(NULL)==0);
 puts("PASS real UAC disconnect: deferred/immediate owner deletion, unrelated list head, null parent, one notification per interface, duplicate events, close error, no post-callback device access");
}
'''
with tempfile.TemporaryDirectory(prefix='uac-disconnect-') as d:
 d=Path(d);src=d/'test.c';src.write_text(head+s[a:b]+tail,encoding='utf-8');exe=d/'test.exe'
 cc='C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe'
 subprocess.run([cc,str(src),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
