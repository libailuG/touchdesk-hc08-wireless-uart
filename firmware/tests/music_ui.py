from pathlib import Path
import subprocess,tempfile,os
from ui_simulator import HEADERS,EXTRA_HEADERS,HOST,ROOT
from weather_ui import MOCK
MUSIC=r'''
#include "music_service.h"
#include "xiaozhi_service.h"
extern const app_desc_t app_xiaozhi;
static xiaozhi_snapshot_t chat={.available=true,.state=XZ_READY,.status="已连接 · 按住下方按钮说话",.user="你好，请介绍一下自己",.answer="你好，我是小智，可以陪你聊天，也可以回答问题。"};
static unsigned chat_connections,chat_cancels,chat_leaves;
void xiaozhi_snapshot(xiaozhi_snapshot_t *out){*out=chat;}
void xiaozhi_connect(void){chat_connections++;}
void xiaozhi_continuous_start(void){chat.continuous=true;}
void xiaozhi_hold(bool on){chat.held=on;}
void xiaozhi_cancel(void){chat_cancels++;chat.held=false;chat.continuous=false;}
void xiaozhi_leave(void){chat_leaves++;chat.held=false;}

extern const app_desc_t app_music;
static music_snapshot_t music={.host=true,.connected=true,.volume=20,.count=2,.rate=44100,.title="晴天 - 示例音乐.mp3",.device="UAC1 1234:5678",.status="USB 耳机已就绪"};
static int music_commands;
bool music_host_mode(void) { return music.host; }
void music_snapshot(music_snapshot_t *o) { *o=music; }
void music_track_name(unsigned i,char *out,unsigned size) { snprintf(out,size,"%s",i?"Long song filename - 演示第二首.wav":"晴天 - 示例音乐.mp3"); }
bool music_command(music_action_t a,int v) { music_commands++; if(a==MUSIC_PLAY) {music.playing=true;music.paused=false;} if(a==MUSIC_PAUSE) music.paused=true; if(a==MUSIC_NEXT) music.index=(music.index+1)%2; if(a==MUSIC_VOLUME) music.volume=v; return true; }
extern const app_desc_t app_microphone;
static microphone_snapshot_t mic={.connected=true,.active=true,.rate=48000,.channels=1,.level=65,.peak_level=82,.seconds=5,.frames=240000,.dbfs=-21,.device="UAC1 001F:0B21",.status="测试中，请对麦克风说话"};
void microphone_snapshot(microphone_snapshot_t *out) { *out=mic; }
bool microphone_start(void) { mic.active=true; return true; }
bool microphone_stop(void) { mic.active=false; return true; }
bool microphone_reset_peak(void) { mic.peak_level=0;mic.clipped=0;return true; }
int main(void) {
 lv_init(); static lv_disp_draw_buf_t buf; static lv_disp_drv_t drv;
 extern const lv_font_t font_ui;lv_font_glyph_dsc_t glyph;
 static const uint32_t covered[]={FONT_COVERAGE_CODEPOINTS};
 for(unsigned i=0;i<sizeof(covered)/sizeof(covered[0]);i++)assert(font_ui.get_glyph_dsc(&font_ui,&glyph,covered[i],0));
 lv_disp_draw_buf_init(&buf,draw,NULL,320*40); lv_disp_drv_init(&drv); drv.hor_res=320;drv.ver_res=480;drv.flush_cb=flush;drv.draw_buf=&buf; assert(lv_disp_drv_register(&drv));
 shell_init();app_process();
 for(unsigned theme=0;theme<3;theme++) {
  weather_choose_theme(theme);app_request(NULL);app_process();settle();
  char path[40];snprintf(path,sizeof(path),"desktop-music-%u",theme);snapshot(path);
  app_request(&app_music);app_process();settle();
  snprintf(path,sizeof(path),"music-%u",theme);snapshot(path);check_bounds(content());
 }
 for(unsigned theme=0;theme<3;theme++) {
  weather_choose_theme(theme);app_request(&app_xiaozhi);app_process();settle();
  char name[40];snprintf(name,sizeof(name),"xiaozhi-%u",theme);snapshot(name);check_bounds(content());
 }
 strcpy(chat.user,"鹦鹉和麒麟是什么？");
 strcpy(chat.answer,"鹦鹉是鸟类，麒麟是传说中的瑞兽。鲨鱼、蝴蝶、蜻蜓也各有特点。");
 snapshot("xiaozhi-font-coverage");
 lv_obj_t *chat_root=content();
 click(lv_obj_get_child(chat_root,4));assert(chat_connections==1);
 lv_obj_t *talk=lv_obj_get_child(chat_root,6);
 lv_event_send(talk,LV_EVENT_PRESSED,NULL);assert(!chat.continuous);
 lv_event_send(talk,LV_EVENT_RELEASED,NULL);assert(!chat.continuous);
 click(talk);assert(chat.continuous);
 chat.state=XZ_SPEAKING;settle();assert(!lv_obj_has_state(talk,LV_STATE_DISABLED));
 click(talk);assert(!chat.continuous&&chat_cancels==1);
 click(talk);assert(chat.continuous);
 click(lv_obj_get_child(chat_root,5));assert(chat_cancels==2&&!chat.continuous);
 chat.state=XZ_ACTIVATING;strcpy(chat.activation,"123456");strcpy(chat.status,"打开 xiaozhi.me，添加设备并输入绑定码");snapshot("xiaozhi-activation");assert(lv_obj_has_state(talk,LV_STATE_DISABLED));
 chat.state=XZ_LISTENING;strcpy(chat.status,"正在听 · 松开发送，最长30秒");snapshot("xiaozhi-listening");
 chat.state=XZ_ERROR;strcpy(chat.status,"连接已中断，请点连接重试");snapshot("xiaozhi-error");
 app_request(NULL);app_process();assert(chat_leaves>=1);
 for(unsigned theme=0;theme<3;theme++) {
  weather_choose_theme(theme);app_request(&app_microphone);app_process();settle();
  char name[40];snprintf(name,sizeof(name),"microphone-%u",theme);snapshot(name);check_bounds(content());assert(!strcmp(lv_label_get_text(lv_obj_get_child(content(),2)),"-21.0 dBFS"));
 }
 click(lv_obj_get_child(content(),7));assert(!mic.active);click(lv_obj_get_child(content(),7));assert(mic.active);
 click(lv_obj_get_child(content(),8));assert(!mic.peak_level);
 app_request(&app_music);app_process();settle();assert(!mic.active);
 lv_obj_t *root=content();click(lv_obj_get_child(root,6));assert(music.playing);click(lv_obj_get_child(root,6));assert(music.paused);
 snapshot("music-paused");
 lv_obj_t *slider=lv_obj_get_child(root,9);lv_slider_set_value(slider,35,LV_ANIM_OFF);lv_event_send(slider,LV_EVENT_VALUE_CHANGED,NULL);assert(music.volume==35);
 lv_dropdown_open(lv_obj_get_child(root,3));snapshot("music-song-list");lv_dropdown_close(lv_obj_get_child(root,3));
 music.connected=false;snprintf(music.status,sizeof(music.status),"未识别 USB 耳机，请检查接口");snapshot("music-no-headset");
 music.host=false;app_request(&app_music);app_process();snapshot("music-hid-mode");
 music.count=0;app_request(&app_music);app_process();snapshot("music-empty");
 app_request(NULL);app_process();settle();lv_mem_monitor_t before,after;lv_mem_monitor(&before);
 for(int i=0;i<300;i++) { app_request(i%3==0?&app_xiaozhi:i%3==1?&app_music:&app_microphone);app_process();settle();app_request(NULL);app_process();settle(); }
 lv_mem_monitor(&after);assert(after.free_size==before.free_size);assert(lv_mem_test()==LV_RES_OK);assert(keys==0);
 printf("PASS music, microphone and XiaoZhi real LVGL: themes, controls, list, empty, HID mode, disconnected, 300 lifecycles, heap %u -> %u, no HID reports\n",before.free_size,after.free_size);
 return 0;
}
'''
coverage=set()
for lead in range(0xa1,0xf8):
 for trail in range(0xa1,0xff):
  try:coverage.update(map(ord,bytes([lead,trail]).decode('gb2312')))
  except UnicodeDecodeError:pass
