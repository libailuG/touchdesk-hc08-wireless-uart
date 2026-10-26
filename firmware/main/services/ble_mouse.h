#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool connected,encrypted,ready,advertising,pairing;
    unsigned sent,completed,failed,dropped,overflows,queued,reconnections;
    uint16_t interval;
    bool keyboard_ready;
    unsigned key_sent,key_completed,key_failed,key_queued,key_dropped,key_overflows,key_rollovers;
} ble_mouse_snapshot_t;
esp_err_t ble_mouse_init(void);
esp_err_t ble_mouse_deinit(void);
void ble_mouse_input(uint8_t buttons,int x,int y);
void ble_mouse_wheel_input(uint8_t buttons,int x,int y,int wheel);
void ble_mouse_usb_gone(void);
void ble_mouse_pair(void);
void ble_mouse_forget(void);
void ble_mouse_snapshot(ble_mouse_snapshot_t *out);
void ble_keyboard_input(const uint8_t report[8]);
void ble_keyboard_usb_gone(void);
