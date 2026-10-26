#pragma once
#include "app.h"
esp_err_t app_services_prepare(const app_desc_t *next);
void app_services_status(void);
esp_err_t app_services_init(void);
void app_services_lock(void);
void app_services_unlock(void);
