#include "serial_history.h"
#include <string.h>
void serial_history_push(serial_history_t *h,const serial_frame_t *f) {
 serial_history_push_timed(h,f,0);
}
void serial_history_push_timed(serial_history_t *h,const serial_frame_t *f,int64_t received_us) {
 uint32_t seq=++h->total;serial_sample_t *s=&h->samples[(seq-1u)%SERIAL_HISTORY_CAPACITY];
 s->sequence=seq;s->received_us=received_us;memcpy(s->values,&f->data3,sizeof(s->values));
}
size_t serial_history_read(const serial_history_t *h,uint32_t *cursor,serial_sample_t *out,size_t capacity,uint32_t *lost) {
 uint32_t pending=h->total-*cursor;
 if(pending>SERIAL_HISTORY_CAPACITY){if(lost)*lost+=pending-SERIAL_HISTORY_CAPACITY;*cursor=h->total-SERIAL_HISTORY_CAPACITY;pending=SERIAL_HISTORY_CAPACITY;}
 size_t n=pending<capacity?pending:capacity;
 for(size_t i=0;i<n;i++){uint32_t seq=*cursor+1u;out[i]=h->samples[(seq-1u)%SERIAL_HISTORY_CAPACITY];*cursor=seq;}
 return n;
}
