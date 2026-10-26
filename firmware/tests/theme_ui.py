"""Production LVGL theme, popup, contrast and page-reset checks. No hardware."""
import re
import serial_ui as sim
from host_regression import ROOT

style=(ROOT/'main/apps/weather/weather_style.c').read_text(encoding='utf8')
palettes=style[style.index('static const weather_palette_t palettes'):style.index('static unsigned checked')]
sim.pre=re.sub(r'static const weather_palette_t palette=\{[^;]+;\nconst weather_palette_t \*weather_palette\(unsigned t\) \{ return &palette; \}',palettes+'const weather_palette_t *weather_palette(unsigned t) { return &palettes[t<3?t:0]; }',sim.pre)
assert 'return &palettes' in sim.pre
prefix=sim.main[:sim.main.index(' state_t state={0};')]
sim.main=prefix+r'''
 state_t state={0};app_ctx_t ctx={.active=true,.state=&state,.root=root,.desc=&app_serial};
 lv_mem_monitor_t before,after;
 lv_obj_t *persistent=ui_button(lv_scr_act(),"home",0,0,100,40,NULL,NULL);lv_obj_add_flag(persistent,LV_OBJ_FLAG_HIDDEN);
 for(unsigned round=0;round<31;round++) {
  for(unsigned theme=0;theme<3;theme++) {
   /* Deliberately leave local colors from the previous theme on the reused page. */
   lv_obj_set_style_bg_color(root,lv_color_hex(weather_palette((theme+2)%3)->bg),0);
   lv_obj_set_style_text_color(root,lv_color_hex(weather_palette((theme+2)%3)->ink),0);
   /* shell_desktop sets geometry BEFORE applying the palette. */
   lv_obj_set_pos(root,0,36);lv_obj_set_size(root,320,444);
   ui_set_theme(theme);ui_reset_page(root);
   assert(lv_obj_get_style_x(root,0)==0&&lv_obj_get_style_y(root,0)==36);
   assert(lv_obj_get_style_width(root,0)==320&&lv_obj_get_style_height(root,0)==444);
   const weather_palette_t *p=weather_palette(theme);
   ui_style_button(persistent);lv_obj_add_state(persistent,LV_STATE_PRESSED);advance(180);
   assert(lv_obj_get_style_bg_color(persistent,0).full==lv_color_hex(p->line).full);
   assert(lv_obj_get_style_text_color(persistent,0).full==lv_color_hex(p->ink).full);
   assert(lv_obj_get_style_border_color(persistent,0).full==lv_color_hex(p->accent).full);
   lv_obj_clear_state(persistent,LV_STATE_PRESSED);advance(180);
   assert(lv_obj_get_style_bg_color(root,0).full==lv_color_hex(p->bg).full);
   assert(lv_obj_get_style_text_color(root,0).full==lv_color_hex(p->ink).full);
   lv_obj_set_pos(root,0,0); /* shell_fullscreen(true), before landscape create */
   assert(create(&ctx,root)==ESP_OK);advance(120);
   for(unsigned field=0;field<10;field++){
    assert(lv_obj_get_child_cnt(state.fields[field])==1);
    int delta=(int)lv_color_brightness(card_ink())-(int)lv_color_brightness(card_color(field));
    assert(abs(delta)>90);
   }
   if(round==0){char name[64];snprintf(name,sizeof(name),"theme-%u-cards",theme);snapshot(name);}

   lv_obj_t *list=lv_dropdown_get_list(state.selector);
   assert(lv_obj_get_style_bg_color(state.selector,0).full==lv_color_hex(p->surface).full);
   assert(lv_obj_get_style_text_color(state.selector,0).full==lv_color_hex(p->ink).full);
   assert(lv_obj_get_style_bg_color(list,0).full==lv_color_hex(p->surface).full);
   assert(lv_obj_get_style_text_color(list,0).full==lv_color_hex(p->ink).full);
   lv_obj_add_state(list,LV_STATE_CHECKED);advance(180);
   assert(lv_obj_get_style_bg_color(list,LV_PART_SELECTED).full==lv_color_hex(p->accent).full);
   assert(lv_obj_get_style_text_color(list,LV_PART_SELECTED).full==ui_accent_foreground().full);
   lv_obj_clear_state(list,LV_STATE_CHECKED);
   assert(lv_obj_get_style_bg_color(state.chart,0).full==lv_color_hex(p->surface).full);
   assert(lv_obj_get_style_line_color(state.chart,0).full==lv_color_hex(p->line).full);
   assert(state.series->color.full==lv_color_hex(p->accent).full);
   assert(lv_obj_get_style_text_color(state.send,0).full==ui_accent_foreground().full);
   lv_obj_add_state(state.send,LV_STATE_PRESSED);advance(180);
   assert(lv_obj_get_style_text_color(state.send,0).full==lv_color_hex(p->ink).full);
   lv_obj_clear_state(state.send,LV_STATE_PRESSED);advance(180);
   lv_event_send(state.fields[2],LV_EVENT_CLICKED,NULL);assert(state.editor);
   assert(lv_obj_get_style_bg_color(state.input,0).full==lv_color_hex(p->surface).full);
   assert(lv_obj_get_style_bg_color(state.matrix,LV_PART_ITEMS).full==lv_color_hex(p->surface).full);
   if(round==0){char name[64];snprintf(name,sizeof(name),"theme-%u-editor",theme);snapshot(name);}
   close_editor(&state);
   lv_obj_t *slider=lv_slider_create(root);
   assert(lv_obj_get_style_bg_color(slider,LV_PART_MAIN).full==lv_color_hex(p->line).full);
   assert(lv_obj_get_style_bg_color(slider,LV_PART_INDICATOR).full==lv_color_hex(p->accent).full);lv_obj_del(slider);
   lv_event_send(state.tabs[2],LV_EVENT_CLICKED,NULL);
   serial_frame_t f;serial_frame_init(&f);
   for(unsigned i=0;i<1000;i++){f.data3=sin(i*.015707963);inject(&f);advance(5);}
   advance(120);
   if(round==0){char name[64];snprintf(name,sizeof(name),"theme-%u-curve",theme);snapshot(name);lv_dropdown_open(state.selector);advance(120);snprintf(name,sizeof(name),"theme-%u-popup",theme);snapshot(name);lv_dropdown_close(state.selector);}
   if(ctx.timers[0]){lv_timer_del(ctx.timers[0]);ctx.timers[0]=NULL;}
   destroy(&ctx);lv_obj_clean(root);memset(&state,0,sizeof(state));memset(&sim_history,0,sizeof(sim_history));memset(&simulated,0,sizeof(simulated));
   assert(allocations==0);assert(lv_mem_test()==LV_RES_OK);
  }
  if(round==0)lv_mem_monitor(&before);
 }
 lv_mem_monitor(&after);assert(after.free_size>=before.free_size);
 printf("PASS 3 production palettes x31 cycles, page reset, dropdown/popup/selected contrast, chart/grid/series, send/pressed contrast, editor/keypad, slider; 1000 uniform points; heap %u -> %u\n",before.free_size,after.free_size);
 return 0;
}
'''
if __name__=='__main__':sim.run()
