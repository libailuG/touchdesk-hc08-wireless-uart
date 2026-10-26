#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

/* Read-only Flash mapping; images and glyphs are never copied wholesale into RAM. */
esp_err_t resource_store_init(void);
const uint8_t *resource_store_data(size_t offset,size_t length);
bool weather_art_bind_resources(void);
bool font_ui_bind_resources(void);
bool weather_fonts_bind_resources(void);
bool weather_number_fonts_bind_resources(void);
