#include <assert.h>
#include <stdio.h>
#include "mouse_flow.h"
static mouse_packet_t next(mouse_flow_t *f){mouse_packet_t p;assert(mouse_flow_peek(f,&p));return p;}
static void neutral(mouse_flow_t *f){mouse_packet_t p=next(f);assert(p.release&&!p.buttons&&!p.x&&!p.y);mouse_flow_commit(f,&p);}
int main(void) {
    mouse_flow_t f={0};mouse_packet_t p;
    mouse_flow_feed(&f,1,100,20);assert(!mouse_flow_peek(&f,&p));
    mouse_flow_ready(&f,true);neutral(&f);assert(!f.armed);
    mouse_flow_feed(&f,1,10,0);assert(!f.count);
    mouse_flow_feed(&f,0,0,0);assert(f.armed&&!f.count);
    mouse_flow_feed(&f,1,0,0);mouse_flow_feed(&f,0,0,0);
    p=next(&f);assert(p.buttons==1);mouse_flow_commit(&f,&p);
    p=next(&f);assert(p.buttons==0);mouse_flow_commit(&f,&p);assert(!f.count);
    mouse_flow_feed(&f,0,-128,127);int x=0,y=0;
    while(mouse_flow_peek(&f,&p)){assert(p.x>=-127&&p.x<=127);x+=p.x;y+=p.y;mouse_flow_commit(&f,&p);}
    assert(x==-128&&y==127);
    mouse_flow_feed(&f,0,20,0);p=next(&f); /* Notification not accepted: retry without advancing. */
    mouse_packet_t retry=next(&f);assert(retry.x==20&&retry.epoch==p.epoch);
    mouse_flow_feed(&f,0,30,0);mouse_flow_commit(&f,&p);p=next(&f);assert(p.x==30);mouse_flow_commit(&f,&p);
    mouse_flow_feed(&f,1,0,0);p=next(&f);mouse_flow_reset(&f);mouse_flow_commit(&f,&p);
    assert(f.release);neutral(&f);assert(!f.armed);
    mouse_flow_feed(&f,0,0,0);assert(f.armed);
    for(int i=0;i<MOUSE_FLOW_CAP+1;i++)mouse_flow_feed(&f,(i&1)?0:1,0,0);
    assert(f.overflows==1&&!f.count&&f.release);neutral(&f);
    mouse_flow_ready(&f,false);mouse_flow_feed(&f,0,127,0);assert(!f.count);
    mouse_flow_ready(&f,true);neutral(&f);assert(!mouse_flow_peek(&f,&p));
    for(int i=0;i<33;i++)mouse_flow_feed(&f,0,127,0);
    assert(f.overflows==2&&f.release);neutral(&f);
    puts("PASS: disconnected input discarded, neutral-before-input, held-button quarantine, ordered clicks, signed motion splitting, failed-send retry, in-flight coalescing, stale commit, overflow release");
    return 0;
}
