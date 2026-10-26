#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "cJSON.h"
#define XZ_PACKET_MAX 4096
#define XZ_SESSION_MAX 128
/* Stable board identity; never impersonate an existing release/OTA channel. */
#define XZ_BOARD_TYPE "huiqin-touchdesk-usb"
#define XZ_OTA_URL "https://api.tenclass.net/xiaozhi/ota/"
typedef struct { float history[63]; unsigned pos,phase; } xz_decimator_t;
typedef struct {uint8_t data[XZ_PACKET_MAX+1],type;size_t used,frame_offset,frame_size;bool complete;} xz_assembly_t;
/* -1 malformed, 0 incomplete, 1 complete. No allocation and no truncation. */
int xz_assemble_ws(xz_assembly_t *s,unsigned opcode,bool fin,int total,int offset,const void *data,int length);
int xz_assemble_mqtt(xz_assembly_t *s,int total,int offset,const void *data,int length);
void xz_decimator_reset(xz_decimator_t *s);
size_t xz_decimate(xz_decimator_t *s,const int16_t *in,size_t n,int16_t *out,size_t cap);
bool xz_copy(char *dst,size_t capacity,const char *src);
bool xz_hex16(const char *hex,uint8_t out[16]);
bool xz_udp_header(const uint8_t *data,size_t length,uint32_t previous,uint32_t *sequence);
bool xz_ws_audio(unsigned version,const uint8_t **data,size_t *length);
const char *xz_string(const cJSON *obj,const char *key);
bool xz_valid_session(const cJSON *obj,const char *session);
/* Clip UTF-8 at a code point boundary; display truncation is explicit. */
void xz_display_text(char *out,size_t cap,const char *text);
