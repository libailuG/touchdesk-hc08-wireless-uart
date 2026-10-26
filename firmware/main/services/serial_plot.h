#pragma once
#include "serial_history.h"
#define SERIAL_PLOT_CAPACITY 12000u
#define SERIAL_PLOT_POINTS 1000u
/* Allocated only in PSRAM for the lifetime of the serial app. */
typedef struct { serial_sample_t samples[SERIAL_PLOT_CAPACITY];unsigned head,count; } serial_plot_t;
typedef struct { unsigned x;float value; } serial_plot_point_t;
typedef struct {
 serial_plot_point_t points[SERIAL_PLOT_POINTS];unsigned count,visible;
 float minimum,maximum;bool finite,truncated;int64_t span_us;
} serial_plot_view_t;
void serial_plot_push(serial_plot_t *h,const serial_sample_t *sample);
const serial_sample_t *serial_plot_at(const serial_plot_t *h,unsigned index);
/* Uniform time grid, nearest raw sample; X=0..10000 ends at latest reception. */
void serial_plot_reduce(const serial_plot_t *h,unsigned channel,unsigned window_ms,serial_plot_view_t *view);
