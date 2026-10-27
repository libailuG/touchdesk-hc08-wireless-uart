#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
esp_err_t usb_service_init(void);
esp_err_t usb_service_deinit(void);
/* Retain the queue/worker while releasing the USB controller and PHY. */
esp_err_t usb_service_start(void);
esp_err_t usb_service_stop(void);
/* UI task owns these calls. Revoke serializes with the worker's report submit. */
unsigned usb_service_set_owner(bool hid_allowed);
void usb_service_cancel(void);
bool usb_service_click(unsigned owner, unsigned gesture, uint8_t mods, uint8_t key);
unsigned usb_service_session(void);
bool usb_service_online(void);
bool usb_service_release_pending(void);

/* Single-touch UI: held reports, relative mouse deltas; owner/session gated. */
bool usb_service_key(unsigned owner,unsigned gesture,uint8_t mods,uint8_t key);
bool usb_service_mouse(unsigned owner,unsigned gesture,uint8_t buttons,int x,int y,int wheel);
bool usb_service_mouse_click(unsigned owner,unsigned gesture);
/* Relative PC media-volume steps. No absolute PC volume feedback is available. */
bool usb_service_volume(unsigned owner,unsigned gesture,int steps);
bool usb_service_mute(unsigned owner,unsigned gesture);

bool usb_service_is_ble(void);
esp_err_t usb_service_transport(bool ble);
const char *usb_service_connection_text(void);
void usb_service_pair(bool forget);
