#pragma once
void music_role_poll(void);
#include <stdbool.h>
#include "esp_err.h"
#define MUSIC_MAX_TRACKS 128
typedef struct {
 bool host, connected, playing, paused, scanning;
 unsigned count,index,volume,seconds,rate,channels,catalog_revision;
 char status[128],device[80],title[256];
} music_snapshot_t;
typedef enum { MUSIC_PLAY, MUSIC_PAUSE, MUSIC_NEXT, MUSIC_PREVIOUS, MUSIC_VOLUME, MUSIC_SELECT, MUSIC_ROLE, MUSIC_GENERATE_TEST } music_action_t;
esp_err_t uart_console_init(void);
esp_err_t music_init(void);
void music_configure(bool audio,bool catalog);
esp_err_t music_deinit(void);
esp_err_t music_set_role(bool audio);
bool music_host_mode(void);
/* Deferred role change, performed only by the audio worker after voice release. */
bool music_role_request(bool audio);
bool music_role_result(esp_err_t *result);
void music_snapshot(music_snapshot_t *out);
bool music_command(music_action_t action,int value);
void music_track_name(unsigned index,char *out,unsigned size);
bool music_take_ui_request(void);

typedef struct {
 bool connected,active;
 unsigned rate,channels,level,peak_level,seconds,frames,clipped;
 float dbfs;
 char device[64],status[128];
} microphone_snapshot_t;
void microphone_snapshot(microphone_snapshot_t *out);
bool microphone_start(void);
bool microphone_stop(void);
bool microphone_reset_peak(void);
bool microphone_take_ui_request(void);
