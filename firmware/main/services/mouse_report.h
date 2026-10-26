#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct {uint16_t bit;uint8_t width,id,kind;bool sign;} mouse_field_t;
typedef struct {mouse_field_t fields[16];unsigned count;bool ids;} mouse_report_map_t;
typedef struct {uint8_t buttons;int x,y,wheel;} mouse_decoded_t;
bool mouse_report_parse(mouse_report_map_t *m,const uint8_t *d,size_t n);
bool mouse_report_decode(const mouse_report_map_t *m,const uint8_t *d,size_t n,uint8_t previous_buttons,mouse_decoded_t *out);
