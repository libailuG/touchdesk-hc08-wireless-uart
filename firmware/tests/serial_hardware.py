import sys,time,struct,json,re
from pathlib import Path
import serial
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from serial_simulator import pack,frames
root=Path(__file__).resolve().parents[2]
results=[]
expected=pack(['305419899','4294967295','-1.25','0','1.5','42','0.0001','1000','-2.5','3.25'])
expected_first=bytearray(expected);expected_first[4:8]=(0x12345679).to_bytes(4,'little')
with serial.Serial('COM10',921600,timeout=.1,rtscts=False,dsrdtr=False) as port:
    time.sleep(.2);port.reset_input_buffer()
    for cycle in range(3):
        port.write(b'serial test\n');port.flush()
        pending=bytearray();raw=bytearray();got=[];deadline=time.monotonic()+6
        while time.monotonic()<deadline and len(got)<2:
            data=port.read(port.in_waiting or 1);raw.extend(data);pending.extend(data);got.extend(frames(pending))
        assert got==[bytes(expected_first),expected],('TX mismatch',got,raw)
        # Console-looking bytes must remain binary noise while app is active.
        noise=b'system home\napp ui keysim\n'
        bad=bytearray(expected);bad[-1]=0
        port.write(noise+bad)
        for fragment in [expected[:1],expected[1:7],expected[7:29],expected[29:]]:
            port.write(fragment);port.flush();time.sleep(.015)
        marker=bytearray(expected);marker[12:16]=b'\xaa'*4;marker[20:24]=b'\xee'*4
        port.write(marker);port.flush();time.sleep(.05)
        for n in range(100):
            final=pack([str(n),str(1000+n),'1','2','3','4','5','6','7','8'])
            port.write(final)
            if n%10==9:port.flush();time.sleep(.02)
        port.flush()
        # App never echoes automatically. Wait for its diagnostic-only auto exit.
        after=bytearray();deadline=time.monotonic()+14
        while time.monotonic()<deadline:
            after.extend(port.read(port.in_waiting or 1))
            if b'serial_test:' in after and b'last=' in after and after.endswith(b'\n'):break
        text=after.decode('utf-8','replace')
        (root/'outputs'/f'serial-hardware-cycle-{cycle+1}.log').write_text(text,encoding='utf-8')
        match=re.search(r'serial_test: tx=(\d+) rx=(\d+) discarded=(\d+) bad_tail=(\d+) uart_errors=(\d+) last=([0-9a-f]{96})',text)
        assert match,repr(text)
        tx,rx,discarded,bad_tail,errors=map(int,match.groups()[:5])
        assert (tx,rx,errors)==(2,102,0),(tx,rx,errors,text)
        assert bad_tail>=1 and discarded>=len(noise)+48
        assert bytes.fromhex(match[6])==final
        assert b'\xaa'*4 not in after,'Unexpected automatic TX after single button event'
        port.write(b'services status\n');port.flush();time.sleep(.5)
        status=port.read(port.in_waiting).decode('utf-8','replace')
        assert 'heap_ok=1' in status and 'caps=0 ' in status,status
        results.append(dict(cycle=cycle+1,tx=tx,rx=rx,discarded=discarded,bad_tail=bad_tail,uart_errors=errors,status=status))
        print(results[-1],flush=True)
    # Leave the normal manual-send page open; release COM10 for the user.
    port.write(b'app ui serial\n');port.flush();time.sleep(1)
(root/'outputs/SERIAL_HARDWARE_RESULTS.json').write_text(json.dumps(results,indent=2),encoding='utf-8')
print('PASS 3 hardware cycles, button TX exact bytes, RX fragmentation/noise/bad tail/embedded flags/100-frame burst, console isolation, heap integrity and exit/reentry')
