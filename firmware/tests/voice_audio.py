"""Exercise production USB voice arbitration with deterministic driver/ring mocks."""
from pathlib import Path
import subprocess,tempfile
r=Path(__file__).resolve().parents[1]
s=(r/'main/services/music_service.c').read_text(encoding='utf-8')
stop_production=s[s.index('static bool stop_usb_stream('):s.index('static void stop(void)')]
production=s[s.index('static void voice_apply('):s.index('static void put16(')]
head=r'''#include "voice_audio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define atomic_load(p) (*(p))
#define atomic_store(p,v) (*(p)=(v))
#define ESP_OK 0
#define ESP_FAIL 1
#define ESP_LOGW(...) ((void)0)
#define pdMS_TO_TICKS(n) (n)
typedef int esp_err_t;
static unsigned voice_generation,voice_done;
static int voice_requested,voice_applied;
static bool voice_failed,mic_stream,stream;
static int microphone=1,speaker=1;
static unsigned mic_rate,mic_channels,rate,channels,samples;
static long long mic_last_data,now;
static int16_t storage[4096],*pcm=storage;
static int voice_in=1,voice_out=2;
static unsigned rx_size,tx_size,reset_in,reset_out,read_error,writes;
static bool overflow,output_fail;
static unsigned cfg_rate=48000,cfg_channels=1;
static int xStreamBufferReset(int b){if(b==1){rx_size=0;reset_in++;}else{tx_size=0;reset_out++;}return 1;}
static unsigned xStreamBufferSend(int b,const void*p,unsigned n,int t){assert(b==1);return overflow?0:n;}
static unsigned xStreamBufferReceive(int b,void*p,unsigned n,int t){assert(b==2);if(n>tx_size)n=tx_size;tx_size-=n;return n;}
static void mic_stop(void){mic_stream=false;}
static void stop(void){writes=0;}
static void mic_begin(void){assert(writes==0);mic_stream=true;mic_rate=cfg_rate;mic_channels=cfg_channels;mic_last_data=now;}
static bool start_output(void){assert(!mic_stream);return !output_fail;}
static bool output(unsigned n){assert(!mic_stream);writes+=n;return !output_fail;}
static int uac_host_device_read(int dev,void*p,unsigned cap,unsigned *got,int timeout){assert(!writes);*got=rx_size;assert(*got<=cap);return read_error;}
static long long esp_timer_get_time(void){return now;}
static unsigned stop_calls,stop_failures,stop_delays;
typedef int uac_host_device_handle_t;
static int uac_host_device_stop(int d){stop_calls++;return stop_calls<=stop_failures?ESP_FAIL:ESP_OK;}
static void vTaskDelay(unsigned t){stop_delays++;}
'''
tail=r'''static void request(int mode){voice_requested=mode;voice_generation++;voice_apply();assert(voice_done==voice_generation&&voice_applied==mode);}
int main(void){
 stop_failures=1;assert(stop_usb_stream(1)&&stop_calls==2&&stop_delays==1);
 stop_calls=stop_delays=0;stop_failures=9;assert(!stop_usb_stream(1)&&stop_calls==5&&stop_delays==5);
 request(VOICE_AUDIO_CAPTURE);assert(mic_stream&&!voice_failed&&reset_in==1);
 rx_size=960;voice_poll();assert(!voice_failed);
 request(VOICE_AUDIO_IDLE);assert(!mic_stream&&reset_in==1); /* retain tail until drained */
 request(VOICE_AUDIO_PLAY);assert(!mic_stream&&rate==48000&&channels==1);tx_size=960;voice_poll();assert(writes==480);
 request(VOICE_AUDIO_CAPTURE);assert(mic_stream&&!writes);overflow=true;rx_size=960;voice_poll();assert(voice_failed);voice_poll();assert(!mic_stream);overflow=false;
 request(VOICE_AUDIO_CAPTURE);now=mic_last_data+2000001;rx_size=0;voice_poll();assert(voice_failed);
 cfg_channels=2;request(VOICE_AUDIO_CAPTURE);assert(voice_failed&&!mic_stream);cfg_channels=1;
 cfg_rate=44100;request(VOICE_AUDIO_CAPTURE);assert(voice_failed&&!mic_stream);cfg_rate=48000;
 request(VOICE_AUDIO_CAPTURE);rx_size=3;voice_poll();assert(voice_failed);
 request(VOICE_AUDIO_PLAY);speaker=0;tx_size=960;voice_poll();assert(voice_failed);speaker=1;
 request(VOICE_AUDIO_OFF);assert(!mic_stream&&!voice_failed&&reset_in>1&&reset_out>1);
 puts("PASS production voice USB arbitration: exclusive RX/TX, fixed-format validation, release-tail preservation, overflow, empty-input timeout, malformed PCM, missing speaker, cancellation cleanup");return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='voice-audio-') as d:
 d=Path(d);(d/'test.c').write_text(head+stop_production+production+tail,encoding='utf-8');exe=d/'test.exe'
 subprocess.run(['C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe','-I'+str(r/'main/services'),str(d/'test.c'),'-o',str(exe)],check=True);subprocess.run([str(exe)],check=True)
