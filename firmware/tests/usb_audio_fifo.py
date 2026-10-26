from pathlib import Path
import tempfile,subprocess
r=Path(__file__).resolve().parents[1]
headers={'hal/usb_dwc_hal.h':r'''
#pragma once
#include <stdint.h>
typedef enum {USB_HAL_FIFO_BIAS_DEFAULT,USB_HAL_FIFO_BIAS_RX,USB_HAL_FIFO_BIAS_PTX} usb_hal_fifo_bias_t;
typedef struct {uint32_t rx_fifo_lines,nptx_fifo_lines,ptx_fifo_lines;} usb_dwc_hal_fifo_config_t;
typedef struct {int *dev;struct {unsigned fifo_size;} constant_config;usb_dwc_hal_fifo_config_t fifo_config;} usb_dwc_hal_context_t;
''','hal/usb_dwc_ll.h':r'''
#pragma once
extern unsigned rx,np_start,np,pt_start,pt,flushes;
static void usb_dwc_ll_grxfsiz_set_fifo_size(int *d,unsigned n){rx=n;}
static void usb_dwc_ll_gnptxfsiz_set_fifo_size(int *d,unsigned a,unsigned n){np_start=a;np=n;}
static void usb_dwc_ll_hptxfsiz_set_ptx_fifo_size(int *d,unsigned a,unsigned n){pt_start=a;pt=n;}
static void usb_dwc_ll_grstctl_flush_nptx_fifo(int *d){flushes++;}
static void usb_dwc_ll_grstctl_flush_ptx_fifo(int *d){flushes++;}
static void usb_dwc_ll_grstctl_flush_rx_fifo(int *d){flushes++;}
'''}
source=r'''
#include "hal/usb_dwc_hal.h"
#include <assert.h>
#include <stdio.h>
unsigned rx,np_start,np,pt_start,pt,flushes,original_calls;
void __real_usb_dwc_hal_set_fifo_bias(usb_dwc_hal_context_t *h,usb_hal_fifo_bias_t b){original_calls++;h->fifo_config=(usb_dwc_hal_fifo_config_t){34,16,150};}
void __wrap_usb_dwc_hal_set_fifo_bias(usb_dwc_hal_context_t*,usb_hal_fifo_bias_t);
int main(void){
 int dev=0;usb_dwc_hal_context_t h={.dev=&dev,.constant_config={200}};
 __wrap_usb_dwc_hal_set_fifo_bias(&h,USB_HAL_FIFO_BIAS_PTX);
 assert(original_calls==1&&flushes==3&&rx==64&&np==16&&pt==120&&np_start==64&&pt_start==80);
 assert(rx+np+pt==200&&(rx-2)*4>=208&&pt*4>=384&&np*4>=64);
 assert(h.fifo_config.rx_fifo_lines==rx&&h.fifo_config.ptx_fifo_lines==pt);
 __wrap_usb_dwc_hal_set_fifo_bias(&h,USB_HAL_FIFO_BIAS_DEFAULT);
 assert(original_calls==2&&flushes==3&&h.fifo_config.rx_fifo_lines==34);
 puts("PASS production USB FIFO adaptation: original HAL retained, 200-line allocation, non-overlapping addresses, IN208/OUT384/control64 fit, HAL state matches hardware, other biases unchanged");
}
'''
with tempfile.TemporaryDirectory() as d:
 d=Path(d)
 for name,s in headers.items():p=d/name;p.parent.mkdir(parents=True,exist_ok=True);p.write_text(s,encoding='utf-8')
 (d/'test.c').write_text(source,encoding='utf-8');exe=d/'test.exe'
 subprocess.run(['C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe','-I'+str(d),str(d/'test.c'),str(r/'components/board/usb_host_fifo.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
