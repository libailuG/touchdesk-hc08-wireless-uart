#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum { XZ_OFFLINE,XZ_CONNECTING,XZ_ACTIVATING,XZ_READY,XZ_LISTENING,XZ_THINKING,XZ_SPEAKING,XZ_ERROR } xiaozhi_state_t;
typedef struct {
 xiaozhi_state_t state;
 bool available,held,continuous;
 unsigned seconds,tx_frames,rx_frames;
 char status[192],activation[96],user[512],answer[1024],emotion[32],device_id[20];
} xiaozhi_snapshot_t;
esp_err_t xiaozhi_init(void);
esp_err_t xiaozhi_deinit(void);
void xiaozhi_snapshot(xiaozhi_snapshot_t *out);
void xiaozhi_connect(void);
void xiaozhi_continuous_start(void);
void xiaozhi_hold(bool held);
void xiaozhi_cancel(void);
void xiaozhi_leave(void);
/* Acknowledge that the voice task has completed the latest leave request. */
bool xiaozhi_usb_released(void);
bool xiaozhi_take_ui_request(void);
/* UART diagnostics contain no credentials. */
bool xiaozhi_console(const char *line);
