#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <time.h>
#include "esp_err.h"
#define WEATHER_DAYS 4
#define WEATHER_CITIES 5
#define WEATHER_HOURS 24
#define WEATHER_THEMES 3
#define WEATHER_DEFAULT_THEME 1
typedef struct { const char *name,*region; float latitude,longitude; } weather_city_t;
extern const weather_city_t weather_cities[WEATHER_CITIES];
typedef struct { int date,code; float low,high; } weather_day_t;
typedef struct { time_t time; float temperature; int code,precipitation; bool daytime; } weather_hour_t;
typedef struct {
    bool valid,daytime;
    unsigned city;
    time_t fetched,data_time;
    float temperature,apparent,humidity,wind;
    int code;
    weather_day_t days[WEATHER_DAYS];
    unsigned hour_count;
    weather_hour_t hours[WEATHER_HOURS];
} weather_data_t;
typedef enum { WEATHER_IDLE,WEATHER_WAIT_WIFI,WEATHER_WAIT_TIME,WEATHER_FETCHING,
    WEATHER_OK,WEATHER_ERROR,WEATHER_LIMITED } weather_phase_t;
typedef struct {
    weather_data_t data;
    unsigned city,theme,revision;
    bool network,time_valid,active,save_warning,dynamic_colors;
    weather_phase_t phase;
    int http_status;
    esp_err_t error;
    time_t last_sync;
} weather_snapshot_t;
esp_err_t weather_start(void);
esp_err_t weather_stop(void);
void weather_activate(bool active);
void weather_desktop_activate(bool active);
void weather_snapshot(weather_snapshot_t *out);
esp_err_t weather_choose_city(unsigned city);
esp_err_t weather_choose_theme(unsigned theme);
esp_err_t weather_choose_colors(bool enabled);
uint32_t weather_config_encode(unsigned city,unsigned theme,bool dynamic);
bool weather_config_decode(uint32_t config,unsigned *city,unsigned *theme,bool *dynamic);
bool weather_refresh(void);
bool weather_parse(const char *json,unsigned city,time_t now,weather_data_t *out);
const char *weather_description(int code);
int weather_date_key(const struct tm *date);
bool weather_lunar(int year,int month,int day,char *out,size_t size);
time_t weather_now(void);
