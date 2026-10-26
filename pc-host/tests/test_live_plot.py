import os,sys,math
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen'
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
from serial_host import *
from PyQt6.QtGui import QFontDatabase
app=QApplication([]);QFontDatabase.addApplicationFont('C:/Windows/Fonts/msyh.ttc');app.setFont(QFont('Microsoft YaHei UI',10));w=Window();w.show();w.pages.setCurrentIndex(1);app.processEvents()
raw=b''.join(WIRE.pack(0xaaaaaaaa,1,2,*[math.sin(n*.03*(i+1))*(i+1) for i in range(8)],0xeeeeeeee) for n in range(3500))
for start in range(0,len(raw),251):w.consume(raw[start:start+251])
assert w.rx==3500 and len(w.curves.history)==2000 and w.curves.history[0][0]==1501
w.curves.pause.setChecked(True);frozen=w.curves.display_data();w.consume(encode(['0']*10));assert w.curves.display_data()==frozen and w.rx==3501
w.curves.pause.setChecked(False);assert w.curves.display_data()[-1][0]==3501
w.curves.pause.setChecked(True);w.curves.clear();assert not w.curves.display_data() and w.rx==3501
w.curves.pause.setChecked(False);w.consume(WIRE.pack(0xaaaaaaaa,0,0,*[float('nan'),float('inf'),1,1,1,1,1,1],0xeeeeeeee));w.curves.channels[1].setChecked(True);w.curves.refresh(force=True);app.processEvents()
count=w.tx;w.curves.demo.setChecked(True)
for _ in range(100):w.curves.demo_tick()
assert w.tx==count and len(w.curves.history)==1
w.curves.channels[1].setChecked(True);w.curves.refresh(force=True);app.processEvents();assert not w.grab().isNull()
w.curves.demo.setChecked(False);w.close();print('PASS Qt 3500 fragmented frames, bounded2000, pause/resume/clear preserving RX, NaN/Inf gaps, isolated local simulation, real QPainter rendering; default no periodic UART TX')
