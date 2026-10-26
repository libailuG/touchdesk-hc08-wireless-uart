import os,sys,math,time
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen';root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
from serial_host import *
from PyQt6.QtTest import QTest
app=QApplication([]);w=Window();w.show();w.pages.setCurrentIndex(1);app.processEvents()
class Fake:
 def __init__(self):self.data=[]
 def isOpen(self):return True
 def write(self,b):self.data.append(b);QTimer.singleShot(0,lambda:w.written(len(b)));return len(b)
 def close(self):pass
old=w.port;w.port=Fake();w.inputs[0].setText('5');w.inputs[3].setText('7')
assert w.curves.wave_period.minimum()==1 and w.curves.wave_period.value()==100
w.curves.wave_period.setValue(1)
w.curves.send_demo.setChecked(True);QTest.qWait(600);time.sleep(.02);QTest.qWait(600);elapsed=w.wave_clock.nsecsElapsed()/1e9
w.curves.send_demo.setChecked(False);QTest.qWait(30);count=len(w.port.data);rate=count/elapsed
assert not w.timer_resolution and not w.wave_timer.isActive() and 900<rate<1100,(count,elapsed,rate)
for i,raw in enumerate(w.port.data):
 values=WIRE.unpack(raw);assert len(raw)==48 and values[1]==5 and values[4]==7
 assert abs(values[3]-math.sin(2*math.pi*.5*i*.001))<1e-6
assert w.tx==count and w.tx_pending==0
QTest.qWait(80);assert len(w.port.data)==count
assert len(w.curves.tx_history)<=2000
w.port=old;w.close();print(f'PASS 1ms scheduled sine/limited catch-up, {count} frames in {elapsed:.3f}s ({rate:.1f}/s), all48-byte frames/sample phases correct, fields preserved, no TX after stop, timer-resolution request released')
