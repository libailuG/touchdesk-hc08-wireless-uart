"""Render the production send/receive layout and exercise large-field touch mapping."""
import serial_ui as sim
sim.pre+='\n#include \"@ROOT@/main/apps/serial/serial_status_font.c\"\n'
sim.pre=sim.pre.replace('return ctx->timers[0]=lv_timer_create(cb,ms,ctx);','for(unsigned i=0;i<APP_MAX_TIMERS;i++)if(!ctx->timers[i])return ctx->timers[i]=lv_timer_create(cb,ms,ctx);return NULL;')
prefix=sim.main[:sim.main.index(' state_t state={0};')]
sim.main=prefix+r'''
 state_t state={0};app_ctx_t ctx={.active=true,.state=&state,.root=root,.desc=&app_serial};
 assert(create(&ctx,root)==ESP_OK);advance(120);
 assert(simulated.tx==0&&!lv_obj_has_state(state.continuous_switch,LV_STATE_CHECKED));
 for(unsigned i=0;i<10;i++){
  lv_area_t a;lv_obj_get_coords(state.fields[i],&a);assert(a.x1>=0&&a.x2<480&&a.y1>=132&&a.y2<274);
  for(unsigned j=0;j<i;j++){lv_area_t b;lv_obj_get_coords(state.fields[j],&b);assert(a.x2<b.x1||b.x2<a.x1||a.y2<b.y1||b.y2<a.y1);}
  assert(lv_obj_get_style_text_font(state.values[i],0)==((i==2||i==3)?&lv_font_montserrat_48:&lv_font_montserrat_26));
 }
 assert(lv_obj_get_width(state.fields[2])==lv_obj_get_width(state.states[0]));
 assert(lv_obj_get_height(state.fields[2])==lv_obj_get_height(state.states[0]));
 snapshot("large-data34-send-default");
 /* A click without incoming data must not color the unknown RX dot via transparency. */
 tap(65,100);assert((state.tx.data1&1u)&&!simulated.has_frame);
 for(unsigned bit=0;bit<4;bit++){
  assert(lv_obj_get_style_bg_opa(state.state_dots[bit],0)==LV_OPA_COVER);
  assert(lv_obj_get_style_bg_color(state.state_dots[bit],0).full==ui_surface().full);
 }
 snapshot("dot-no-feedback-after-toggle");
 lv_area_t dot;lv_obj_get_coords(state.state_dots[0],&dot);int dx=(dot.x1+dot.x2)/2,dy=(dot.y1+dot.y2)/2;
 assert(frame[(479-dx)*320+dy].full==ui_surface().full);

 tap(66,183);assert(state.editor&&state.editing==2);lv_textarea_set_text(state.input,"-1.25");
 lv_event_send(state.confirm,LV_EVENT_CLICKED,NULL);advance(30);
 assert(!state.editor&&state.tx.data3==-1.25f&&state.tx.data4==0);
 state.tx.data4=3.1415f;refresh(&state);snapshot("large-data34-send-values");
 const char *samples[]={"4294967295","-3.4028e+38","-0.0001","123.4567","nan","inf"};
 for(unsigned k=0;k<2;k++)for(unsigned i=0;i<6;i++){lv_point_t sz;lv_txt_get_size(&sz,samples[i],value_font(samples[i],k),0,0,LV_COORD_MAX,LV_TEXT_FLAG_NONE);assert(sz.x<=100);}
 serial_frame_init(&simulated.last);simulated.last.data1=5;simulated.last.data2=123;simulated.last.data3=2.5f;simulated.last.data4=-.125f;simulated.has_frame=true;simulated.rx=1;
 lv_obj_add_state(state.continuous_switch,LV_STATE_CHECKED);lv_event_send(state.continuous_switch,LV_EVENT_VALUE_CHANGED,NULL);
 unsigned sent_before=simulated.tx;advance(1000);assert(simulated.tx-sent_before==50);
 lv_event_send(state.tabs[1],LV_EVENT_CLICKED,NULL);sent_before=simulated.tx;advance(1000);assert(simulated.tx-sent_before==50);
 assert(state.receive&&!strcmp(lv_label_get_text(state.values[2]),"2.5")&&!strcmp(lv_label_get_text(state.values[3]),"-0.125"));
 for(unsigned i=0;i<10;i++)assert(!lv_obj_has_flag(state.fields[i],LV_OBJ_FLAG_CLICKABLE));
 tap(66,183);assert(!state.editor);snapshot("large-data34-receive-values");
 lv_event_send(state.tabs[2],LV_EVENT_CLICKED,NULL);sent_before=simulated.tx;advance(1000);assert(simulated.tx-sent_before==50);
 lv_event_send(state.tabs[0],LV_EVENT_CLICKED,NULL);lv_obj_clear_state(state.continuous_switch,LV_STATE_CHECKED);lv_event_send(state.continuous_switch,LV_EVENT_VALUE_CHANGED,NULL);
 unsigned stopped=simulated.tx;advance(1000);assert(simulated.tx==stopped);
 for(unsigned requested=0;requested<16;requested++)for(unsigned received=0;received<16;received++){
  state.tx.data1=requested;simulated.last.data1=received;refresh(&state);
  for(unsigned bit=0;bit<4;bit++)assert(lv_obj_get_style_bg_color(state.state_dots[bit],0).full==lv_color_hex((received&(1u<<bit))?0x43d47a:0x6b7280).full);
 }
 ctx.active=false;for(unsigned i=0;i<APP_MAX_TIMERS;i++)if(ctx.timers[i]){lv_timer_del(ctx.timers[i]);ctx.timers[i]=NULL;}
 destroy(&ctx);lv_obj_clean(root);memset(&state,0,sizeof(state));ctx.active=true;assert(create(&ctx,root)==ESP_OK);advance(1000);
 assert(!lv_obj_has_state(state.continuous_switch,LV_STATE_CHECKED)&&simulated.tx==stopped);
 assert(lv_mem_test()==LV_RES_OK);
 puts("PASS production LVGL: non-overlapping shared grid, adaptive large fonts, data3 touch/edit identity, RX read-only, 20ms TX across tabs, exit/reopen default off, independent RX dot fill and opaque unknown-dot pixels after send");return 0;
}
'''
original_run=sim.subprocess.run
def run(command,**kwargs):
 if '-c' in command:command=[command[0],'-DLV_FONT_MONTSERRAT_32=1','-DLV_FONT_MONTSERRAT_40=1','-DLV_FONT_MONTSERRAT_48=1',*command[1:]]
 return original_run(command,**kwargs)
sim.subprocess.run=run
if __name__=='__main__':sim.run()
