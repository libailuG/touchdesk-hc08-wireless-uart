#pragma once
#include <stdbool.h>
#include <stdint.h>
#define MOUSE_FLOW_CAP 32
typedef struct { uint8_t buttons; int32_t x,y,wheel; } mouse_item_t;
typedef struct {
    mouse_item_t q[MOUSE_FLOW_CAP];
    unsigned head,count,epoch,overflows,dropped;
    uint8_t physical;
    bool ready,armed,release;
} mouse_flow_t;
typedef struct { uint8_t buttons; int16_t x,y;int8_t wheel; unsigned epoch; bool release; } mouse_packet_t;
/* Caller serializes all operations. Only commit after notification accepted. */
void mouse_flow_ready(mouse_flow_t *f,bool ready);
void mouse_flow_reset(mouse_flow_t *f);
void mouse_flow_feed(mouse_flow_t *f,uint8_t buttons,int x,int y);
void mouse_flow_feed_wheel(mouse_flow_t *f,uint8_t buttons,int x,int y,int wheel);
bool mouse_flow_peek(mouse_flow_t *f,mouse_packet_t *p);
bool mouse_flow_peek_limit(mouse_flow_t *f,mouse_packet_t *p,int limit);
void mouse_flow_commit(mouse_flow_t *f,const mouse_packet_t *p);
