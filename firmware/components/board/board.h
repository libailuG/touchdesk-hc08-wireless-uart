#pragma once
#include "lvgl.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
void board_init(void);
/* UI task only: one owner of the I2C output shadow and input state. */
esp_err_t board_backlight(bool enabled);
void board_input_cancel(void);
/* UI task only: wait for DMA completion and rearm touch after rotation. */
esp_err_t board_display_landscape(bool enabled);
/* UI consumers can invalidate gestures after ambiguous/faulted touch frames. */
unsigned board_input_fault_epoch(void);
bool board_touch_down(void);
int64_t board_idle_us(void);
const char *board_touch_id(void);

/* Boot diagnostics; SD shadow updates serialize with UI backlight updates. */
esp_err_t board_sd_select(bool selected);
void board_storage_probe(void);
const char *board_storage_summary(void);

/* Music worker only; block writes always return write-protected. */
esp_err_t board_storage_mount(void);
/* Music worker only: explicit user-requested new-file generation. Default false. */
void board_storage_allow_test_write(bool allowed);
