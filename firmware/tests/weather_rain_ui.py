"""Render real mapped weather artwork, verify code mapping and added Chinese glyphs."""
from pathlib import Path
import serial_ui as sim
root=sim.ROOT
sim.pre+='\n#include "@ROOT@/main/apps/serial/serial_status_font.c"\n'
sim.pre=sim.pre.replace('return ctx->timers[0]=lv_timer_create(cb,ms,ctx);','for(unsigned i=0;i<APP_MAX_TIMERS;i++)if(!ctx->timers[i])return ctx->timers[i]=lv_timer_create(cb,ms,ctx);return NULL;')
sim.mocks+='\n#include "@ROOT@/main/apps/weather/weather_art.c"\n#define draw weather_icon_draw\n#include "@ROOT@/main/apps/weather/weather_icons.c"\n#undef draw\n#include "@ROOT@/main/apps/weather/weather_fonts.c"\n'
sim.mocks+='\nconst char *weather_description(int c)'+(root/'main/apps/weather/weather_data.c').read_text(encoding='utf-8').split('const char *weather_description(int c)',1)[1]
sim.resource=r'''
const uint8_t *resource_store_data(size_t offset,size_t len){
 static uint8_t *data;
 if(!data){FILE*f=fopen("@ROOT@/build-wireless/resources.bin","rb");assert(f);data=malloc(RESOURCE_BUNDLE_SIZE);assert(data);assert(fread(data,1,RESOURCE_BUNDLE_SIZE,f)==RESOURCE_BUNDLE_SIZE);fclose(f);}
 assert(offset>=64&&offset+len<=RESOURCE_BUNDLE_SIZE);return data+offset;
}
'''
prefix=sim.main[:sim.main.index(' state_t state={0};')]
sim.main=prefix+r'''
 assert(weather_art_bind_resources()&&weather_fonts_bind_resources());
 board_display_landscape(true);lv_obj_set_size(root,480,320);
 unsigned codes[]={51,61,63,65,80,80,66,95};bool days[]={true,true,true,true,true,false,true,true};
 unsigned expected[]={6,7,12,8,13,15,14,9};const char *names[]={"细雨","小雨","中雨","大雨","阵雨","阵雨","冻雨","雷雨"};
 for(unsigned i=0;i<8;i++)assert(artwork(codes[i],days[i])==expected[i]);
 assert(artwork(56,true)==14&&artwork(57,false)==14&&artwork(67,true)==14&&artwork(97,false)==9);
 assert(artwork(81,true)==13&&artwork(82,false)==15);
 assert(!strcmp(weather_description(80),"阵雨")&&!strcmp(weather_description(56),"冻雨"));
 assert(artwork(0,true)==0&&artwork(0,false)==1&&artwork(71,true)==10&&artwork(-1,true)==11);
 const lv_font_t *fonts[]={&font_weather12,&font_weather13,&font_weather16};
 for(unsigned i=0;i<3;i++){lv_font_glyph_dsc_t glyph;assert(fonts[i]->get_glyph_dsc(fonts[i],&glyph,0x9635,0)&&glyph.box_w>0);}
 for(unsigned theme=0;theme<2;theme++)for(unsigned small=0;small<2;small++){
  lv_obj_clean(root);lv_obj_set_style_bg_color(root,lv_color_hex(theme?0x182126:0xf5f3ec),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
  for(unsigned i=0;i<8;i++){
   int x=120*(i%4),y=158*(i/4),sz=small?56:112;
   weather_icon(root,x+(120-sz)/2,y,sz,codes[i],days[i],lv_color_hex(0),lv_color_hex(0));
   lv_obj_t *label=lv_label_create(root);lv_obj_set_style_text_font(label,&font_weather16,0);lv_obj_set_style_text_color(label,lv_color_hex(theme?0xf6f0e3:0x263e36),0);lv_label_set_text(label,names[i]);lv_obj_set_pos(label,x,y+116);lv_obj_set_width(label,120);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);
  }
  char name[64];snprintf(name,sizeof(name),"rain-%s-%s",theme?"dark":"light",small?"56px":"large");snapshot(name);
 }
 lv_obj_clean(root);assert(lv_mem_test()==LV_RES_OK);
 puts("PASS mapped rain icons: eight distinct day/night renderings at large/56px on light/dark; WMO rain/freezing/showers/thunder mappings; unchanged other categories; Chinese glyphs; heap integrity");return 0;
}
'''
original_run=sim.subprocess.run
def run(cmd,**kwargs):
 if '-c' in cmd:
  for arg in cmd:
   if arg.startswith('-I'):
    header=Path(arg[2:])/'resource_manifest.h'
    if header.exists():header.write_bytes((root/'build-wireless/generated/resource_manifest.h').read_bytes());break
  cmd=[cmd[0],'-DLV_FONT_MONTSERRAT_32=1','-DLV_FONT_MONTSERRAT_40=1','-DLV_FONT_MONTSERRAT_48=1',*cmd[1:]]
 return original_run(cmd,**kwargs)
sim.subprocess.run=run
if __name__=='__main__':sim.run()
