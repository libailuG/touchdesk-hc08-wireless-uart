#pragma once
#include "app.h"

/* Prepare an asynchronous transition; the current app closes before submission. */
esp_err_t usb_app_mode_prepare(const app_desc_t *next);
bool usb_app_mode_pending(void);
void usb_app_mode_poll(void);
/* Request the KeySim screen from UART without emitting a keystroke. */
bool usb_app_mode_console(const char *line);
