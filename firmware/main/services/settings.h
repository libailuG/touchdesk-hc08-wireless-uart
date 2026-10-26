#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t settings_init(void);
bool settings_autosleep(void);
esp_err_t settings_set_autosleep(bool enabled);
esp_err_t settings_counter_load(int *value);
esp_err_t settings_counter_save(int value);
bool settings_available(void);
/* Seconds of inactivity; zero means never. */
uint32_t settings_sleep_seconds(void);
esp_err_t settings_set_sleep_seconds(uint32_t seconds);
