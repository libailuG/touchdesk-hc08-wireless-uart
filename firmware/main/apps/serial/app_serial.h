#pragma once
/* Explicit diagnostic only: sends one test frame via the actual send button,
 * receives for 12 seconds, returns home and reports after UART text is restored. */
void serial_app_selftest_request(void);
void serial_app_poll(void);

void serial_app_curve_request(void);

/* Explicit HC-08 diagnostic: two button frames after READY, auto-exit at 30s. */
void wireless_app_selftest_request(void);
