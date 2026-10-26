"""Exercise production server-TTS handoff with queued audio and stop state."""
from pathlib import Path
import tempfile,subprocess
r=Path(__file__).resolve().parents[1]
s=(r/'main/services/xiaozhi_service.c').read_text(encoding='utf-8')
production=s[s.index('static bool handle_json('):s.index('static bool decode_audio(')]
head=r'''#include "xiaozhi_protocol.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define atomic_load(p) (*(p))
#define atomic_store(p,v) (*(p)=(v))
#define VOICE_AUDIO_PLAY 3
#define XZ_SPEAKING 6
#define portMAX_DELAY 0
static int guard,ws=1;
static bool continuous=true,held=true,tts_started,tts_stopped,channel_ready=true,play_ok=true;
static long long response_started=100,drain_since;
static unsigned play_calls;
static char session[128]="session";
static struct {char user[512],answer[1024],emotion[32];} snapshot;
typedef struct {char data[4097];unsigned size;} packet_t;
static bool voice_audio_mode(int mode){assert(mode==VOICE_AUDIO_PLAY);play_calls++;return play_ok;}
static long long esp_timer_get_time(void){return 200;}
static void state(int s,const char*m){assert(s==XZ_SPEAKING);}
static void xSemaphoreTake(int a,int b){}
static void xSemaphoreGive(int a){}
static bool udp_open(const cJSON *o){return false;}
static void mcp_reply(const cJSON *o){}
'''
tail=r'''static bool feed(const char *s){packet_t p;strcpy(p.data,s);p.size=strlen(s);return handle_json(&p);}
int main(void){
 assert(feed("{\"type\":\"tts\",\"state\":\"start\"}"));
 assert(play_calls==1&&tts_started&&!held&&response_started==200);
 assert(feed("{\"type\":\"tts\",\"state\":\"start\"}"));assert(play_calls==1);
 assert(feed("{\"type\":\"tts\",\"state\":\"stop\"}"));assert(tts_stopped&&drain_since==200);
 tts_started=false;held=true;play_ok=false;
 assert(!feed("{\"type\":\"tts\",\"state\":\"start\"}"));assert(!tts_started);
 play_ok=true;response_started=0;unsigned before=play_calls;
 assert(feed("{\"type\":\"tts\",\"state\":\"start\"}"));assert(play_calls==before);
 response_started=100;continuous=false;
 assert(feed("{\"type\":\"tts\",\"state\":\"start\"}"));assert(play_calls==before);
 puts("PASS production automatic TTS handoff, duplicate start, stop, USB failure, stale response, manual mode");return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 d=Path(d);src=d/'test.c';src.write_text(head+production+tail,encoding='utf-8');exe=d/'test.exe'
 j=Path('C:/play/esp32s3touchscreen/toolchains/idf-5.4.1-cn/components/json/cJSON')
 subprocess.run(['C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe','-I'+str(r/'main/services'),'-I'+str(j),str(src),str(r/'main/services/xiaozhi_protocol.c'),str(j/'cJSON.c'),'-o',str(exe)],check=True)
 subprocess.run([str(exe)],check=True)
