#pragma once
#include "serial_uart.h"
esp_err_t serial_ble_start(void);
esp_err_t serial_ble_stop(void);
bool serial_ble_active(void);
bool serial_ble_send(const serial_frame_t *f);
void serial_ble_snapshot(serial_uart_snapshot_t *out);
size_t serial_ble_history(uint32_t *cursor,serial_sample_t *out,size_t cap,uint32_t *lost);
const char *serial_ble_status(void);
