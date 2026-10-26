#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* All USB operations remain owned by the existing music worker. */
typedef enum { VOICE_AUDIO_OFF, VOICE_AUDIO_IDLE, VOICE_AUDIO_CAPTURE, VOICE_AUDIO_PLAY } voice_audio_mode_t;
bool voice_audio_init(void);
bool voice_audio_mode(voice_audio_mode_t mode);
bool voice_audio_ok(void);
size_t voice_audio_read(int16_t *samples,size_t capacity,unsigned timeout_ms);
size_t voice_audio_write(const int16_t *samples,size_t count,unsigned timeout_ms);
size_t voice_audio_pending(void);
bool voice_audio_busy(void);
