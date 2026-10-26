#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
/* A worker suspends itself only after dropping locks and joining callbacks.
 * Delete it from the owning main task; never free the stack of a live task. */
static inline esp_err_t service_task_join(TaskHandle_t *task,unsigned timeout_ms) {
 if(!*task) return ESP_OK;
 TickType_t start=xTaskGetTickCount();
 while(eTaskGetState(*task)!=eSuspended) {
  if(xTaskGetTickCount()-start>=pdMS_TO_TICKS(timeout_ms)) return ESP_ERR_TIMEOUT;
  vTaskDelay(pdMS_TO_TICKS(10));
 }
 vTaskDelete(*task); *task=NULL; return ESP_OK;
}
