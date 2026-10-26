#include "keyboard_flow.h"
#include <string.h>
static bool neutral(const uint8_t *r){if(r[0])return false;for(unsigned i=2;i<8;i++)if(r[i])return false;return true;}
void keyboard_flow_reset(keyboard_flow_t *f){f->head=f->count=0;f->epoch++;f->armed=false;f->release=f->ready;}
void keyboard_flow_ready(keyboard_flow_t *f,bool ready){if(ready!=f->ready){f->ready=ready;keyboard_flow_reset(f);}}
void keyboard_flow_feed(keyboard_flow_t *f,const uint8_t r[8]) {
    bool same=f->physical[0]==r[0]&&!memcmp(f->physical+2,r+2,6);memcpy(f->physical,r,8);f->physical[1]=0;
    for(unsigned i=2;i<8;i++)if(r[i]>=1&&r[i]<=3){f->rollovers++;keyboard_flow_reset(f);return;}
    if(!f->ready||f->release){f->dropped++;return;}
    if(!f->armed){if(neutral(f->physical))f->armed=true;f->dropped++;return;}
    if(same)return;
    if(f->count==KEYBOARD_FLOW_CAP){f->overflows++;keyboard_flow_reset(f);return;}
    memcpy(f->q[(f->head+f->count++)%KEYBOARD_FLOW_CAP],f->physical,8);
}
bool keyboard_flow_peek(keyboard_flow_t *f,keyboard_packet_t *p){
    if(!f->ready||(!f->release&&!f->count))return false;
    *p=(keyboard_packet_t){.epoch=f->epoch,.release=f->release};
    if(!f->release)memcpy(p->data,f->q[f->head],8);
    return true;
}
void keyboard_flow_commit(keyboard_flow_t *f,const keyboard_packet_t *p){
    if(!f->ready||p->epoch!=f->epoch)return;
    if(p->release){f->release=false;f->armed=neutral(f->physical);return;}
    if(f->count){f->head=(f->head+1)%KEYBOARD_FLOW_CAP;f->count--;}
}
