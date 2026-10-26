#ifndef TOUCHDESK_SERIAL_UART_H
#define TOUCHDESK_SERIAL_UART_H
#pragma once
#include "serial_frame.h"
#include "serial_history.h"
#include "esp_err.h"
typedef struct { serial_frame_t last;uint32_t tx,rx,discarded,bad_tail,uart_errors;bool has_frame;uint32_t fifo_errors,buffer_errors,line_errors; } serial_uart_snapshot_t;
esp_err_t serial_uart_console_init(void);
/* Called by the single console task, before getchar. Also handles pause acknowledgement. */
bool serial_uart_console_step(void);
/* Main task pauses the console before taking the app lifecycle lock. */
esp_err_t serial_uart_pause(void);
void serial_uart_resume(void);
esp_err_t serial_uart_start(void);
esp_err_t serial_uart_stop(void);
bool serial_uart_active(void);
bool serial_uart_send(const serial_frame_t *frame);
void serial_uart_snapshot(serial_uart_snapshot_t *out);
size_t serial_uart_history(uint32_t *cursor,serial_sample_t *out,size_t capacity,uint32_t *lost);

#endif
