#pragma once
#include "esp_heap_caps.h"
/* Keep LVGL objects and temporary drawing allocations out of internal DMA RAM. */
static inline void *td_ui_alloc(size_t n) { return heap_caps_malloc(n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
static inline void *td_ui_realloc(void *p,size_t n) { return heap_caps_realloc(p,n,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT); }
#undef LV_MEM_CUSTOM_ALLOC
#undef LV_MEM_CUSTOM_FREE
#undef LV_MEM_CUSTOM_REALLOC
#define LV_MEM_CUSTOM_ALLOC td_ui_alloc
#define LV_MEM_CUSTOM_FREE heap_caps_free
#define LV_MEM_CUSTOM_REALLOC td_ui_realloc
