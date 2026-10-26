#include "mouse_report.h"
#include <string.h>
typedef struct {uint32_t page,size,count,id;int32_t min;} globals_t;
bool mouse_report_parse(mouse_report_map_t *m,const uint8_t *d,size_t n) {
    memset(m,0,sizeof(*m));if(!d||n>2048)return false;
    globals_t g={0},stack[4];unsigned depth=0;
    uint16_t offsets[256]={0};uint32_t usages[32],umin=0,umax=0;unsigned nu=0;bool range=false,hx=false,hy=false;
    for(size_t p=0;p<n;) {
        uint8_t prefix=d[p++];if(prefix==0xfe)return false;
        unsigned len=prefix&3;if(len==3)len=4;if(p+len>n)return false;
        uint32_t v=0;for(unsigned j=0;j<len;j++)v|=(uint32_t)d[p++]<<(8*j);
        unsigned type=(prefix>>2)&3,tag=prefix>>4;
        if(type==1) {
            if(tag==0)g.page=v;
            else if(tag==1)g.min=len==1?(int8_t)v:len==2?(int16_t)v:(int32_t)v;
            else if(tag==7)g.size=v;
            else if(tag==8){if(!v||v>255)return false;g.id=v;m->ids=true;}
            else if(tag==9)g.count=v;
            else if(tag==10){if(depth==4)return false;stack[depth++]=g;}
            else if(tag==11){if(!depth)return false;g=stack[--depth];}
        } else if(type==2) {
            uint32_t usage=len==4?v:(g.page<<16)|v;
            if(tag==0){if(nu==32)return false;usages[nu++]=usage;}
            else if(tag==1){umin=usage;range=true;}
            else if(tag==2)umax=usage;
        } else if(type==0) {
            if(tag==8) {
                if(!g.size||g.size>32||g.count>512||g.size*g.count+offsets[g.id]>512)return false;
                for(unsigned j=0;j<g.count;j++) {
                    uint32_t usage=j<nu?usages[j]:(range&&umin+j<=umax?umin+j:0);
                    uint32_t page=usage>>16,u=usage&0xffff;unsigned kind=0;
                    if(!(v&1)&&(v&2)&&g.size<=16) {
                        if(page==9&&u>=1&&u<=3)kind=u;
                        else if(page==1&&(v&4)){if(u==0x30)kind=4;else if(u==0x31)kind=5;else if(u==0x38)kind=6;}
                    }
                    if(kind) {
                        if(m->count==16)return false;
                        m->fields[m->count++]=(mouse_field_t){.bit=offsets[g.id]+j*g.size,.width=g.size,.id=g.id,.kind=kind,.sign=g.min<0};
                        if(kind==4)hx=true;
                        if(kind==5)hy=true;
                    }
                }
                offsets[g.id]+=g.size*g.count;
            }
            nu=0;range=false;umin=umax=0;
        }
    }
    return hx&&hy&&!depth;
}
bool mouse_report_decode(const mouse_report_map_t *m,const uint8_t *d,size_t n,uint8_t previous_buttons,mouse_decoded_t *out) {
    if(!d||!n)return false;
    uint8_t id=0;if(m->ids){id=*d++;n--;}
    mouse_decoded_t value={.buttons=previous_buttons};bool found=false;
    for(unsigned i=0;i<m->count;i++) {
        const mouse_field_t *f=&m->fields[i];if(f->id!=id)continue;
        if((size_t)f->bit+f->width>n*8)return false;
        uint32_t raw=0;for(unsigned j=0;j<f->width;j++)raw|=((d[(f->bit+j)/8]>>((f->bit+j)%8))&1u)<<j;
        int v=(int)raw;if(f->sign&&(raw&(1u<<(f->width-1))))v-=(1u<<f->width);
        if(f->kind<=3){unsigned bit=1u<<(f->kind-1);value.buttons=(value.buttons&~bit)|(raw?bit:0);}
        else if(f->kind==4)value.x=v;else if(f->kind==5)value.y=v;else if(f->kind==6)value.wheel=v;
        found=true;
    }
    if(found)*out=value;
    return found;
}
