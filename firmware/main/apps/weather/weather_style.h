#ifndef TOUCHDESK_WEATHER_STYLE_H
#define TOUCHDESK_WEATHER_STYLE_H
#pragma once
#include "weather.h"
#include "lvgl.h"
typedef struct { uint32_t bg,ink,sub,line,surface,accent; } weather_palette_t;
typedef enum { WEATHER_TEMPERATURE,WEATHER_HUMIDITY,WEATHER_WIND } weather_metric_t;
const weather_palette_t *weather_palette(unsigned theme);
const char *weather_theme_name(unsigned theme);
const lv_font_t *weather_clock_font(unsigned theme);
const lv_font_t *weather_clock_seconds_font(unsigned theme);
const lv_font_t *weather_temperature_font(unsigned theme);
lv_color_t weather_value_color(unsigned theme,bool dynamic,weather_metric_t metric,float value);
void weather_color_number(char *out,size_t size,unsigned theme,bool dynamic,weather_metric_t metric,float value,unsigned decimals);

#endif
