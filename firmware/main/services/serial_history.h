#ifndef SERIAL_HISTORY_H
#define SERIAL_HISTORY_H
#include "serial_frame.h"
#define SERIAL_HISTORY_CAPACITY 512u
typedef struct { uint32_t sequence;float values[8];int64_t received_us; } serial_sample_t;
typedef struct { serial_sample_t samples[SERIAL_HISTORY_CAPACITY];uint32_t total; } serial_history_t;
void serial_history_push(serial_history_t *h,const serial_frame_t *f);
void serial_history_push_timed(serial_history_t *h,const serial_frame_t *f,int64_t received_us);
size_t serial_history_read(const serial_history_t *h,uint32_t *cursor,serial_sample_t *out,size_t capacity,uint32_t *lost);
#endif
