#include "app_services.h"
#include "serial_uart.h"
#include "serial_ble.h"
#include "music_service.h"
#include "xiaozhi_service.h"
#include "wifi_service.h"
#include "usb_service.h"
#include "keymouse_service.h"
#include "weather.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
static SemaphoreHandle_t lifecycle_lock;
static unsigned active_caps;
esp_err_t app_services_init(void) {
 if(!lifecycle_lock)lifecycle_lock=xSemaphoreCreateMutex();
 return lifecycle_lock?ESP_OK:ESP_ERR_NO_MEM;
}
void app_services_lock(void) { xSemaphoreTake(lifecycle_lock,portMAX_DELAY); }
void app_services_unlock(void) { xSemaphoreGive(lifecycle_lock); }
#define TRY(call) do { esp_err_t e=(call); if(e!=ESP_OK) { ESP_LOGE("services","%s: %s",#call,esp_err_to_name(e));return e; } }while(0)
static esp_err_t prepare(const app_desc_t *next) {
 unsigned caps=next?next->caps:(APP_CAP_NETWORK|APP_CAP_WEATHER);
 /* UI callbacks have finished before entering here. Stop consumers first.
  * On timeout retain their storage and block the new app; never kill a live worker. */
 TRY(serial_uart_stop());
 TRY(serial_ble_stop());
 TRY(xiaozhi_deinit());
 TRY(music_deinit());
 TRY(keymouse_deinit());
 TRY(usb_service_deinit());
 if(!(caps&APP_CAP_NETWORK)) { TRY(weather_stop());TRY(wifi_service_deinit()); }
 active_caps=0;
 if(caps&APP_CAP_NETWORK) {
  esp_err_t network_error=wifi_service_init();
  if(network_error==ESP_OK)network_error=weather_start();
  if(network_error!=ESP_OK){
   if(next)return network_error;
   /* A network allocation failure must leave a usable offline desktop. */
   ESP_LOGW("services","Desktop network unavailable: %s",esp_err_to_name(network_error));
   TRY(weather_stop());TRY(wifi_service_deinit());caps=0;
  }
 }
 if(caps&(APP_CAP_USB_AUDIO|APP_CAP_USB_HOST_HID)) {
  music_configure((caps&APP_CAP_USB_AUDIO)!=0,(caps&APP_CAP_MUSIC)!=0);
  TRY(music_init());TRY(music_set_role(true));
  if(caps&APP_CAP_USB_HOST_HID) { TRY(keymouse_init());TRY(keymouse_host_start()); }
 } else if(caps&APP_CAP_HID) { TRY(usb_service_start()); }
 if(caps&APP_CAP_VOICE) { TRY(xiaozhi_init()); }
 if(caps&APP_CAP_WIRELESS) { TRY(serial_ble_start()); }

 else if(caps&APP_CAP_SERIAL) { TRY(serial_uart_start()); }
 active_caps=caps;
 app_services_status();return ESP_OK;
}
esp_err_t app_services_prepare(const app_desc_t *next) {
 esp_err_t e=app_services_init();if(e!=ESP_OK)return e;
 e=serial_uart_pause();if(e!=ESP_OK)return e;
 app_services_lock();e=prepare(next);app_services_unlock();serial_uart_resume();return e;
}
void app_services_status(void) {
 ESP_LOGI("services","caps=%u internal=%u largest=%u psram=%u min=%u tasks=%u heap_ok=%u",active_caps,
  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT),
  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT),
  (unsigned)uxTaskGetNumberOfTasks(),(unsigned)heap_caps_check_integrity_all(true));
}
