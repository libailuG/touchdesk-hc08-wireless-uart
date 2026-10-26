#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "keyboard_flow.h"
#include "mouse_report.h"
#include "mouse_flow.h"
static void release_keys(keyboard_flow_t *f){keyboard_packet_t p;assert(keyboard_flow_peek(f,&p)&&p.release);for(int i=0;i<8;i++)assert(!p.data[i]);keyboard_flow_commit(f,&p);}
int main(void) {
    keyboard_flow_t f={0};keyboard_packet_t p;
    uint8_t a[8]={0,0,4},up[8]={0},ctrl_c[8]={1,0,6},error[8]={0,0,1,1,1,1,1,1};
    keyboard_flow_feed(&f,a);keyboard_flow_ready(&f,true);release_keys(&f);assert(!f.armed);
    keyboard_flow_feed(&f,up);assert(f.armed);
    keyboard_flow_feed(&f,a);keyboard_flow_feed(&f,a);keyboard_flow_feed(&f,up);assert(f.count==2);
    assert(keyboard_flow_peek(&f,&p)&&!memcmp(p.data,a,8));keyboard_flow_commit(&f,&p);
    assert(keyboard_flow_peek(&f,&p)&&!memcmp(p.data,up,8));keyboard_flow_commit(&f,&p);
    keyboard_flow_feed(&f,ctrl_c);assert(keyboard_flow_peek(&f,&p)&&p.data[0]==1&&p.data[2]==6);
    keyboard_packet_t retry;assert(keyboard_flow_peek(&f,&retry)&&!memcmp(p.data,retry.data,8));
    keyboard_flow_feed(&f,error);assert(f.rollovers==1&&f.release&&!f.count);keyboard_flow_commit(&f,&p);assert(f.release);
    release_keys(&f);assert(!f.armed);keyboard_flow_feed(&f,up);
    for(int i=0;i<=KEYBOARD_FLOW_CAP;i++)keyboard_flow_feed(&f,i&1?up:a);
    assert(f.overflows==1&&f.release&&!f.count);release_keys(&f);
    keyboard_flow_ready(&f,false);keyboard_flow_feed(&f,up);keyboard_flow_ready(&f,true);release_keys(&f);assert(!f.count);
    const uint8_t descriptor[]={
      0x05,0x01,0x09,0x02,0xa1,0x01,0x09,0x01,0xa1,0x00,0x05,0x09,0x19,0x01,0x29,0x03,
      0x15,0x00,0x25,0x01,0x95,0x03,0x75,0x01,0x81,0x02,0x95,0x01,0x75,0x05,0x81,0x01,
      0x05,0x01,0x09,0x30,0x09,0x31,0x16,0x00,0xf8,0x26,0xff,0x07,0x75,0x0c,0x95,0x02,
      0x81,0x06,0x09,0x38,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x01,0x81,0x06,0xc0,0xc0};
    mouse_report_map_t map;mouse_decoded_t v;
    assert(mouse_report_parse(&map,descriptor,sizeof(descriptor))&&map.count==6);
    uint8_t d[]={1,0xf6,0x4f,0x01,0xff};
    assert(mouse_report_decode(&map,d,5,0,&v)&&v.buttons==1&&v.x==-10&&v.y==20&&v.wheel==-1);
    uint8_t extremes[]={0,0xff,0x07,0x80,1};
    assert(mouse_report_decode(&map,extremes,5,1,&v)&&!v.buttons&&v.x==2047&&v.y==-2048&&v.wheel==1);
    v.x=123;assert(!mouse_report_decode(&map,d,4,0,&v)&&v.x==123);
    uint8_t iddesc[sizeof(descriptor)+2];iddesc[0]=0x85;iddesc[1]=7;memcpy(iddesc+2,descriptor,sizeof(descriptor));
    assert(mouse_report_parse(&map,iddesc,sizeof(iddesc)));
    uint8_t iddata[]={7,1,0xf6,0x4f,1,0xff};
    assert(mouse_report_decode(&map,iddata,6,0,&v)&&v.wheel==-1);iddata[0]=8;assert(!mouse_report_decode(&map,iddata,6,0,&v));
    uint8_t bad[]={0x75};assert(!mouse_report_parse(&map,bad,1));
    mouse_flow_t m={0};mouse_packet_t mp;mouse_flow_ready(&m,true);assert(mouse_flow_peek(&m,&mp));mouse_flow_commit(&m,&mp);
    mouse_flow_feed_wheel(&m,0,2047,-2048,-3);assert(mouse_flow_peek_limit(&m,&mp,32767));
    assert(mp.x==2047&&mp.y==-2048&&mp.wheel==-3);mouse_flow_commit(&m,&mp);assert(!m.count);
    puts("PASS keyboard ordering/modifiers/dedup/held gating/retry/rollover/overflow/reconnect; actual 12-bit XY + wheel descriptor; report IDs; truncation; 16-bit BLE movement and wheel");
    return 0;
}