MUSIC=MUSIC.replace('FONT_COVERAGE_CODEPOINTS',','.join(map(str,sorted(coverage))))
baseline=os.environ.get('TOUCHDESK_RESOURCE_BASELINE')
out=Path(os.environ.get('TOUCHDESK_PREVIEW_OUTPUT',str(ROOT/'docs/music-previews')));out.mkdir(parents=True,exist_ok=True)
resource_host=r'''
#include "resource_store.h"
#include "resource_manifest.h"
static uint8_t *resource_bytes;
const uint8_t *resource_store_data(size_t offset,size_t length){
 assert(resource_bytes && offset>=64 && offset<=RESOURCE_BUNDLE_SIZE && length<=RESOURCE_BUNDLE_SIZE-offset);
 return resource_bytes+offset;
}
esp_err_t resource_store_init(void){
 FILE *file=fopen("@BUNDLE@","rb");assert(file);resource_bytes=malloc(RESOURCE_BUNDLE_SIZE);assert(resource_bytes);
 assert(fread(resource_bytes,1,RESOURCE_BUNDLE_SIZE,file)==RESOURCE_BUNDLE_SIZE);assert(fgetc(file)==EOF);fclose(file);
 assert(weather_art_bind_resources()&&font_ui_bind_resources()&&weather_fonts_bind_resources()&&weather_number_fonts_bind_resources());return ESP_OK;
}
'''.replace('@BUNDLE@',(ROOT/'build-ondemand/resources.bin').as_posix())
mode_mocks='\nesp_err_t app_services_prepare(const app_desc_t *next){return ESP_OK;}\nesp_err_t usb_app_mode_prepare(const app_desc_t *next){return ESP_OK;}\nbool usb_app_mode_pending(void){return false;}\n'
if not baseline:
 MUSIC=MUSIC.replace('lv_init();','lv_init(); assert(resource_store_init()==ESP_OK);',1)
