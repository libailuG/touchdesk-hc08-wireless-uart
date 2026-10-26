#pragma once
#include <stdbool.h>
#include <stdint.h>
#define KEYBOARD_FLOW_CAP 32
typedef struct {
    uint8_t q[KEYBOARD_FLOW_CAP][8],physical[8];
    unsigned head,count,epoch,dropped,overflows,rollovers;
    bool ready,release,armed;
} keyboard_flow_t;
typedef struct {uint8_t data[8];unsigned epoch;bool release;} keyboard_packet_t;
void keyboard_flow_reset(keyboard_flow_t *f);
void keyboard_flow_ready(keyboard_flow_t *f,bool ready);
void keyboard_flow_feed(keyboard_flow_t *f,const uint8_t report[8]);
bool keyboard_flow_peek(keyboard_flow_t *f,keyboard_packet_t *p);
void keyboard_flow_commit(keyboard_flow_t *f,const keyboard_packet_t *p);
