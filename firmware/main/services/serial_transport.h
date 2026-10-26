#pragma once
#include "serial_uart.h"
#include "serial_ble.h"
/* The app lifecycle serializes mode changes against UI callbacks. */
static inline bool serial_transport_active(void){return serial_ble_active()||serial_uart_active();}
static inline bool serial_transport_send(const serial_frame_t *f){return serial_ble_active()?serial_ble_send(f):serial_uart_send(f);}
static inline void serial_transport_snapshot(serial_uart_snapshot_t *o){if(serial_ble_active())serial_ble_snapshot(o);else serial_uart_snapshot(o);}
static inline size_t serial_transport_history(uint32_t *c,serial_sample_t *o,size_t n,uint32_t *l){return serial_ble_active()?serial_ble_history(c,o,n,l):serial_uart_history(c,o,n,l);}
