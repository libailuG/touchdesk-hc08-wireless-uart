#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
typedef struct {
    unsigned devices,hubs,interfaces,reports,errors,disconnects,buttons,changes,down[3],up[3];
    unsigned key_reports,key_interfaces,wheel_events;
    int64_t x,y,wheel;
    size_t len;
    uint8_t raw[16];
} keymouse_snapshot_t;
esp_err_t keymouse_init(void);
esp_err_t keymouse_deinit(void);
esp_err_t keymouse_host_start(void);
esp_err_t keymouse_host_stop(void);
esp_err_t keymouse_host_prepare_stop(void);
void keymouse_snapshot(keymouse_snapshot_t *out);
void keymouse_poll(void);