else:
 resource_host=''

mode_mocks += r'''
#include "ble_mouse.h"
#include "keymouse_service.h"
void ble_mouse_pair(void){}
void ble_mouse_forget(void){}
void ble_mouse_snapshot(ble_mouse_snapshot_t *out){memset(out,0,sizeof(*out));}
void keymouse_snapshot(keymouse_snapshot_t *out){memset(out,0,sizeof(*out));}
unsigned board_input_fault_epoch(void){return 0;}
bool usb_service_key(unsigned o,unsigned g,uint8_t m,uint8_t k){return false;}
bool usb_service_mouse(unsigned o,unsigned g,uint8_t b,int x,int y,int w){return false;}
bool usb_service_mouse_click(unsigned o,unsigned g){return false;}
bool usb_service_mute(unsigned o,unsigned g){return false;}
bool usb_service_volume(unsigned o,unsigned g,int n){return false;}
'''
weather_checks=r'''
 extern const app_desc_t app_weather;
 /* Shortcut interactions are covered by touch_ui.py and touch_hid.py. */
 weather.network=true;weather.time_valid=true;weather.data.valid=true;weather.data.daytime=true;weather.data.temperature=23;
 weather.data.apparent=24;weather.data.humidity=60;weather.data.wind=5;weather.phase=WEATHER_OK;
 weather.data.fetched=simulated_now;weather.data.data_time=simulated_now;
 const int resource_codes[]={0,0,2,2,3,45,51,61,63,80,95,71};
 for(unsigned theme=0;theme<3;theme++) for(unsigned i=0;i<12;i++){
  weather_choose_theme(theme);weather.data.code=resource_codes[i];weather.data.daytime=i!=1&&i!=3;weather.revision++;
  app_request(&app_weather);app_process();settle();char name[64];snprintf(name,sizeof(name),"resources-weather-%u-%u",theme,i);snapshot(name);
 }
 app_request(NULL);app_process();
'''
MUSIC=MUSIC.replace('shell_init();app_process();','shell_init();app_process();'+weather_checks,1)
cc='C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe'
with tempfile.TemporaryDirectory(prefix='music-ui-') as tmp:
 tmp=Path(tmp);headers={k:v for k,v in HEADERS.items() if k not in ('lvgl.h','board.h','shell.h')};headers.update(EXTRA_HEADERS)
 headers['esp_log.h']+='#define ESP_LOGW(...) ((void)0)\n'
 headers['clock_compat.h']='#include <time.h>\ntime_t sim_time(time_t *out);\nstruct tm *sim_localtime_r(const time_t*,struct tm*);\n'
 for name,text in headers.items():
  p=tmp/name;p.parent.mkdir(parents=True,exist_ok=True);guard='MUSIC_'+name.upper().replace('/','_').replace('.','_');p.write_text(f'#ifndef {guard}\n#define {guard}\n{text}\n#endif\n',encoding='utf-8')
 host=tmp/'music_sim.c';host.write_text(HOST.split('int main(void)')[0]+MOCK.split('int main(void)')[0]+mode_mocks+resource_host+MUSIC,encoding='utf-8')
 sources=list((ROOT/'components/lvgl/src').rglob('*.c'))+[ROOT/'main/app_registry.c',ROOT/'main/platform/app.c',ROOT/'main/services/wifi_policy.c']+list((ROOT/'main/shell').glob('*.c'))
 sources += [p for p in (ROOT/'main/apps').rglob('*.c') if p.name not in ('weather_service.c','weather_data.c')]
 sources += [ROOT/'templates/minimal/app_minimal.c',ROOT/'main/apps/weather/weather_data.c',Path('C:/play/esp32s3touchscreen/toolchains/idf-5.4.1-cn/components/json/cJSON/cJSON.c')]
 if baseline:
  sources=[Path(baseline)/p.relative_to(ROOT) if (Path(baseline)/p.relative_to(ROOT)).is_file() else p for p in sources if p.is_relative_to(ROOT)]+[p for p in sources if not p.is_relative_to(ROOT)]
 cmd=[cc,'-DLV_CONF_SKIP','-DLV_MEM_SIZE=2097152','-DLV_FONT_MONTSERRAT_20=1','-Dlocaltime_r=sim_localtime_r','-include',str(tmp/'clock_compat.h')]
 cmd+=['-I'+str(p) for p in [tmp,ROOT/'build-ondemand/generated',ROOT/'components/lvgl',ROOT/'main/platform',ROOT/'main/shell',ROOT/'main/services',ROOT/'main/apps/weather',Path('C:/play/esp32s3touchscreen/toolchains/idf-5.4.1-cn/components/json/cJSON')]]
 objects=[]
 for i,source in enumerate([host,*sources]):
  obj=tmp/f'{i}.o';subprocess.run([*cmd,'-c',str(source),'-o',str(obj)],check=True,stdout=subprocess.DEVNULL);objects.append(obj)
 exe=tmp/'music-sim.exe';subprocess.run([cc,*map(str,objects),'-o',str(exe)],check=True);subprocess.run([str(exe)],cwd=out,check=True)
import struct,zlib
def png_chunk(kind,data):
 return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data))
for ppm in out.glob('*.ppm'):
 magic,dimensions,maximum,pixels=ppm.read_bytes().split(b'\n',3)
 assert magic==b'P6' and maximum==b'255'
 width,height=map(int,dimensions.split());assert len(pixels)==width*height*3
 scan=b''.join(b'\0'+pixels[y*width*3:(y+1)*width*3] for y in range(height))
 png=b'\x89PNG\r\n\x1a\n'+png_chunk(b'IHDR',struct.pack('>IIBBBBB',width,height,8,2,0,0,0))+png_chunk(b'IDAT',zlib.compress(scan))+png_chunk(b'IEND',b'')
 ppm.with_suffix('.png').write_bytes(png);ppm.unlink()
