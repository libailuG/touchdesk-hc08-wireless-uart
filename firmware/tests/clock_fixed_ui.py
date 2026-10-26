"""Check all daily clock values with production fonts and mapped bitmap resources."""
import weather_rain_ui as rain
sim = rain.sim
sim.pre = sim.pre.replace("const weather_palette_t *weather_palette(unsigned t) { return &palette; }", "")
sim.mocks += '\n#include "@ROOT@/main/apps/weather/weather_number_fonts.c"\n#include "@ROOT@/main/apps/weather/weather_style.c"\n'
sim.main = sim.main[:sim.main.index(' assert(weather_art_bind_resources')] + r'''
 assert(weather_number_fonts_bind_resources());
 board_display_landscape(false);lv_obj_set_size(root,320,480);
 for(unsigned theme=0;theme<WEATHER_THEMES;theme++) {
  const lv_font_t *large=weather_clock_font(theme),*small=weather_clock_seconds_font(theme);
  assert(large->line_height==small->line_height);
  for(unsigned ch='0';ch<='9';ch++) {
   lv_font_glyph_dsc_t a,b;
   assert(lv_font_get_glyph_dsc(large,&a,ch,0)&&lv_font_get_glyph_dsc(small,&b,ch,0));
   assert(a.box_w==b.box_w&&a.box_h==b.box_h&&a.ofs_y==b.ofs_y);
   assert(lv_font_get_glyph_bitmap(large,ch)==lv_font_get_glyph_bitmap(small,ch));
  }
  int widths[]={lv_txt_get_width("00:00",5,large,0,LV_TEXT_FLAG_NONE),lv_txt_get_width("00:00:00",8,small,0,LV_TEXT_FLAG_NONE)};
  assert(widths[0]<=288&&widths[1]<=320);
  for(unsigned seconds=0;seconds<2;seconds++) {
   const lv_font_t *font=seconds?small:large;lv_font_glyph_dsc_t first,glyph;
   assert(lv_font_get_glyph_dsc(font,&first,'0',0));
   for(unsigned ch='0';ch<='9';ch++) {
    assert(lv_font_get_glyph_dsc(font,&glyph,ch,':')&&glyph.adv_w==first.adv_w);
    assert(glyph.box_w<=glyph.adv_w&&glyph.ofs_x>=0&&glyph.ofs_x+glyph.box_w<=glyph.adv_w);
    assert(lv_font_get_glyph_bitmap(font,ch));
   }
   assert(lv_font_get_glyph_dsc(font,&glyph,':','0')&&!glyph.is_placeholder&&glyph.box_w>0);
   assert(lv_font_get_glyph_bitmap(font,':'));
   assert(lv_txt_get_width(seconds?"--:--:--":"--:--",seconds?8:5,font,0,LV_TEXT_FLAG_NONE)==widths[seconds]);
   for(int stamp=0;stamp<86400;stamp++) {
    char value[16];snprintf(value,sizeof(value),"%02d:%02d:%02d",stamp/3600,stamp/60%60,stamp%60);
    assert(lv_txt_get_width(value,seconds?8:5,font,0,LV_TEXT_FLAG_NONE)==widths[seconds]);
   }
  }
  printf("theme %u: fixed HH:MM width %d; HH:MM:SS width %d\n",theme,widths[0],widths[1]);
 }
 for(unsigned sample=0;sample<4;sample++) {
  lv_obj_clean(root);lv_obj_set_style_bg_color(root,lv_color_hex(0xf5f3ec),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);
  for(unsigned theme=0;theme<WEATHER_THEMES;theme++) for(unsigned seconds=0;seconds<2;seconds++) {
   lv_obj_t *label=lv_label_create(root);lv_obj_set_pos(label,0,theme*156+seconds*78);lv_obj_set_width(label,320);
   lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_obj_set_style_text_color(label,lv_color_hex(0x263e36),0);
   lv_obj_set_style_text_font(label,seconds?weather_clock_seconds_font(theme):weather_clock_font(theme),0);
   lv_label_set_text(label,sample==0?(seconds?"11:11:11":"11:11"):sample==1?(seconds?"20:08:00":"20:08"):sample==3?(seconds?"00:00:00":"00:00"):(seconds?"--:--:--":"--:--"));
  }
  char shot[64];snprintf(shot,sizeof(shot),"clock-fixed-%u",sample);snapshot(shot);
 }
 assert(lv_mem_test()==LV_RES_OK);
 puts("PASS three themes, every daily second, constant character positions, placeholders, complete glyphs/bitmaps and <=320px width; real LVGL render and heap integrity");return 0;
}
'''
if __name__ == '__main__': sim.run()
