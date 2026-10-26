#include "mouse_flow.h"
void mouse_flow_reset(mouse_flow_t *f) {
    f->head=f->count=0;f->epoch++;f->armed=false;f->release=f->ready;
}
void mouse_flow_ready(mouse_flow_t *f,bool ready) {
    if(f->ready!=ready){f->ready=ready;mouse_flow_reset(f);}
}
void mouse_flow_feed(mouse_flow_t *f,uint8_t buttons,int x,int y) {
    mouse_flow_feed_wheel(f,buttons,x,y,0);
}
void mouse_flow_feed_wheel(mouse_flow_t *f,uint8_t buttons,int x,int y,int wheel) {
    uint8_t previous=f->physical;
    f->physical=buttons&7;
    if(!f->ready||f->release){f->dropped++;return;}
    if(!f->armed){if(!f->physical)f->armed=true;f->dropped++;return;}
    if(!x&&!y&&!wheel&&previous==f->physical)return;
    mouse_item_t *item=0;
    if(f->count) {
        item=&f->q[(f->head+f->count-1)%MOUSE_FLOW_CAP];
        if(item->buttons!=f->physical)item=0;
    }
    if(!item) {
        if(f->count==MOUSE_FLOW_CAP){f->overflows++;mouse_flow_reset(f);return;}
        item=&f->q[(f->head+f->count++)%MOUSE_FLOW_CAP];
        *item=(mouse_item_t){.buttons=f->physical};
    }
    item->x+=x;item->y+=y;item->wheel+=wheel;
    if(item->x>4096||item->x< -4096||item->y>4096||item->y< -4096||item->wheel>4096||item->wheel< -4096) {
        f->overflows++;mouse_flow_reset(f);
    }
}
static int8_t slice(int32_t v){return v>127?127:v< -127?-127:(int8_t)v;}
bool mouse_flow_peek(mouse_flow_t *f,mouse_packet_t *p) {
    return mouse_flow_peek_limit(f,p,127);
}
bool mouse_flow_peek_limit(mouse_flow_t *f,mouse_packet_t *p,int limit) {
    if(!f->ready)return false;
    *p=(mouse_packet_t){.epoch=f->epoch,.release=f->release};
    if(f->release)return true;
    if(!f->count)return false;
    mouse_item_t *item=&f->q[f->head];
    p->buttons=item->buttons;
    p->x=item->x>limit?limit:item->x< -limit?-limit:item->x;
    p->y=item->y>limit?limit:item->y< -limit?-limit:item->y;
    p->wheel=slice(item->wheel);return true;
}
void mouse_flow_commit(mouse_flow_t *f,const mouse_packet_t *p) {
    if(!f->ready||p->epoch!=f->epoch)return;
    if(p->release){f->release=false;f->armed=!f->physical;return;}
    if(!f->count)return;
    mouse_item_t *item=&f->q[f->head];item->x-=p->x;item->y-=p->y;item->wheel-=p->wheel;
    if(!item->x&&!item->y&&!item->wheel){f->head=(f->head+1)%MOUSE_FLOW_CAP;f->count--;}
}
