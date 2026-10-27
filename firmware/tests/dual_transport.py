"""Production USB worker: held keyboard/mouse, busy endpoints and revocation.
No serial port or PC HID input is opened by this test.
"""
from pathlib import Path
import subprocess, tempfile, argparse
from host_regression import HEADERS, ROOT

SOURCE=r'''
#include <assert.h>
#include <stdio.h>
#include "@ROOT@/main/services/usb_service.c"
static void drain(worker_state_t *s) { for(int i=0;i<50;i++) worker_step(s); }
int main(void) {
    assert(usb_service_init()==ESP_OK);
    worker_state_t s={.seen=~0u};
    tud_mount_cb(); unsigned o=usb_service_set_owner(true);
    drain(&s); assert(!usb_service_release_pending() && last_buttons==0 && last_key==0);
    unsigned g=usb_service_session();
    assert(usb_service_key(o,g,2,4)); worker_step(&s);
    assert(last_key==4 && presses==1);
    clock_ms+=2000; drain(&s); assert(last_key==4 && presses==1); // held, no auto-release
    endpoint_ready=false;
    for(int i=0;i<8;i++) assert(usb_service_click(o,g,0,5));
    assert(usb_service_key(o,g,0,0)); // release bypasses full queue
    endpoint_ready=true; worker_step(&s); assert(last_key==0);
    usb_service_cancel(); drain(&s); assert(queue->count==0);
    g=usb_service_session();
    assert(!usb_service_key(o,g-1,1,4));
    assert(!usb_service_mouse(o+1,g,1,1,2,3));
    assert(usb_service_mouse(o,g,1,300,-301,9));
    drain(&s); assert(last_buttons==1 && mouse_total_x==300 && mouse_total_y==-301 && mouse_total_wheel==9);
    clock_ms+=10000; drain(&s); assert(last_buttons==1); // drag latch survives finger lift
    accept_report=false;
    assert(usb_service_mouse(o,g,0,0,0,0)); drain(&s); assert(last_buttons==1);
    accept_report=true; worker_step(&s); assert(last_buttons==0); // retry release
    assert(usb_service_mouse(o,g,3,0,0,0)); worker_step(&s); assert(last_buttons==3);
    assert(usb_service_key(o,g,1,10)); worker_step(&s);
    endpoint_ready=false; usb_service_cancel(); drain(&s);
    assert(usb_service_release_pending());
    assert(!usb_service_mouse(o,g,1,500,500,0));
    endpoint_ready=true; drain(&s); assert(last_buttons==0 && last_key==0 && !usb_service_release_pending());
    g=usb_service_session();
    assert(usb_service_mouse(o,g,1,2048,2048,0));
    assert(usb_service_key(o,g,1,12));
    int x=mouse_total_x,y=mouse_total_y;
    tud_umount_cb(); drain(&s); tud_mount_cb(); drain(&s);
    assert(last_buttons==0 && last_key==0 && mouse_total_x==x && mouse_total_y==y);
    g=usb_service_session(); assert(!usb_service_mouse(o,g-1,1,0,0,0));
    assert(usb_service_mouse(o,g,2,0,0,0)); worker_step(&s);
    usb_service_set_owner(false); drain(&s); assert(last_buttons==0);
    uint8_t b[16]; assert(tud_hid_get_report_cb(0,1,1,b,16)==8);
    assert(tud_hid_get_report_cb(0,2,1,b,16)==5);
    assert(tud_hid_get_report_cb(0,3,1,b,16)==2);
    o=usb_service_set_owner(true);drain(&s);g=usb_service_session();
    endpoint_ready=false;
    assert(usb_service_key(o,g,0,4));assert(usb_service_key(o,g,0,0));
    endpoint_ready=true;worker_step(&s);worker_step(&s);assert(last_key==4);
    clock_ms+=40;worker_step(&s);assert(last_key==0);
    endpoint_ready=false;assert(usb_service_mouse_click(o,g));
    clock_ms+=1000;drain(&s);assert(last_buttons==0);
    endpoint_ready=true;worker_step(&s);assert(last_buttons==1);
    clock_ms+=39;worker_step(&s);assert(last_buttons==1);
    clock_ms++;accept_report=false;worker_step(&s);assert(last_buttons==1);
    accept_report=true;worker_step(&s);assert(last_buttons==0);
    assert(usb_service_volume(o,g,1));worker_step(&s);assert(last_consumer==0xE9);
    clock_ms+=40;accept_report=false;worker_step(&s);assert(last_consumer==0xE9);
    accept_report=true;worker_step(&s);assert(last_consumer==0);
    assert(usb_service_volume(o,g,-1));worker_step(&s);assert(last_consumer==0xEA);
    clock_ms+=40;worker_step(&s);assert(last_consumer==0);
    assert(usb_service_mute(o,g));worker_step(&s);assert(last_consumer==0xE2);
    usb_service_cancel();drain(&s);assert(last_consumer==0 && volume_steps==0);
    assert(!usb_service_mute(o,g) && !usb_service_volume(o,g,1));
    g=usb_service_session();assert(usb_service_volume(o,g,10));tud_umount_cb();tud_mount_cb();drain(&s);assert(last_consumer==0 && volume_steps==0);
    puts("PASS media volume: up/down/mute, release retry, cancel and reconnect no replay");
    puts("PASS touch HID: held key, release bypasses full queue, mouse drag/deltas/wheel, failed release retry, cancel, stale owner/session, disconnect no replay, get-report lengths");

    assert(usb_service_transport(true)==ESP_OK && use_ble && !driver_installed);
    o=usb_service_set_owner(true);mock_ble_ready=true;mock_ble_generation++;drain(&s);
    g=usb_service_session();assert(usb_service_key(o,g,2,4));worker_step(&s);assert(mock_ble_key==4&&mock_ble_mods==2);
    assert(usb_service_mouse(o,g,1,2,3,1));worker_step(&s);assert(mock_ble_buttons==1);
    usb_service_cancel();drain(&s);assert(mock_ble_key==0&&mock_ble_buttons==0);
    assert(!usb_service_key(o,g,0,5));g=usb_service_session();
    assert(usb_service_mute(o,g));worker_step(&s);assert(mock_ble_consumer==0xE2);
    clock_ms+=40;mock_ble_accept=false;worker_step(&s);assert(mock_ble_consumer==0xE2);
    mock_ble_accept=true;worker_step(&s);assert(mock_ble_consumer==0);
    assert(usb_service_key(o,g,1,8));mock_ble_ready=false;worker_step(&s);
    mock_ble_ready=true;mock_ble_generation++;drain(&s);assert(mock_ble_key==0&&queue->count==0);
    g=usb_service_session();usb_service_pair(false);assert(mock_pairs==1&&!usb_service_key(o,g,0,5));
    usb_service_pair(true);assert(mock_forgets==1);drain(&s);
    assert(usb_service_transport(false)==ESP_OK && !use_ble && driver_installed);
    o=usb_service_set_owner(true);tud_mount_cb();drain(&s);assert(last_key==0&&last_buttons==0);
    puts("PASS dual transport: owner/session, BLE media retry, disconnect/reconnect no replay, pair/forget, USB return");
    return 0;
}
'''
def main():
    ap=argparse.ArgumentParser();ap.add_argument('--cc',default='C:/Users/libai/Documents/Codex/2026-10-19/esp32s3-app-platform/work/host-tools/tcc/tcc.exe');a=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='touch-hid-') as d:
        d=Path(d)
        for n,t in HEADERS.items():
            p=d/n;p.parent.mkdir(parents=True,exist_ok=True)
            guard='HID_TEST_'+n.upper().replace('/','_').replace('.','_')
            p.write_text(f'#ifndef {guard}\n#define {guard}\n{t}\n#endif\n',encoding='utf-8')
        p=d/'test.c';p.write_text(SOURCE.replace('@ROOT@',ROOT.as_posix()),encoding='utf-8')
        exe=d/'test.exe'
        subprocess.run([a.cc,'-I'+str(d),'-I'+str(ROOT/'main/services'),str(p),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__=='__main__': main()
