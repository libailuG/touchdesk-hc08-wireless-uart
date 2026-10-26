#pragma once
#include <stdint.h>
#include <stddef.h>
typedef struct { float dbfs; unsigned level,peak_level,clipped; } mic_meter_result_t;
void mic_meter_process(const int16_t *pcm,size_t samples,mic_meter_result_t *out);
