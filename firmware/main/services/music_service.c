#include "music_service.h"
#include "service_task.h"
#include "app_services.h"
#include "serial_uart.h"
#include "app_serial.h"
#include "wifi_service.h"
#include "weather.h"
#include "board.h"
#include "ff.h"
#include "usb/usb_host.h"
#include "usb/uac_host.h"
#include "nvs.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <math.h>
#include "esp_crc.h"
#include "mic_meter.h"
#include "esp_timer.h"
#include "voice_audio.h"
#include "xiaozhi_service.h"
#include "freertos/stream_buffer.h"
#include "usb_service.h"
#include "usb_app_mode.h"
#include "keymouse_service.h"
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#define MINIMP3_NO_SIMD
#include "minimp3.h"
typedef struct { int action,value,iface; } command_t;
bool voice_audio_busy(void);
static TaskHandle_t music_task;
static StackType_t *music_stack;
static bool audio_enabled,catalog_enabled;
static atomic_bool stop_requested,worker_ready;
static atomic_int worker_result;
static uint8_t *voice_in_memory,*voice_out_memory;
static QueueHandle_t commands;
static StaticTask_t music_task_control;
static StaticTask_t host_task_control;
static StackType_t *host_stack;
static TaskHandle_t host_task;
static atomic_bool role_save_requested;
static SemaphoreHandle_t guard;
static music_snapshot_t state={.volume=20,.scanning=true};
static atomic_bool host_mode;
static atomic_bool role_requested,role_target,role_finished,accept_devices;
static atomic_int role_result;
static atomic_bool host_stopping,host_stopped;
static atomic_int host_stop_result;
static bool host_installed,uac_installed;
static esp_err_t host_start(void);
static esp_err_t change_role(bool audio);
bool music_role_request(bool audio) {
 if(!guard) return false;
 xSemaphoreTake(guard,portMAX_DELAY);
 if(atomic_load(&role_requested)) { xSemaphoreGive(guard); return false; }
 atomic_store(&role_target,audio); atomic_store(&role_finished,false);
 atomic_store(&role_requested,true); xSemaphoreGive(guard); return true;
}
bool music_role_result(esp_err_t *result) {
 if(!atomic_load(&role_finished)) return false;
 *result=atomic_load(&role_result); return true;
}
static atomic_bool disconnected,transfer_error;
static atomic_bool ui_requested;
bool music_take_ui_request(void) { return atomic_exchange(&ui_requested,false); }
static uac_host_device_handle_t speaker,microphone;
static atomic_bool mic_disconnected,mic_error,mic_ui_requested;
static microphone_snapshot_t mic_state={.dbfs=-96};
static bool mic_stream;
static unsigned mic_rate,mic_channels;
static int64_t mic_last_data;
static uint64_t mic_frames;
void microphone_snapshot(microphone_snapshot_t *out) { if(!guard) { *out=mic_state; return; } xSemaphoreTake(guard,portMAX_DELAY); *out=mic_state; xSemaphoreGive(guard); }
bool microphone_take_ui_request(void) { return atomic_exchange(&mic_ui_requested,false); }
static bool mic_command(int action) { if(!audio_enabled)return false; command_t c={action,0,0}; return commands&&xQueueSend(commands,&c,0)==pdTRUE; }
bool microphone_start(void) { return !voice_audio_busy()&&mic_command(110); }
bool microphone_stop(void) { return !voice_audio_busy()&&mic_command(111); }
bool microphone_reset_peak(void) { return mic_command(112); }
static void mic_stop(void);
static StreamBufferHandle_t voice_in,voice_out;
static StaticStreamBuffer_t voice_in_control,voice_out_control;
static atomic_int voice_requested,voice_applied;
static atomic_uint voice_generation,voice_done;
static atomic_bool voice_failed;
bool voice_audio_init(void) {
 if(voice_in&&voice_out) return true;
 uint8_t *in=heap_caps_malloc(65537,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 uint8_t *out=heap_caps_malloc(98305,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!in||!out) { free(in);free(out);return false; }
 voice_in_memory=in;voice_out_memory=out;
 voice_in=xStreamBufferCreateStatic(65537,2,in,&voice_in_control);
 voice_out=xStreamBufferCreateStatic(98305,2,out,&voice_out_control);
 return voice_in&&voice_out;
}
bool voice_audio_busy(void) { return atomic_load(&voice_requested)!=VOICE_AUDIO_OFF; }
bool voice_audio_mode(voice_audio_mode_t mode) {
 if(!commands||!host_mode||!voice_in||!voice_out) return false;
 atomic_store(&voice_requested,mode);
 unsigned generation=atomic_fetch_add(&voice_generation,1)+1;
 for(unsigned i=0;i<150;i++) {
  if(atomic_load(&voice_done)==generation) return !atomic_load(&voice_failed);
  vTaskDelay(pdMS_TO_TICKS(10));
 }
 atomic_store(&voice_failed,true);atomic_store(&voice_requested,VOICE_AUDIO_OFF);
 atomic_fetch_add(&voice_generation,1);return false;
}
bool voice_audio_ok(void) { return !atomic_load(&voice_failed); }
size_t voice_audio_read(int16_t *p,size_t n,unsigned ms) { return voice_in?xStreamBufferReceive(voice_in,p,n*2,pdMS_TO_TICKS(ms))/2:0; }
size_t voice_audio_write(const int16_t *p,size_t n,unsigned ms) { return voice_out?xStreamBufferSend(voice_out,p,n*2,pdMS_TO_TICKS(ms))/2:0; }
size_t voice_audio_pending(void) { return voice_out?xStreamBufferBytesAvailable(voice_out):0; }


static char (*tracks)[512];
static FIL file;
static bool opened,stream,mp3;
static unsigned rate,channels,wav_remaining;
static uint64_t samples;
static uint8_t *encoded;
static int16_t *pcm,*converted;
static unsigned output_rate,output_channels,resample_phase;
static int16_t previous_l,previous_r;
static bool have_previous;
static unsigned buffered;
static unsigned scanned_entries;
static mp3dec_t *decoder;
static void status(const char *s) { xSemaphoreTake(guard,portMAX_DELAY); snprintf(state.status,sizeof(state.status),"%s",s); xSemaphoreGive(guard); ESP_LOGI("music","%s",s); }
bool music_host_mode(void) { return host_mode; }
void music_snapshot(music_snapshot_t *out) { if(!guard) { *out=state; return; } xSemaphoreTake(guard,portMAX_DELAY); *out=state; xSemaphoreGive(guard); }
void music_track_name(unsigned index,char *out,unsigned size) {
 if(!guard) { if(size) *out=0; return; }
 xSemaphoreTake(guard,portMAX_DELAY);
 const char *p=index<state.count?strrchr(tracks[index],'/'):NULL;
 snprintf(out,size,"%s",p?p+1:""); xSemaphoreGive(guard);
}
bool music_command(music_action_t a,int v) { if(!catalog_enabled&&a!=MUSIC_ROLE&&a!=MUSIC_VOLUME)return false; if(voice_audio_busy()&&a!=MUSIC_VOLUME) return false; command_t c={a,v,0}; return commands&&xQueueSend(commands,&c,0)==pdTRUE; }
static void scan(const char *path,unsigned depth) {
 FF_DIR dir; FILINFO info;
 if(depth>4||f_opendir(&dir,path)!=FR_OK) return;
 while(f_readdir(&dir,&info)==FR_OK&&info.fname[0]) {
  if(++scanned_entries>8192) break;
  if(scanned_entries%16==0) vTaskDelay(pdMS_TO_TICKS(1));
  if(info.fname[0]=='.') continue;
  char next[512]; int n=snprintf(next,sizeof(next),"%s/%s",path,info.fname);
  if(n<0||n>=sizeof(next)) continue;
  if(info.fattrib&AM_DIR) scan(next,depth+1);
  else { const char *ext=strrchr(info.fname,'.');
   if(ext&&(!strcasecmp(ext,".mp3")||!strcasecmp(ext,".wav"))) {
    xSemaphoreTake(guard,portMAX_DELAY);
    if(state.count<MUSIC_MAX_TRACKS) snprintf(tracks[state.count++],512,"%s",next);
    xSemaphoreGive(guard);
   }
  }
  taskYIELD();
 }
 f_closedir(&dir);
}
static void device_event(uac_host_device_handle_t h,uac_host_device_event_t e,void *a) {
 if(e==UAC_HOST_DRIVER_EVENT_DISCONNECTED) atomic_store(&disconnected,true);
 if(e==UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR) atomic_store(&transfer_error,true);
}
static void driver_event(uint8_t addr,uint8_t iface,uac_host_driver_event_t e,void *a) {
 if(!atomic_load(&accept_devices)) return;
 if(e==UAC_HOST_DRIVER_EVENT_TX_CONNECTED||e==UAC_HOST_DRIVER_EVENT_RX_CONNECTED) { command_t c={e==UAC_HOST_DRIVER_EVENT_TX_CONNECTED?100:101,addr,iface}; if(xQueueSend(commands,&c,0)!=pdTRUE) ESP_LOGW("music","USB connect queue full; reconnect headset"); }
}
static void host_events(void *a) {
 for(;;) {
  /* Keep this static task alive between roles; a static TCB must not be
   * reused while the idle task may still be processing its deletion. */
  ulTaskNotifyTake(pdTRUE,portMAX_DELAY);
  for(;;) {
  uint32_t flags=0;
  usb_host_lib_handle_events(pdMS_TO_TICKS(20),&flags);
  if(atomic_load(&host_stopping)) {
   /* Only this daemon handles library events and uninstalls the host. */
   esp_err_t e=usb_host_device_free_all();
   if(e==ESP_OK) {
    uint32_t drained=0;
    usb_host_lib_handle_events(0,&drained);
    e=usb_host_uninstall();
    if(e==ESP_ERR_INVALID_STATE) continue; /* Drain newly completed hub events. */
    atomic_store(&host_stop_result,e);
    if(e!=ESP_OK) atomic_store(&host_stopping,false);
    atomic_store(&host_stopped,true);
    if(e==ESP_OK) break;
   }
  } else if(flags&USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) usb_host_device_free_all();
  }
 }
}
static bool stop_usb_stream(uac_host_device_handle_t device) {
 for(unsigned attempt=0;attempt<5;attempt++){
  if(uac_host_device_stop(device)==ESP_OK)return true;
  vTaskDelay(pdMS_TO_TICKS(20)); /* Let USB client return cancelled transfers. */
 }
 ESP_LOGW("voice","USB interface stop failed after retries");return false;
}
static void stop(void) {
 if(!stream||!speaker||stop_usb_stream(speaker)){stream=false;}
 if(opened){f_close(&file);} opened=false; buffered=0;
 xSemaphoreTake(guard,portMAX_DELAY); state.playing=false; state.paused=false; xSemaphoreGive(guard);
}
static uint16_t le16(const uint8_t *p) { return p[0]|p[1]<<8; }
static uint32_t le32(const uint8_t *p) { return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static bool read_exact(void *out,unsigned n) { UINT got; return f_read(&file,out,n,&got)==FR_OK&&got==n; }
static bool wav_header(void) {
 uint8_t h[40]; bool format=false;
 if(!read_exact(h,12)||memcmp(h,"RIFF",4)||memcmp(h+8,"WAVE",4)) return false;
 while(f_tell(&file)+8<=f_size(&file)) {
  if(!read_exact(h,8)) return false;
  uint32_t n=le32(h+4),pos=f_tell(&file);
  if(n>f_size(&file)-pos) return false;
  if(!memcmp(h,"fmt ",4)) {
   if(n<16||!read_exact(h,16)||le16(h)!=1||le16(h+14)!=16) return false;
   channels=le16(h+2); rate=le32(h+4);
   if((channels!=1&&channels!=2)||rate<8000||rate>48000||le16(h+12)!=channels*2||le32(h+8)!=rate*channels*2) return false;
   format=true;
  } else if(!memcmp(h,"data",4)&&format) { wav_remaining=n; return n>0&&n%(channels*2)==0; }
  if((uint64_t)pos+n+(n&1)>f_size(&file)||f_lseek(&file,pos+n+(n&1))!=FR_OK) return false;
 }
 return false;
}
static void choose(unsigned index) {
 mic_stop(); stop();
 music_snapshot_t s; music_snapshot(&s);
 if(index>=s.count) { status("TF 卡没有 MP3/WAV"); return; }
 xSemaphoreTake(guard,portMAX_DELAY); state.index=index; state.seconds=0; snprintf(state.title,sizeof(state.title),"%s",strrchr(tracks[index],'/')+1); xSemaphoreGive(guard);
 if(!host_mode) { status("请切换 USB 音频模式"); return; }
 if(!speaker) { status("未识别 USB 耳机，请检查接口"); return; }
 if(f_open(&file,tracks[index],FA_READ)!=FR_OK) { status("歌曲读取失败"); return; }
 opened=true; samples=0; rate=0; channels=0; buffered=0;
 const char *ext=strrchr(tracks[index],'.'); mp3=ext&&!strcasecmp(ext,".mp3");
 if(mp3) {
  mp3dec_init(decoder);
  uint8_t id3[10];
  if(read_exact(id3,10)&&!memcmp(id3,"ID3",3)) {
   unsigned n=((id3[6]&127)<<21)|((id3[7]&127)<<14)|((id3[8]&127)<<7)|(id3[9]&127);
   uint64_t end=10u+(uint64_t)n+((id3[5]&16)?10:0);
   if(end>f_size(&file)||f_lseek(&file,end)!=FR_OK) { stop(); status("MP3 标签损坏"); return; }
  } else f_lseek(&file,0);
 } else if(!wav_header()) { stop(); status("WAV 需为 16 位 PCM 单/双声道"); return; }
 xSemaphoreTake(guard,portMAX_DELAY); state.playing=true; state.paused=false; xSemaphoreGive(guard);
 status("正在播放");
}
static bool start_output(void) {
 uac_host_dev_info_t info;
 if(uac_host_get_device_info(speaker,&info)!=ESP_OK) return false;
 int best=-1;
 for(unsigned alt=1;alt<=info.iface_alt_num;alt++) {
  uac_host_dev_alt_param_t p;
  if(uac_host_get_device_alt_param(speaker,alt,&p)!=ESP_OK||p.format!=1||p.bit_resolution!=16||(p.channels!=1&&p.channels!=2)) continue;
  for(unsigned j=0;j<(p.sample_freq_type?p.sample_freq_type:3);j++) {
   unsigned candidate=p.sample_freq_type?p.sample_freq[j]:j==0?48000:j==1?rate:44100;
   if(!p.sample_freq_type&&(candidate<p.sample_freq_lower||candidate>p.sample_freq_upper)) continue;
   if(candidate<8000||candidate>48000) continue;
   int score=(p.channels==2?10:0)+(candidate==48000?4:candidate==rate?3:candidate==44100?2:0);
   if(score>best) { best=score; output_rate=candidate; output_channels=p.channels; }
  }
 }
 if(best<0) { status("耳机不支持 16 位 PCM 输出"); return false; }
 uac_host_stream_config_t cfg={.channels=output_channels,.bit_resolution=16,.sample_freq=output_rate};
 esp_err_t e=uac_host_device_start(speaker,&cfg);
 if(e!=ESP_OK) { ESP_LOGW("music","Stream start: %s",esp_err_to_name(e)); status("USB 音频启动失败"); return false; }
 stream=true; have_previous=false; resample_phase=rate;
 uac_host_device_set_mute(speaker,false);
 xSemaphoreTake(guard,portMAX_DELAY); state.rate=output_rate; state.channels=output_channels; xSemaphoreGive(guard);
 ESP_LOGI("music","PCM input=%u Hz/%u ch output=%u Hz/%u ch; software volume, linear resampler",rate,channels,output_rate,output_channels);
 return true;
}
static bool send_audio(unsigned frames,unsigned volume) {
 for(unsigned i=0;i<frames*output_channels;i++) converted[i]=(int32_t)converted[i]*(int32_t)volume/100;
 if(atomic_load(&disconnected)||uac_host_device_write(speaker,(uint8_t*)converted,frames*output_channels*2,pdMS_TO_TICKS(400))!=ESP_OK) { status("USB 音频传输中断"); return false; }
 return true;
}
static bool output(int frames) {
 if(!frames) return true;
 if(!stream&&!start_output()) return false;
 unsigned volume; xSemaphoreTake(guard,portMAX_DELAY); volume=state.volume; xSemaphoreGive(guard);
 unsigned produced=0;
 for(int i=0;i<frames;i++) {
  int16_t l=pcm[i*channels],r=channels==2?pcm[i*channels+1]:l;
  if(!have_previous) {
   previous_l=l; previous_r=r; have_previous=true;
   if(output_channels==2) { converted[0]=l; converted[1]=r; } else converted[0]=((int32_t)l+r)/2;
   produced=1; continue;
  }
  while(resample_phase<=output_rate) {
   int32_t a=previous_l+(int64_t)((int32_t)l-previous_l)*resample_phase/output_rate;
   int32_t b=previous_r+(int64_t)((int32_t)r-previous_r)*resample_phase/output_rate;
   if(output_channels==2) { converted[produced*2]=a; converted[produced*2+1]=b; } else converted[produced]=(a+b)/2;
   produced++; resample_phase+=rate;
   if(produced==2048) { if(!send_audio(produced,volume)) return false; produced=0; }
  }
  resample_phase-=output_rate; previous_l=l; previous_r=r;
 }
 if(produced&&!send_audio(produced,volume)) return false;
 samples+=frames;
 xSemaphoreTake(guard,portMAX_DELAY); state.seconds=samples/rate; xSemaphoreGive(guard);
 return true;
}
/* Returns 1 for audio, 0 for clean EOF, -1 on corruption/I/O. */
static int decode(void) {
 if(!mp3) {
  if(!wav_remaining) return 0;
  unsigned n=wav_remaining>4096?4096:wav_remaining; UINT got;
  if(f_read(&file,pcm,n,&got)!=FR_OK||got!=n) return -1;
  wav_remaining-=n; return output(n/(channels*2))?1:-1;
 }
 for(unsigned attempts=0;attempts<16;attempts++) {
  UINT got=0;
  if(buffered<16384 && f_read(&file,encoded+buffered,16384-buffered,&got)!=FR_OK) return -1;
  buffered+=got;
  if(!buffered) return samples?0:-1;
  mp3dec_frame_info_t info;
  int frames=mp3dec_decode_frame(decoder,encoded,buffered,pcm,&info);
  if(info.frame_bytes>0) { buffered-=info.frame_bytes; memmove(encoded,encoded+info.frame_bytes,buffered); }
  else { if(f_eof(&file)) return samples?0:-1; buffered=0; }
  if(frames) {
   if(rate&&(rate!=(unsigned)info.hz||channels!=(unsigned)info.channels)) return -1;
   rate=info.hz; channels=info.channels;
   return output(frames)?1:-1;
  }
 }
 return -1;
}
static void mic_status(const char *value) {
 xSemaphoreTake(guard,portMAX_DELAY); snprintf(mic_state.status,sizeof(mic_state.status),"%s",value); xSemaphoreGive(guard); ESP_LOGI("microphone","%s",value);
}
static void mic_event(uac_host_device_handle_t h,uac_host_device_event_t e,void *arg) {
 if(e==UAC_HOST_DRIVER_EVENT_DISCONNECTED) atomic_store(&mic_disconnected,true);
 if(e==UAC_HOST_DEVICE_EVENT_TRANSFER_ERROR) atomic_store(&mic_error,true);
}
static void mic_stop(void) {
 if(!mic_stream||!microphone||stop_usb_stream(microphone))mic_stream=false;
 xSemaphoreTake(guard,portMAX_DELAY); mic_state.active=false; mic_state.level=0; mic_state.dbfs=-96; xSemaphoreGive(guard);
}
static void mic_begin(void) {
 stop(); mic_stop();
 if(stream||mic_stream){mic_status("USB 接口停止失败，请重新连接耳机");return;}
 if(!host_mode) { mic_status("请先切换 USB 音频模式"); return; }
 if(!microphone) { mic_status("未识别耳机麦克风"); return; }
 uac_host_dev_info_t info;
 if(uac_host_get_device_info(microphone,&info)!=ESP_OK) { mic_status("无法读取麦克风信息"); return; }
 int best=-1;
 for(unsigned alt=1;alt<=info.iface_alt_num;alt++) {
  uac_host_dev_alt_param_t p;
  if(uac_host_get_device_alt_param(microphone,alt,&p)!=ESP_OK||p.format!=1||p.bit_resolution!=16||(p.channels!=1&&p.channels!=2)) continue;
  for(unsigned j=0;j<(p.sample_freq_type?p.sample_freq_type:2);j++) {
   unsigned candidate=p.sample_freq_type?p.sample_freq[j]:j?16000:48000;
   if(candidate<8000||candidate>48000||(!p.sample_freq_type&&(candidate<p.sample_freq_lower||candidate>p.sample_freq_upper))) continue;
   int score=(p.channels==1?10:0)+(candidate==48000?3:candidate==16000?2:1);
   if(score>best) { best=score; mic_rate=candidate; mic_channels=p.channels; }
  }
 }
 if(best<0) { mic_status("麦克风不支持 16 位 PCM"); return; }
 uac_host_stream_config_t cfg={.channels=mic_channels,.bit_resolution=16,.sample_freq=mic_rate};
 esp_err_t err=uac_host_device_start(microphone,&cfg);
 if(err!=ESP_OK) { ESP_LOGW("microphone","Start %s",esp_err_to_name(err)); mic_status("麦克风启动失败，请检查 USB"); return; }
 uac_host_device_set_mute(microphone,false);
 mic_stream=true; mic_frames=0; mic_last_data=esp_timer_get_time();
 xSemaphoreTake(guard,portMAX_DELAY); mic_state.active=true; mic_state.rate=mic_rate; mic_state.channels=mic_channels; mic_state.frames=0; mic_state.seconds=0; mic_state.peak_level=0; mic_state.clipped=0; xSemaphoreGive(guard);
 mic_status("测试中，请对麦克风说话");
 ESP_LOGI("microphone","Capture started %u Hz/%u ch/16bit",mic_rate,mic_channels);
}
static void mic_poll(void) {
 uint32_t got=0;
 esp_err_t err=uac_host_device_read(microphone,(uint8_t*)pcm,4096,&got,pdMS_TO_TICKS(20));
 if(err!=ESP_OK&&err!=ESP_FAIL) { mic_stop(); mic_status("麦克风读取失败"); return; }
 if(!got) {
  if(esp_timer_get_time()-mic_last_data>2000000) { mic_stop(); mic_status("麦克风超过两秒无数据"); }
  return;
 }
 if(got%(mic_channels*2)) { mic_stop(); mic_status("麦克风数据格式异常"); return; }
 mic_last_data=esp_timer_get_time();
 mic_meter_result_t meter; mic_meter_process(pcm,got/2,&meter);
 mic_frames+=got/(mic_channels*2);
 xSemaphoreTake(guard,portMAX_DELAY);
 mic_state.level=meter.level; mic_state.dbfs=meter.dbfs;
 if(meter.peak_level>mic_state.peak_level) mic_state.peak_level=meter.peak_level;
 mic_state.clipped+=meter.clipped; mic_state.frames=mic_frames; mic_state.seconds=mic_frames/mic_rate;
 xSemaphoreGive(guard);
}
/* Voice has exclusive ownership of the USB interfaces; never open both streams. */
static void voice_apply(void) {
 unsigned generation=atomic_load(&voice_generation);
 if(generation==atomic_load(&voice_done)) return;
 int mode=atomic_load(&voice_requested);
 mic_stop();stop();
 if(mic_stream||stream){atomic_store(&voice_failed,true);atomic_store(&voice_applied,VOICE_AUDIO_OFF);atomic_store(&voice_done,generation);return;}
 if(mode==VOICE_AUDIO_OFF||mode==VOICE_AUDIO_CAPTURE) xStreamBufferReset(voice_in);
 if(mode==VOICE_AUDIO_OFF||mode==VOICE_AUDIO_PLAY) xStreamBufferReset(voice_out);
 atomic_store(&voice_failed,false);
 if(mode==VOICE_AUDIO_CAPTURE) {
  mic_begin();
  if(!mic_stream||mic_rate!=48000||mic_channels!=1) {
   mic_stop();atomic_store(&voice_failed,true);
  }
 } else if(mode==VOICE_AUDIO_PLAY) {
  rate=48000;channels=1;samples=0;
  if(!speaker||!start_output()) atomic_store(&voice_failed,true);
 }
 atomic_store(&voice_applied,mode);atomic_store(&voice_done,generation);
}
static void voice_poll(void) {
 if(atomic_load(&voice_failed)) { mic_stop();stop();vTaskDelay(pdMS_TO_TICKS(5));return; }
 if(atomic_load(&voice_applied)==VOICE_AUDIO_CAPTURE) {
  if(!microphone||!mic_stream) {atomic_store(&voice_failed,true);return;}
  uint32_t got=0;
  esp_err_t e=uac_host_device_read(microphone,(uint8_t*)pcm,1920,&got,pdMS_TO_TICKS(15));
  if((e!=ESP_OK&&e!=ESP_FAIL)||got%2||got>1920) {ESP_LOGW("voice","USB read error=%d bytes=%u",e,(unsigned)got);atomic_store(&voice_failed,true);return;}
  if(got) {
   mic_last_data=esp_timer_get_time();
   if(xStreamBufferSend(voice_in,pcm,got,0)!=got) atomic_store(&voice_failed,true);
  } else if(esp_timer_get_time()-mic_last_data>2000000) {ESP_LOGW("voice","USB microphone delivered no PCM for 2 seconds");atomic_store(&voice_failed,true);}
 } else if(atomic_load(&voice_applied)==VOICE_AUDIO_PLAY) {
  unsigned bytes=xStreamBufferReceive(voice_out,pcm,1920,0);
  if(bytes&&(!speaker||!output(bytes/2))) atomic_store(&voice_failed,true);
 }
 vTaskDelay(pdMS_TO_TICKS(2));
}
static void put16(uint8_t *p,uint16_t v) { p[0]=v; p[1]=v>>8; }
static void put32(uint8_t *p,uint32_t v) { put16(p,v); put16(p+2,v>>16); }
/* Invoked only by an explicit serial command following a user request.
 * CREATE_NEW prevents replacement; neither formatting nor unlink is used. */
static void generate_test(void) {
 mic_stop(); stop(); status("正在生成 10 秒测试音乐");
 char path[80]; FIL out; FRESULT result=FR_EXIST; bool created=false;
 board_storage_allow_test_write(true);
 for(unsigned i=0;i<100&&result==FR_EXIST;i++) {
  if(i) snprintf(path,sizeof(path),"0:/TouchDesk_Test_10s_%02u.wav",i);
  else snprintf(path,sizeof(path),"0:/TouchDesk_Test_10s.wav");
  result=f_open(&out,path,FA_WRITE|FA_CREATE_NEW);
 }
 uint32_t expected_crc=0;
 if(result==FR_OK) {
  created=true;
  uint8_t h[44]={0}; memcpy(h,"RIFF",4); put32(h+4,1920036); memcpy(h+8,"WAVEfmt ",8);
  put32(h+16,16);put16(h+20,1);put16(h+22,2);put32(h+24,48000);put32(h+28,192000);put16(h+32,4);put16(h+34,16);memcpy(h+36,"data",4);put32(h+40,1920000);
  UINT written=0; result=f_write(&out,h,sizeof(h),&written);
  if(result==FR_OK&&written!=sizeof(h)) result=FR_DISK_ERR;
  expected_crc=esp_crc32_le(0,h,sizeof(h));
  static const float notes[20]={261.6256f,329.6276f,391.9954f,523.2511f,391.9954f,293.6648f,391.9954f,493.8833f,587.3295f,493.8833f,220.0f,261.6256f,329.6276f,440.0f,329.6276f,174.6141f,261.6256f,349.2282f,391.9954f,523.2511f};
  static const float chords[4][3]={{130.8128f,164.8138f,195.9977f},{146.8324f,195.9977f,246.9417f},{110.0f,130.8128f,164.8138f},{87.3071f,130.8128f,174.6141f}};
  for(unsigned start=0;start<480000&&result==FR_OK;start+=2048) {
   unsigned frames=480000-start; if(frames>2048) frames=2048;
   for(unsigned j=0;j<frames;j++) {
    unsigned sample=start+j,note=sample/24000; float time=(float)sample/48000,local=(float)(sample%24000)/48000;
    float env=fminf(1.0f,fminf(local/.012f,(.5f-local)/.08f));
    float phase=6.28318530718f*notes[note]*local;
    float value=env*(.72f*sinf(phase)+.10f*sinf(2*phase)+.03f*sinf(3*phase));
    unsigned chord=sample/120000;
    for(unsigned k=0;k<3;k++) value+=.05f*sinf(6.28318530718f*chords[chord][k]*time);
    float fade=fminf(1.0f,fminf(time/.02f,(10-time)/.1f));
    int16_t v=(int16_t)(12000*value*fade); pcm[j*2]=pcm[j*2+1]=v;
   }
   unsigned bytes=frames*4; expected_crc=esp_crc32_le(expected_crc,(uint8_t*)pcm,bytes);
   result=f_write(&out,pcm,bytes,&written);
   if(result==FR_OK&&written!=bytes) result=FR_DISK_ERR;
   vTaskDelay(pdMS_TO_TICKS(1));
  }
  if(result==FR_OK) result=f_sync(&out);
  FRESULT close_result=f_close(&out); if(result==FR_OK) result=close_result;
 }
 board_storage_allow_test_write(false);
 if(result!=FR_OK) { ESP_LOGE("music_test","Generation result=%d created=%u; writes disabled",result,created); status("测试音乐写入失败，已恢复只读"); return; }
 /* Reopen and verify every byte through the normal CRC-checked SD reader. */
 uint32_t actual_crc=0; result=f_open(&out,path,FA_READ);
 if(result==FR_OK) {
  if(f_size(&out)!=1920044) result=FR_INT_ERR;
  UINT got=0;
  while(result==FR_OK&&!f_eof(&out)) {
   result=f_read(&out,encoded,16384,&got);
   if(result==FR_OK) { actual_crc=esp_crc32_le(actual_crc,encoded,got); if(!got&&!f_eof(&out)) result=FR_DISK_ERR; }
   vTaskDelay(pdMS_TO_TICKS(1));
  }
  f_close(&out);
 }
 if(result!=FR_OK||actual_crc!=expected_crc) { status("测试文件回读校验失败"); return; }
 xSemaphoreTake(guard,portMAX_DELAY);
 unsigned keep=state.count<MUSIC_MAX_TRACKS?state.count:MUSIC_MAX_TRACKS-1;
 memmove(tracks[1],tracks[0],keep*512);
 snprintf(tracks[0],512,"%s",path); state.count=keep+1; state.index=0; state.seconds=0; state.catalog_revision++;
 snprintf(state.title,sizeof(state.title),"%s",strrchr(path,'/')+1);
 xSemaphoreGive(guard);
 ESP_LOGI("music_test","PASS file=%s bytes=1920044 duration=10.000 rate=48000 channels=2 bits=16 CRC32=%08lx full readback; write-protected",path,(unsigned long)actual_crc);
 status("10 秒测试音乐已生成，点击播放");
}
static esp_err_t stop_host_library(void) {
 if(!host_installed) return ESP_OK;
 atomic_store(&host_stopped,false); atomic_store(&host_stopping,true);
 usb_host_lib_unblock();
 for(unsigned i=0;i<500&&!atomic_load(&host_stopped);i++) vTaskDelay(pdMS_TO_TICKS(10));
 if(!atomic_load(&host_stopped)) return ESP_ERR_TIMEOUT;
 esp_err_t e=atomic_load(&host_stop_result);
 if(e==ESP_OK) host_installed=false;
 return e;
}
static esp_err_t host_start(void) {
 if(host_installed || uac_installed) return ESP_ERR_INVALID_STATE;
 usb_host_config_t host={.skip_phy_setup=false,.intr_flags=ESP_INTR_FLAG_LEVEL1};
 esp_err_t e=usb_host_install(&host); if(e!=ESP_OK) return e;
 host_installed=true; atomic_store(&host_stopping,false); atomic_store(&host_stopped,false);
 if(!host_task) {
  if(!host_stack)host_stack=heap_caps_malloc(4096,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  if(host_stack)host_task=xTaskCreateStaticPinnedToCore(host_events,"usb_host",4096,NULL,6,host_stack,&host_task_control,tskNO_AFFINITY);
 }
 if(!host_task) {
  usb_host_device_free_all(); e=usb_host_uninstall();
  if(e==ESP_OK) host_installed=false;
  return ESP_ERR_NO_MEM;
 }
 xTaskNotifyGive(host_task);
 atomic_store(&accept_devices,true);
 if(audio_enabled) {
 uac_host_driver_config_t cfg={.create_background_task=true,.task_priority=5,.stack_size=6144,.core_id=0,.callback=driver_event};
 e=uac_host_install(&cfg);
 if(e!=ESP_OK) { atomic_store(&accept_devices,false); stop_host_library(); return e; }
 uac_installed=true;
 }
 /* The keymouse app explicitly starts the HID client and BLE. */
 ESP_LOGI("music","USB Host ready; audio=%u",audio_enabled);
 return ESP_OK;
}
static esp_err_t host_stop(void) {
 atomic_store(&accept_devices,false);
 mic_stop(); stop();
 if(mic_stream||stream) { atomic_store(&accept_devices,true); return ESP_ERR_INVALID_STATE; }
 esp_err_t e;
 if(microphone) {
  e=uac_host_device_close(microphone); if(e!=ESP_OK) return e;
  microphone=NULL;
 }
 if(speaker) {
  e=uac_host_device_close(speaker); if(e!=ESP_OK) return e;
  speaker=NULL;
 }
 e=keymouse_host_prepare_stop();
 if(e!=ESP_OK){keymouse_host_start();atomic_store(&accept_devices,true);return e;}
 /* Disable the controller root port to deliver normal disconnects to both
  * class clients before uninstalling the shared Host library. */
 e=usb_host_lib_set_root_port_power(false);
 if(e!=ESP_OK){keymouse_host_start();atomic_store(&accept_devices,true);return e;}
 e=keymouse_host_stop();
 if(e!=ESP_OK){keymouse_host_start();atomic_store(&accept_devices,true);usb_host_lib_set_root_port_power(true);return e;}
 if(uac_installed) {
  e=uac_host_uninstall();
  if(e!=ESP_OK){keymouse_host_start();atomic_store(&accept_devices,true);usb_host_lib_set_root_port_power(true);return e;}
  uac_installed=false;
 }
 e=stop_host_library();
 atomic_store(&disconnected,false); atomic_store(&transfer_error,false);
 atomic_store(&mic_disconnected,false); atomic_store(&mic_error,false);
 xSemaphoreTake(guard,portMAX_DELAY);
 state.connected=false; state.device[0]=0; state.rate=0;
 mic_state.connected=false; mic_state.device[0]=0; mic_state.active=false;
 xSemaphoreGive(guard);
 return e;
}
static esp_err_t change_role(bool audio) {
 if(audio==atomic_load(&host_mode)) return ESP_OK;
 esp_err_t e=audio?usb_service_stop():host_stop();
 if(e!=ESP_OK) return e;
 xQueueReset(commands); /* Discard old events before installing the new role. */
 /* Both stacks have released the controller and PHY before the other starts. */
 vTaskDelay(pdMS_TO_TICKS(100));
 e=audio?host_start():usb_service_start();
 if(e!=ESP_OK) {
  esp_err_t recovery=audio?usb_service_start():host_start();
  ESP_LOGE("usb_role","start failed=%s; restore=%s",esp_err_to_name(e),esp_err_to_name(recovery));
  return e;
 }
 atomic_store(&host_mode,audio);
 xSemaphoreTake(guard,portMAX_DELAY); state.host=audio; xSemaphoreGive(guard);
 status(audio?"USB 音频模式已就绪":"USB 键盘模式已就绪");
 ESP_LOGI("usb_role","switched audio=%u without restart; uptime=%lld",audio,(long long)esp_timer_get_time());
 atomic_store(&role_save_requested,true);
 return ESP_OK;
}
/* Flash operations run on the main task's internal stack. The large MP3
 * decoder stack lives in PSRAM and must not disable its own cache for NVS. */
void music_role_poll(void) {
 if(!atomic_exchange(&role_save_requested,false))return;
 nvs_handle_t n;
 esp_err_t saved=nvs_open("usb_audio",NVS_READWRITE,&n);
 if(saved==ESP_OK) {
  saved=nvs_set_u8(n,"host",atomic_load(&host_mode));
  if(saved==ESP_OK)saved=nvs_commit(n);
  nvs_close(n);
 }
 if(saved!=ESP_OK)ESP_LOGW("usb_role","mode persistence failed: %s",esp_err_to_name(saved));
}
static void worker(void *arg) {
 if(catalog_enabled) {
  tracks=heap_caps_calloc(MUSIC_MAX_TRACKS,512,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  encoded=heap_caps_malloc(16384,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  decoder=heap_caps_malloc(sizeof(*decoder),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
  converted=heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 }
 if(audio_enabled)pcm=heap_caps_malloc(8192,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if((audio_enabled&&!pcm)||(catalog_enabled&&(!tracks||!encoded||!decoder||!converted))) {
  status("音乐内存不足");
  atomic_store(&worker_result,ESP_ERR_NO_MEM);atomic_store(&worker_ready,true);
  for(;;)vTaskSuspend(NULL);
 }
 atomic_store(&worker_result,ESP_OK);atomic_store(&worker_ready,true);
 if(catalog_enabled) {
  if(board_storage_mount()==ESP_OK) { scan("0:",0);status("TF 歌曲扫描完成"); }
  else status("TF 只读挂载失败，请重启检查");
 }
 xSemaphoreTake(guard,portMAX_DELAY);state.scanning=false;state.host=host_mode;xSemaphoreGive(guard);
 for(;;) {
  if(atomic_load(&stop_requested)) { mic_stop(); stop(); for(;;) vTaskSuspend(NULL); }
  if(voice_in) voice_apply();
  if(voice_audio_busy()&&(atomic_load(&mic_disconnected)||atomic_load(&mic_error)||atomic_load(&disconnected)||atomic_load(&transfer_error))) atomic_store(&voice_failed,true);
  if(atomic_exchange(&mic_disconnected,false)) { mic_stop(); if(microphone) uac_host_device_close(microphone); microphone=NULL; xSemaphoreTake(guard,portMAX_DELAY); mic_state.connected=false; xSemaphoreGive(guard); mic_status("麦克风已断开"); }
  if(atomic_exchange(&mic_error,false)) { mic_stop(); mic_status("麦克风 USB 传输错误"); }
  if(atomic_exchange(&disconnected,false)) { stop(); if(speaker) uac_host_device_close(speaker); speaker=NULL; xSemaphoreTake(guard,portMAX_DELAY); state.connected=false; state.device[0]=0; xSemaphoreGive(guard); status("USB 耳机已断开"); }
  if(atomic_exchange(&transfer_error,false)) { stop(); status("USB 音频错误，请重新播放"); }
  if(atomic_load(&role_requested)&&!voice_audio_busy()) {
   esp_err_t e=change_role(atomic_load(&role_target));
   atomic_store(&role_result,e); atomic_store(&role_requested,false); atomic_store(&role_finished,true);
   if(e!=ESP_OK) ESP_LOGE("usb_role","switch failed: %s",esp_err_to_name(e));
   continue;
  }
  command_t c; music_snapshot_t s; music_snapshot(&s);
  if(xQueueReceive(commands,&c,pdMS_TO_TICKS((s.playing&&!s.paused)||mic_stream||voice_audio_busy()?0:30))==pdTRUE) {
   if(voice_audio_busy()&&c.action<100&&c.action!=MUSIC_VOLUME) continue;
   if(voice_audio_busy()&&c.action>=110&&c.action<=112) continue;
   if(c.action==110) mic_begin();
   else if(c.action==111) { mic_stop(); mic_status("测试已停止"); }
   else if(c.action==112) { xSemaphoreTake(guard,portMAX_DELAY); mic_state.peak_level=0; mic_state.clipped=0; xSemaphoreGive(guard); }
   else if(c.action==101) {
    if(microphone) continue;
    uac_host_device_config_t cfg={.addr=c.value,.iface_num=c.iface,.buffer_size=8192,.buffer_threshold=1024,.callback=mic_event};
    if(uac_host_device_open(&cfg,&microphone)==ESP_OK) {
     uac_host_dev_info_t info; uac_host_get_device_info(microphone,&info); uac_host_printf_device_param(microphone);
     xSemaphoreTake(guard,portMAX_DELAY); mic_state.connected=true; snprintf(mic_state.device,sizeof(mic_state.device),"UAC1 %04X:%04X",info.VID,info.PID); xSemaphoreGive(guard); mic_status("耳机麦克风已就绪");
    } else mic_status("麦克风打开失败");
   }
   else if(c.action==MUSIC_GENERATE_TEST) generate_test();
   else if(c.action==100) {
    if(speaker) continue;
    uac_host_device_config_t cfg={.addr=c.value,.iface_num=c.iface,.buffer_size=16384,.buffer_threshold=4096,.callback=device_event};
    if(uac_host_device_open(&cfg,&speaker)==ESP_OK) {
     uac_host_dev_info_t info; uac_host_get_device_info(speaker,&info); uac_host_printf_device_param(speaker);
     xSemaphoreTake(guard,portMAX_DELAY); state.connected=true; snprintf(state.device,sizeof(state.device),"UAC1 %04X:%04X",info.VID,info.PID); xSemaphoreGive(guard); status("USB 耳机已就绪");
    } else status("USB 音频设备打开失败");
   } else if(c.action==MUSIC_VOLUME) { xSemaphoreTake(guard,portMAX_DELAY); state.volume=c.value<0?0:c.value>100?100:c.value; xSemaphoreGive(guard); }
   else if(c.action==MUSIC_ROLE) {
    music_role_request(!atomic_load(&host_mode));
   } else if(c.action==MUSIC_SELECT) choose(c.value);
   else if(c.action==MUSIC_NEXT&&s.count) choose((s.index+1)%s.count);
   else if(c.action==MUSIC_PREVIOUS&&s.count) choose((s.index+s.count-1)%s.count);
   else if(c.action==MUSIC_PLAY) { mic_stop(); if(s.paused&&opened) {
    if(stream&&uac_host_device_resume(speaker)!=ESP_OK) { stop(); status("恢复播放失败"); continue; }
    xSemaphoreTake(guard,portMAX_DELAY); state.paused=false; xSemaphoreGive(guard); status("正在播放");
   } else if(!s.playing) choose(s.index); }
   else if(c.action==MUSIC_PAUSE&&s.playing) {
    if(stream&&uac_host_device_suspend(speaker)!=ESP_OK) { stop(); status("暂停失败"); continue; }
    xSemaphoreTake(guard,portMAX_DELAY); state.paused=true; xSemaphoreGive(guard); status("已暂停");
   }
  }
  if(voice_audio_busy()) { voice_poll();continue; }
  if(mic_stream) { vTaskDelay(pdMS_TO_TICKS(10)); mic_poll(); }
  music_snapshot(&s);
  if(s.playing&&!s.paused&&speaker) { int result=decode(); if(result<=0) {
   /* The UAC write queues samples; let its bounded ring buffer drain at EOF. */
   if(result==0&&rate) vTaskDelay(pdMS_TO_TICKS(16384u*1000/(output_rate*output_channels*2)+15));
   stop(); if(result==0) { status("播放完成"); if(s.count>1) choose((s.index+1)%s.count); } else { music_snapshot_t failed; music_snapshot(&failed); if(!strcmp(failed.status,"正在播放")) status("歌曲损坏或 TF 读取失败"); }
  } }
 }
}
static void console(void *arg) {
 char line[80]; unsigned used=0;
 for(;;) {
  if(serial_uart_console_step()){used=0;continue;}
  int c=getchar();
  if(c==EOF) { clearerr(stdin); vTaskDelay(pdMS_TO_TICKS(20)); continue; }
  if(c=='\r'||c=='\n') {
   line[used]=0; used=0;
   app_services_lock();
   if(!strcmp(line,"serial curve")) { serial_app_curve_request(); }
   else if(!strcmp(line,"wireless test")) { wireless_app_selftest_request(); }
   else if(!strcmp(line,"serial test")) { serial_app_selftest_request(); }
   else if(usb_app_mode_console(line)) { /* UI navigation only; never submits HID. */ }
   else if(!strcmp(line,"services status")) app_services_status();
   else if(!strcmp(line,"weather status")) {
    weather_snapshot_t w;wifi_status_t n;weather_snapshot(&w);wifi_service_status(&n);
    ESP_LOGI("weather_status","enabled=%u saved=%u net_state=%u network=%u active=%u valid=%u time_valid=%u phase=%u http=%d fetched=%lld",
     n.enabled,n.saved_count,n.state,w.network,w.active,w.data.valid,w.time_valid,w.phase,w.http_status,(long long)w.data.fetched);
   }
   else if(!strcmp(line,"weather refresh")) { ESP_LOGI("weather_status","refresh_requested=%u",weather_refresh()); }
   else if(!strcmp(line,"usb status")) ESP_LOGI("usb_status","host=%u device_online=%u",music_host_mode(),usb_service_online());
   else if(xiaozhi_console(line)) { /* Diagnostics contain no credentials. */ }
   else if(!strcmp(line,"mic ui")) atomic_store(&mic_ui_requested,true);
   else if(!strcmp(line,"mic start")) microphone_start();
   else if(!strcmp(line,"mic stop")) microphone_stop();
   else if(!strcmp(line,"mic status")) {
    microphone_snapshot_t m; microphone_snapshot(&m);
    ESP_LOGI("mic_status","connected=%u active=%u rate=%u ch=%u level=%u peak=%u dBFS=%.1f frames=%u seconds=%u clips=%u status=%s",m.connected,m.active,m.rate,m.channels,m.level,m.peak_level,(double)m.dbfs,m.frames,m.seconds,m.clipped,m.status);
   }
   else if(!strcmp(line,"music status")) {
    music_snapshot_t s; music_snapshot(&s);
    ESP_LOGI("music_status","host=%u connected=%u tracks=%u index=%u playing=%u paused=%u seconds=%u rate=%u volume=%u status=%s",s.host,s.connected,s.count,s.index,s.playing,s.paused,s.seconds,s.rate,s.volume,s.status);
   } else if(!strcmp(line,"music generate-test")) music_command(MUSIC_GENERATE_TEST,0);
   else if(!strcmp(line,"music ui")) atomic_store(&ui_requested,true);
   else if(!strcmp(line,"music play")) music_command(MUSIC_PLAY,0);
   else if(!strcmp(line,"music pause")) music_command(MUSIC_PAUSE,0);
   else if(!strcmp(line,"music next")) music_command(MUSIC_NEXT,0);
   else if(!strcmp(line,"music role")) music_command(MUSIC_ROLE,0);
   else if(!strncmp(line,"music select ",13)) music_command(MUSIC_SELECT,atoi(line+13));
   app_services_unlock();
  } else if(c>=32&&c<127&&used<sizeof(line)-1) line[used++]=c;
 }
}
esp_err_t uart_console_init(void) {
 esp_err_t e=app_services_init();if(e!=ESP_OK)return e;
 e=serial_uart_console_init();if(e!=ESP_OK)return e;
 return xTaskCreate(console,"uart_console",4096,NULL,2,NULL)==pdPASS?ESP_OK:ESP_ERR_NO_MEM;
}
esp_err_t music_set_role(bool audio) { return change_role(audio); }
esp_err_t music_deinit(void) {
 if(music_task) {
  if(voice_audio_busy()) return ESP_ERR_INVALID_STATE;
  atomic_store(&stop_requested,true);
  esp_err_t e=service_task_join(&music_task,30000);if(e!=ESP_OK)return e;
 }
 /* Class callbacks may still enqueue until the complete Host teardown succeeds. */
 if(host_installed) { esp_err_t e=host_stop();if(e!=ESP_OK)return e; }
 if(host_task) {
  /* Host stop acknowledgement precedes return to its outer notification wait.
   * Join that wait before deleting/freeing the external stack. */
  unsigned waits=0;
  while(eTaskGetState(host_task)!=eBlocked) {
   if(waits++>=500)return ESP_ERR_TIMEOUT;
   vTaskDelay(pdMS_TO_TICKS(10));
  }
  vTaskDelete(host_task);host_task=NULL;
 }
 free(host_stack);host_stack=NULL;free(music_stack);music_stack=NULL;
 free(tracks);tracks=NULL;free(encoded);encoded=NULL;free(pcm);pcm=NULL;free(converted);converted=NULL;free(decoder);decoder=NULL;
 if(voice_in) { vStreamBufferDelete(voice_in); }
 if(voice_out) { vStreamBufferDelete(voice_out); }
 voice_in=voice_out=NULL;
 free(voice_in_memory);voice_in_memory=NULL;free(voice_out_memory);voice_out_memory=NULL;
 if(commands) { vQueueDelete(commands); }
 commands=NULL;
 if(guard) { vSemaphoreDelete(guard); }
 guard=NULL;
 unsigned volume=state.volume;state=(music_snapshot_t){.volume=volume};mic_state=(microphone_snapshot_t){.dbfs=-96};
 atomic_store(&host_mode,false);atomic_store(&role_requested,false);atomic_store(&role_finished,false);
 atomic_store(&voice_requested,VOICE_AUDIO_OFF);atomic_store(&voice_applied,VOICE_AUDIO_OFF);
 atomic_store(&voice_generation,0);atomic_store(&voice_done,0);atomic_store(&voice_failed,false);
 return ESP_OK;
}
void music_configure(bool audio,bool catalog) { audio_enabled=audio;catalog_enabled=catalog; }
esp_err_t music_init(void) {
 if(music_task) return atomic_load(&stop_requested)?ESP_ERR_INVALID_STATE:ESP_OK;
 if(guard) return ESP_ERR_INVALID_STATE; /* Failed teardown must be retried, not overwritten. */
 atomic_store(&stop_requested,false);atomic_store(&worker_ready,false);state.scanning=true;
 guard=xSemaphoreCreateMutex();commands=xQueueCreate(16,sizeof(command_t));
 if(!guard||!commands) { music_deinit();return ESP_ERR_NO_MEM; }
 music_stack=heap_caps_malloc(32768,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!music_stack) { music_deinit();return ESP_ERR_NO_MEM; }
 music_task=xTaskCreateStaticPinnedToCore(worker,"music",32768,NULL,4,music_stack,&music_task_control,1);
 if(!music_task) { music_deinit();return ESP_ERR_NO_MEM; }
 for(unsigned i=0;i<100&&!atomic_load(&worker_ready);i++)vTaskDelay(pdMS_TO_TICKS(10));
 if(!atomic_load(&worker_ready))return ESP_ERR_TIMEOUT;
 esp_err_t e=atomic_load(&worker_result);if(e!=ESP_OK)music_deinit();return e;
}
