#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define SERIAL_FRAME_SIZE 48u
#define SERIAL_START_FLAG UINT32_C(0xAAAAAAAA)
#define SERIAL_END_FLAG UINT32_C(0xEEEEEEEE)
typedef struct {
 uint32_t start_flag,data1,data2;
 float data3,data4,data5,data6,data7,data8,data9,data10;
 uint32_t end_flag;
} serial_frame_t;
typedef char serial_float32_required[(sizeof(float)==4)?1:-1];
typedef char serial_frame_size_required[(sizeof(serial_frame_t)==48)?1:-1];
typedef char serial_tail_offset_required[(offsetof(serial_frame_t,end_flag)==44)?1:-1];
typedef struct { uint8_t bytes[48];unsigned used;uint32_t discarded,bad_tail; } serial_parser_t;
typedef void (*serial_frame_callback_t)(const serial_frame_t *,void *);
void serial_frame_init(serial_frame_t *frame);
void serial_frame_encode(const serial_frame_t *frame,uint8_t out[48]);
bool serial_frame_decode(const uint8_t in[48],serial_frame_t *frame);
void serial_parser_feed(serial_parser_t *parser,const uint8_t *bytes,size_t length,serial_frame_callback_t callback,void *ctx);
bool serial_value_parse(serial_frame_t *frame,unsigned index,const char *text);
void serial_value_format(const serial_frame_t *frame,unsigned index,char *text,size_t capacity,bool editing);

void serial_float_format(float value,char *text,size_t capacity);
