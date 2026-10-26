/* ESP-IDF 5.4.1 S3 USB host FIFO adaptation, scoped to this firmware.
 * The board's headset declares OUT MPS=384 and IN MPS=208. Stock PTX bias
 * allows IN=128, balanced bias allows OUT=128. Repartition only at the HAL's
 * normal initial/reset configuration point, where all channels are inactive.
 * Keep the original HAL initialization/assertions and do not patch the SDK. */
#include "hal/usb_dwc_hal.h"
#include "hal/usb_dwc_ll.h"
#include <assert.h>
void __real_usb_dwc_hal_set_fifo_bias(usb_dwc_hal_context_t *hal,usb_hal_fifo_bias_t bias);
void __wrap_usb_dwc_hal_set_fifo_bias(usb_dwc_hal_context_t *hal,usb_hal_fifo_bias_t bias) {
 __real_usb_dwc_hal_set_fifo_bias(hal,bias);
 if(bias!=USB_HAL_FIFO_BIAS_PTX) return;
 const unsigned rx=64,nptx=16;
 unsigned total=hal->constant_config.fifo_size;
 assert(total>=rx+nptx+96);
 unsigned ptx=total-rx-nptx;
 usb_dwc_ll_grxfsiz_set_fifo_size(hal->dev,rx);
 usb_dwc_ll_gnptxfsiz_set_fifo_size(hal->dev,rx,nptx);
 usb_dwc_ll_hptxfsiz_set_ptx_fifo_size(hal->dev,rx+nptx,ptx);
 usb_dwc_ll_grstctl_flush_nptx_fifo(hal->dev);
 usb_dwc_ll_grstctl_flush_ptx_fifo(hal->dev);
 usb_dwc_ll_grstctl_flush_rx_fifo(hal->dev);
 hal->fifo_config=(usb_dwc_hal_fifo_config_t){.rx_fifo_lines=rx,.nptx_fifo_lines=nptx,.ptx_fifo_lines=ptx};
 /* HCD calls this with its port lock held: never log or block here. */
}
