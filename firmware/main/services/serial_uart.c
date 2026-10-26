#include "serial_uart.h"
#include "driver/uart.h"
#include "esp_rom_uart.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_intr_alloc.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
typedef struct { serial_parser_t parser;serial_uart_snapshot_t snapshot;serial_history_t history; } session_t;
static session_t *session;
static SemaphoreHandle_t data_lock,log_lock;
static QueueHandle_t uart_events;
static atomic_bool paused,acknowledged,binary;
static vprintf_like_t original_log;
static int log_output(const char *fmt,va_list args) {
 if(atomic_load(&binary))return 0;
 xSemaphoreTake(log_lock,portMAX_DELAY);
 int n=atomic_load(&binary)?0:original_log(fmt,args);
 xSemaphoreGive(log_lock);return n;
}
esp_err_t serial_uart_console_init(void) {
 if(log_lock)return ESP_OK;
 log_lock=xSemaphoreCreateMutex();if(!log_lock)return ESP_ERR_NO_MEM;
 setvbuf(stdin,NULL,_IONBF,0);
 original_log=esp_log_set_vprintf(log_output);return ESP_OK;
}
bool serial_uart_active(void) { return atomic_load(&binary); }
esp_err_t serial_uart_pause(void) {
 atomic_store(&acknowledged,false);atomic_store(&paused,true);TickType_t start=xTaskGetTickCount();
 while(!atomic_load(&acknowledged)) {
  if(xTaskGetTickCount()-start>pdMS_TO_TICKS(1500)){atomic_store(&paused,false);return ESP_ERR_TIMEOUT;}
  vTaskDelay(pdMS_TO_TICKS(1));
 }
 return ESP_OK;
}
void serial_uart_resume(void) { atomic_store(&paused,false); }
static void received(const serial_frame_t *f,void *ctx) {
 session_t *s=ctx;s->snapshot.last=*f;s->snapshot.rx++;s->snapshot.has_frame=true;serial_history_push_timed(&s->history,f,esp_timer_get_time());
}
bool serial_uart_console_step(void) {
 if(atomic_load(&paused)){atomic_store(&acknowledged,true);vTaskDelay(pdMS_TO_TICKS(1));return true;}
 atomic_store(&acknowledged,false);
 if(!atomic_load(&binary))return false;
 uart_event_t event;
 while(xQueueReceive(uart_events,&event,0)==pdTRUE) {
  if(event.type==UART_FIFO_OVF||event.type==UART_BUFFER_FULL||event.type==UART_FRAME_ERR||event.type==UART_PARITY_ERR) {
   uart_flush_input(UART_NUM_0);xSemaphoreTake(data_lock,portMAX_DELAY);session->parser.used=0;session->snapshot.uart_errors++;
   if(event.type==UART_FIFO_OVF)session->snapshot.fifo_errors++;else if(event.type==UART_BUFFER_FULL)session->snapshot.buffer_errors++;else session->snapshot.line_errors++;
   xSemaphoreGive(data_lock);
  }
 }
 uint8_t bytes[256];int n=uart_read_bytes(UART_NUM_0,bytes,sizeof(bytes),pdMS_TO_TICKS(10));
 xSemaphoreTake(data_lock,portMAX_DELAY);
 if(n>0)serial_parser_feed(&session->parser,bytes,(size_t)n,received,session);
 else if(n<0)session->snapshot.uart_errors++;
 session->snapshot.discarded=session->parser.discarded;session->snapshot.bad_tail=session->parser.bad_tail;
 xSemaphoreGive(data_lock);return true;
}
esp_err_t serial_uart_start(void) {
 if(serial_uart_active())return ESP_OK;
 if(!atomic_load(&acknowledged)||uart_is_driver_installed(UART_NUM_0))return ESP_ERR_INVALID_STATE;
 session=heap_caps_calloc(1,sizeof(*session),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 data_lock=xSemaphoreCreateMutex();
 if(!session||!data_lock){if(data_lock)vSemaphoreDelete(data_lock);data_lock=NULL;heap_caps_free(session);session=NULL;return ESP_ERR_NO_MEM;}
 xSemaphoreTake(log_lock,portMAX_DELAY);fflush(stdout);esp_rom_uart_tx_wait_idle(0);atomic_store(&binary,true);xSemaphoreGive(log_lock);
 uart_config_t cfg={.baud_rate=921600,.data_bits=UART_DATA_8_BITS,.parity=UART_PARITY_DISABLE,.stop_bits=UART_STOP_BITS_1,.flow_ctrl=UART_HW_FLOWCTRL_DISABLE,.source_clk=UART_SCLK_DEFAULT};
 esp_err_t e=uart_param_config(UART_NUM_0,&cfg);
 if(e==ESP_OK)e=uart_set_pin(UART_NUM_0,43,44,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE);
 if(e==ESP_OK)e=uart_driver_install(UART_NUM_0,8192,0,32,&uart_events,ESP_INTR_FLAG_LEVEL2);
 if(e==ESP_OK)e=uart_set_rx_full_threshold(UART_NUM_0,32);
 if(e==ESP_OK)e=uart_set_rx_timeout(UART_NUM_0,2);
 if(e!=ESP_OK){if(uart_is_driver_installed(UART_NUM_0))uart_driver_delete(UART_NUM_0);atomic_store(&binary,false);vSemaphoreDelete(data_lock);data_lock=NULL;heap_caps_free(session);session=NULL;return e;}
 uart_flush_input(UART_NUM_0);return ESP_OK;
}
esp_err_t serial_uart_stop(void) {
 if(!serial_uart_active())return ESP_OK;
 if(!atomic_load(&acknowledged))return ESP_ERR_INVALID_STATE;
 esp_err_t e=uart_wait_tx_done(UART_NUM_0,pdMS_TO_TICKS(200));if(e!=ESP_OK)return e;
 uart_flush_input(UART_NUM_0);e=uart_driver_delete(UART_NUM_0);if(e!=ESP_OK)return e;
 /* No consumer or UI callback is running now. Raw bytes never reach getchar. */
 uint8_t byte;while(esp_rom_uart_rx_one_char(&byte)==0){}
 clearerr(stdin);uart_events=NULL;vSemaphoreDelete(data_lock);data_lock=NULL;heap_caps_free(session);session=NULL;
 atomic_store(&binary,false);return ESP_OK;
}
bool serial_uart_send(const serial_frame_t *f) {
 if(!serial_uart_active()||!session)return false;
 uint8_t bytes[48];serial_frame_encode(f,bytes);
 int n=uart_write_bytes(UART_NUM_0,bytes,sizeof(bytes));
 bool ok=n==48&&uart_wait_tx_done(UART_NUM_0,pdMS_TO_TICKS(100))==ESP_OK;
 xSemaphoreTake(data_lock,portMAX_DELAY);if(ok)session->snapshot.tx++;else session->snapshot.uart_errors++;xSemaphoreGive(data_lock);return ok;
}
void serial_uart_snapshot(serial_uart_snapshot_t *out) {
 memset(out,0,sizeof(*out));if(!serial_uart_active()||!session)return;
 xSemaphoreTake(data_lock,portMAX_DELAY);*out=session->snapshot;xSemaphoreGive(data_lock);
}
size_t serial_uart_history(uint32_t *cursor,serial_sample_t *out,size_t capacity,uint32_t *lost) {
 if(!serial_uart_active()||!session)return 0;
 xSemaphoreTake(data_lock,portMAX_DELAY);size_t n=serial_history_read(&session->history,cursor,out,capacity,lost);xSemaphoreGive(data_lock);return n;
}
