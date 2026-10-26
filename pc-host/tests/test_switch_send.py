import os,sys
from pathlib import Path
os.environ['QT_QPA_PLATFORM']='offscreen'
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
from serial_host import *
from PyQt6.QtTest import QTest
app=QApplication([]);w=Window();w.show();app.processEvents()
class Fake:
 def __init__(self):self.data=[]
 def isOpen(self):return True
 def bytesToWrite(self):return len(self.data)*48
 def write(self,b):self.data.append(b);return len(b)
 def close(self):pass
old=w.port;w.port=Fake();w.send_button.setEnabled(True)
for pattern in range(16):
 w.inputs[0].setText(str(0xabcdef00|pattern))
 for bit,button in enumerate(w.state_buttons):
  before=int(w.inputs[0].text());count=len(w.port.data);QTest.mouseClick(button,Qt.MouseButton.LeftButton)
  assert len(w.port.data)==count+1
  assert WIRE.unpack(w.port.data[-1])[1]==before^(1<<bit)
  assert int(w.inputs[0].text())&0xfffffff0==0xabcdef00
assert w.tx_pending==64*48
w.written(17);assert w.tx==0
w.written(48*63+7);assert w.tx==63
w.written(24);assert w.tx==64 and w.tx_pending==w.tx_partial==0
count=len(w.port.data);QTest.mousePress(w.state_buttons[0],Qt.MouseButton.LeftButton);QTest.qWait(900);assert len(w.port.data)==count
QTest.mouseRelease(w.state_buttons[0],Qt.MouseButton.LeftButton);assert len(w.port.data)==count+1;w.written(48)
count=len(w.port.data);w.inputs[0].setText('123');assert len(w.port.data)==count
w.consume(encode(['15']+['0']*9));assert len(w.port.data)==count
assert all(l.text().endswith('开') for l in w.state_lamps)
w.inputs[2].setText('bad');QTest.mouseClick(w.state_buttons[0],Qt.MouseButton.LeftButton);assert len(w.port.data)==count
w.port=old;w.close();print('PASS Qt all 16 patterns, each switch click exactly one 48-byte frame, queued fast clicks/partial bytesWritten counted correctly, hold no repeats, numeric/RX refresh no auto TX, invalid data blocked')
