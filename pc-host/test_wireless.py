import os,sys
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen'
sys.path.insert(0,str(Path(__file__).resolve().parent))
from PyQt6.QtWidgets import QApplication
from serial_host import Window,encode,Parser
app=QApplication([]);w=Window()
assert not w.port.isOpen() and not w.wave_timer.isActive()
assert w.profile.currentData()==115200 and w.curves.wave_period.value()==100 and w.curves.wave_period.minimum()==1
w.profile.setCurrentIndex(1)
assert w.profile.currentData()==921600 and w.curves.wave_period.minimum()==1
w.profile.setCurrentIndex(0)
class Port:
 def __init__(self):self.frames=[]
 def isOpen(self):return True
 def bytesToWrite(self):return 0
 def write(self,raw):self.frames.append(raw);return len(raw)
p=Port();w.port=p
raw=encode(['1','2']+['3.25']*8)
for _ in range(4):assert w.write_frame(raw,'test')
assert not w.write_frame(raw,'overload') and len(p.frames)==4 and w.tx_pending==192
w.written(192);assert w.tx==4 and w.tx_pending==0
assert w.write_frame(raw,'recovered')
parser=Parser();assert parser.feed(raw[:20])==[];assert parser.feed(raw[20:40])==[];assert parser.feed(raw[40:]+raw)==[raw,raw]
w.wave_timer.stop();w.wave_stats.stop();w.curves.demo_timer.stop()
print('PASS wireless default 115200/100ms, wired 921600/1ms, no auto-connect/send, bounded 4-frame UART queue, drain recovery, fragmented/concatenated protocol')
