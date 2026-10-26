"""Drive the real four-row keypad by touch; verify geometry, validation and send pause."""
import serial_large_data_ui
sim=serial_large_data_ui.sim
prefix=sim.main[:sim.main.index(' state_t state={0};')]
sim.main=prefix+r'''
 state_t state={0};app_ctx_t ctx={.active=true,.state=&state,.root=root,.desc=&app_serial};
 assert(create(&ctx,root)==ESP_OK);advance(120);
 tap(298,159);assert(state.editor&&state.editing==0);advance(50);
 lv_btnmatrix_t *m=(lv_btnmatrix_t*)state.matrix;assert(m->btn_cnt==16&&m->row_cnt==4);
 for(unsigned i=0;i<16;i++){assert(lv_area_get_width(&m->button_areas[i])==108);assert(lv_area_get_height(&m->button_areas[i])==54);}
 assert(lv_btnmatrix_has_btn_ctrl(state.matrix,11,LV_BTNMATRIX_CTRL_DISABLED));
 assert(lv_btnmatrix_has_btn_ctrl(state.matrix,12,LV_BTNMATRIX_CTRL_DISABLED));
 assert(lv_btnmatrix_has_btn_ctrl(state.matrix,14,LV_BTNMATRIX_CTRL_DISABLED));
 bounds(state.editor);snapshot("keyboard-tall-integer");
 tap(414,167);tap(66,107);tap(182,107);tap(298,107);tap(182,287);assert(!strcmp(lv_textarea_get_text(state.input),"7890"));
 tap(414,107);assert(!strcmp(lv_textarea_get_text(state.input),"789"));tap(416,49);assert(!state.editor&&state.tx.data1==789&&simulated.tx==0);
 tap(298,159);lv_textarea_set_text(state.input,"4294967296");tap(416,49);assert(state.editor&&state.tx.data1==789);bounds(state.editor);snapshot("keyboard-tall-integer-error");
 lv_textarea_set_text(state.input,"4294967295");tap(416,49);assert(!state.editor&&state.tx.data1==UINT32_MAX);
 tap(298,159);tap(414,167);tap(66,227);tap(414,287);assert(!state.editor&&state.tx.data1==UINT32_MAX);
 tap(66,183);assert(state.editor&&state.editing==2);assert(!lv_btnmatrix_has_btn_ctrl(state.matrix,11,LV_BTNMATRIX_CTRL_DISABLED));
 tap(414,167);tap(66,227);tap(66,287);tap(298,287);tap(182,227);tap(182,167);assert(!strcmp(lv_textarea_get_text(state.input),"-1.25"));
 bounds(state.editor);snapshot("keyboard-tall-float");tap(416,49);assert(!state.editor&&state.tx.data3==-1.25f);
 tap(66,183);lv_textarea_set_text(state.input,"1e");tap(416,49);assert(state.editor&&state.tx.data3==-1.25f);bounds(state.editor);snapshot("keyboard-tall-float-error");
 tap(414,167);tap(66,227);tap(414,227);tap(298,227);tap(416,49);assert(!state.editor&&state.tx.data3==1000);
 lv_obj_add_state(state.continuous_switch,LV_STATE_CHECKED);lv_event_send(state.continuous_switch,LV_EVENT_VALUE_CHANGED,NULL);advance(100);assert(simulated.tx==5);
 tap(66,183);unsigned paused=simulated.tx;advance(1000);assert(simulated.tx==paused);
 tap(414,287);assert(!state.editor);unsigned resumed=simulated.tx;advance(1000);assert(simulated.tx-resumed==50);
 lv_obj_clear_state(state.continuous_switch,LV_STATE_CHECKED);lv_event_send(state.continuous_switch,LV_EVENT_VALUE_CHANGED,NULL);
 for(unsigned n=0;n<100;n++){lv_event_send(state.fields[n%10],LV_EVENT_CLICKED,NULL);assert(state.editor);lv_event_send(state.confirm,LV_EVENT_CLICKED,NULL);assert(!state.editor);}
 assert(lv_mem_test()==LV_RES_OK);
 puts("PASS production LVGL four-row keypad: 16 keys at 108x54; touch digits/backspace/clear/sign/decimal/e/confirm/cancel; integer bounds; float validation; send pauses during editor and resumes; repeated open/close");return 0;
}
'''
if __name__=='__main__':sim.run()
