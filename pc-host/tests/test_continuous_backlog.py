import os
import sys
from pathlib import Path
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from serial_host import QApplication, Window, WIRE

app = QApplication([])
w = Window()
original_port = w.port
class BackloggedPort:
    def __init__(self): self.data = []
    def isOpen(self): return True
    def bytesToWrite(self): return 1000000 + len(self.data) * 48
    def write(self, raw): self.data.append(raw); return len(raw)
    def close(self): pass
class DelayedClock:
    def isValid(self): return True
    def nsecsElapsed(self): return 250000000
w.port = BackloggedPort()
w.curves.wave_period.setValue(1)
w.curves.send_demo.setChecked(True)
w.wave_timer.stop()
w.wave_stats.stop()
w.wave_clock = DelayedClock()
initial_frames = len(w.port.data)
w.tx_pending = 1000000
for _ in range(20): w.wave_send()
assert w.curves.send_demo.isChecked()
assert w.wave_tick == 251, w.wave_tick
assert len(w.port.data) == 251
assert w.tx_pending == 1000000 + (251 - initial_frames) * 48
assert all(len(raw) == WIRE.size for raw in w.port.data)
w.curves.send_demo.setChecked(False)
assert not w.wave_timer.isActive() and not w.timer_resolution
w.port = original_port
w.close()
print('PASS continuous TX survives 250ms delay and >1MB queued bytes; catches up in bounded batches; manual stop works')

