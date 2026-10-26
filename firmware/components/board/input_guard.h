#ifndef TOUCHDESK_INPUT_GUARD_H
#define TOUCHDESK_INPUT_GUARD_H
#include <stdbool.h>
typedef struct { bool blocked; } input_guard_t;
static inline void input_guard_cancel(input_guard_t *g) { g->blocked=true; }
static inline bool input_guard_filter(input_guard_t *g,bool physical_down,bool visible)
{
    if(!physical_down) g->blocked=false;
    return physical_down && visible && !g->blocked;
}
#endif
