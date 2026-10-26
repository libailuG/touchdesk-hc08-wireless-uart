#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "serial_history.h"
static serial_history_t h;static serial_sample_t out[512];
int main(void){serial_frame_t f={0};uint32_t cursor=0,lost=0;
 for(unsigned i=1;i<=3000;i++){f.data3=(float)i;serial_history_push(&h,&f);}
 assert(serial_history_read(&h,&cursor,out,64,&lost)==64&&lost==2488&&out[0].sequence==2489&&out[63].values[0]==2552);
 assert(serial_history_read(&h,&cursor,out,512,&lost)==448&&cursor==3000&&out[447].values[0]==3000);
 assert(serial_history_read(&h,&cursor,out,512,&lost)==0);
 memset(&h,0,sizeof(h));h.total=UINT32_MAX-2;cursor=h.total;lost=0;
 for(unsigned i=0;i<5;i++){f.data3=(float)i;serial_history_push(&h,&f);}
 assert(serial_history_read(&h,&cursor,out,512,&lost)==5&&cursor==2&&lost==0&&out[4].values[0]==4);
 puts("PASS history FIFO order, partial drains, bounded512 overwrite/lost accounting, uint32 sequence wrap");return 0;}
