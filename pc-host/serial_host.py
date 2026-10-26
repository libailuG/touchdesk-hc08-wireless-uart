"""TouchDesk UART test console; Python + PyQt6/QtSerialPort only."""
import argparse
import math
import struct
import sys
from datetime import datetime
from PyQt6.QtCore import QIODevice, Qt, QTimer, QElapsedTimer
from PyQt6.QtGui import QFont
from PyQt6.QtSerialPort import QSerialPort, QSerialPortInfo
from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout,
    QHBoxLayout, QGridLayout, QGroupBox, QLabel, QLineEdit, QPushButton,
    QComboBox, QCheckBox, QPlainTextEdit, QSplitter, QMessageBox, QTabWidget)
from live_plot import LivePlot

WIRE = struct.Struct('<III8fI')
START, END = b'\xaa'*4, b'\xee'*4

def encode(texts):
    integers = []
    for i, text in enumerate(texts[:2]):
        if not text or any(c not in '0123456789' for c in text):
            raise ValueError(f'data{i+1} 必须是十进制 uint32 整数')
        n = int(text)
        if n > 0xffffffff:
            raise ValueError(f'data{i+1} 范围为 0~4294967295')
        integers.append(n)
    floats = []
    for i, text in enumerate(texts[2:], 3):
        try:
            n = float(text)
            if not math.isfinite(n):
                raise ValueError()
            struct.pack('<f', n)
        except (ValueError, OverflowError):
            raise ValueError(f'data{i} 必须是有限的 float32 数值') from None
        floats.append(n)
    if len(integers) != 2 or len(floats) != 8:
        raise ValueError('需要十项数据')
    return WIRE.pack(0xaaaaaaaa, *integers, *floats, 0xeeeeeeee)

class Parser:
    def __init__(self):
        self.buffer = bytearray()
        self.discarded = self.bad_tail = 0

    def feed(self, data):
        self.buffer.extend(data)
        result = []
        while True:
            pos = self.buffer.find(START)
            if pos < 0:
                count = max(0, len(self.buffer)-3)
                self.discarded += count
                del self.buffer[:count]
                break
            self.discarded += pos
            del self.buffer[:pos]
            if len(self.buffer) < 48:
                break
            if self.buffer[44:48] != END:
                self.bad_tail += 1
                self.discarded += 1
                del self.buffer[0]
                continue
            result.append(bytes(self.buffer[:48]))
            del self.buffer[:48]
        return result

class Window(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle('TouchDesk 串口上位机 · 实时曲线')
        self.resize(1080, 850)
        self.port = QSerialPort(self)
        self.port.readyRead.connect(self.read_ready)
        self.port.errorOccurred.connect(self.port_error)
        self.port.bytesWritten.connect(self.written)
        self.parser = Parser()
        self.tx = self.rx = self.tx_pending = 0
        self.tx_partial = 0
        self.last_frame = None
        root = QWidget(); self.setCentralWidget(root)
        layout = QVBoxLayout(root); layout.setSpacing(12)
        title = QLabel('串口结构体收发'); title.setStyleSheet('font-size:24px;font-weight:600')
        layout.addWidget(title)
        line = QHBoxLayout(); layout.addLayout(line)
        line.addWidget(QLabel('串口'))
        self.ports = QComboBox(); self.ports.setMinimumWidth(280); line.addWidget(self.ports)
        self.scan = QPushButton('刷新'); self.scan.clicked.connect(self.refresh_ports); line.addWidget(self.scan)
        self.profile=QComboBox();self.profile.addItem('HC-08 无线 · 115200',115200);self.profile.addItem('有线 · 921600',921600);line.addWidget(self.profile)
        self.profile.currentIndexChanged.connect(self.profile_changed)
        line.addStretch()
        self.connect_button = QPushButton('连接'); self.connect_button.clicked.connect(self.toggle_connection)
        line.addWidget(self.connect_button)
        layout.addWidget(QLabel('48 字节 · 小端 · AA AA AA AA + 2×uint32 + 8×float32 + EE EE EE EE · 无 CRC'))
        self.pages=QTabWidget();layout.addWidget(self.pages,1)
        data_page=QWidget();data_layout=QVBoxLayout(data_page);self.pages.addTab(data_page,'数据收发')
        self.curves=LivePlot();self.pages.addTab(self.curves,'实时曲线');self.curves.waveSendRequested.connect(self.wave_mode)
        self.wave_tick=0;self.wave_timer=QTimer(self);self.wave_timer.setInterval(50);self.wave_timer.timeout.connect(self.wave_send)
        self.wave_clock=QElapsedTimer();self.wave_settings=None
        self.curves.wave_period.setRange(1,1000);self.curves.wave_period.setValue(100)
        self.curves.wave_status.setText('无线初始周期 100 ms · 连续发送默认关闭')
        self.wave_tx_base=0;self.timer_resolution=False
        self.wave_stats=QTimer(self);self.wave_stats.setInterval(100);self.wave_stats.timeout.connect(self.wave_statistics)
        panels = QHBoxLayout(); data_layout.addLayout(panels)
        send_box = QGroupBox('发送指令'); receive_box = QGroupBox('接收数据 · 最近一帧')
        panels.addWidget(send_box); panels.addWidget(receive_box)
        send_grid = QGridLayout(send_box); receive_grid = QGridLayout(receive_box)
        self.inputs = []; self.received = []
        self.state_buttons = []; self.state_lamps = []
        state_row = QHBoxLayout(); lamp_row = QHBoxLayout()
        send_grid.addWidget(QLabel('data1 低4位 · bit0→bit3'), 0, 0, 1, 3)
        receive_grid.addWidget(QLabel('data1 低4位 · bit0→bit3'), 0, 0, 1, 3)
        send_grid.addLayout(state_row, 1, 0, 1, 3); receive_grid.addLayout(lamp_row, 1, 0, 1, 3)
        for bit in range(4):
            button = QPushButton(f'状态{bit+1} 关'); button.setCheckable(True)
            button.setMinimumHeight(36)
            button.clicked.connect(lambda checked, bit=bit: self.toggle_state(bit, checked))
            self.state_buttons.append(button); state_row.addWidget(button)
            lamp = QLabel(f'状态{bit+1} 未知'); lamp.setAlignment(Qt.AlignmentFlag.AlignCenter)
            lamp.setMinimumHeight(36); self.state_lamps.append(lamp); lamp_row.addWidget(lamp)
        for i in range(10):
            name = f'data{i+1}'; kind = 'uint32' if i < 2 else 'float32'
            edit = QLineEdit('0'); edit.setMaxLength(40); edit.setObjectName(name)
            edit.setPlaceholderText('0~4294967295' if i < 2 else '支持负数、小数、e')
            # Enter edits the field; transmitting always requires the send button.
            edit.returnPressed.connect(lambda: None)
            send_grid.addWidget(QLabel(name), i+2, 0); send_grid.addWidget(QLabel(kind), i+2, 1)
            send_grid.addWidget(edit, i+2, 2); self.inputs.append(edit)
            value = QLineEdit('—'); value.setReadOnly(True)
            receive_grid.addWidget(QLabel(name), i+2, 0); receive_grid.addWidget(QLabel(kind), i+2, 1)
            receive_grid.addWidget(value, i+2, 2); self.received.append(value)
        self.inputs[0].textChanged.connect(self.sync_states)
        self.send_button = QPushButton('发送一次 · 48 字节'); self.send_button.setMinimumHeight(40)
        self.send_button.clicked.connect(self.send_once); self.send_button.setEnabled(False)
        send_grid.addWidget(self.send_button, 12, 0, 1, 3)
        self.echo = QCheckBox('原帧回显（收到有效帧后返回一次）')
        receive_grid.addWidget(self.echo, 12, 0, 1, 3)
        self.receive_time = QLabel('尚未接收'); receive_grid.addWidget(self.receive_time, 13, 0, 1, 3)
        controls = QHBoxLayout(); data_layout.addLayout(controls)
        self.counts = QLabel(); controls.addWidget(self.counts); controls.addStretch()
        self.log_enabled = QCheckBox('记录每帧 HEX'); self.log_enabled.setChecked(False); controls.addWidget(self.log_enabled)
        clear = QPushButton('清空日志'); clear.clicked.connect(lambda: self.log.clear()); controls.addWidget(clear)
        self.log = QPlainTextEdit(); self.log.setReadOnly(True); self.log.setMaximumBlockCount(1000)
        self.log.setMinimumHeight(100); self.log.setFont(QFont('Consolas', 10)); data_layout.addWidget(self.log, 1)
        self.statusBar().showMessage('未连接；请先在开发板打开“串口助手”')
        self.setStyleSheet('QGroupBox{font-weight:600} QLineEdit{padding:5px} QPushButton{padding:6px 12px} QMainWindow{background:#f3f6fb}')
        self.refresh_ports(); self.update_counts(); self.sync_states(); self.show_lamps(None)

    @staticmethod
    def state_style(on):
        return ('background:#20844b;color:white;' if on else 'background:#e1e6ed;color:#253044;')+'border-radius:5px;padding:5px;'

    def data1_value(self):
        text = self.inputs[0].text().strip()
        if not text or any(c not in '0123456789' for c in text): return None
        value = int(text)
        return value if value <= 0xffffffff else None

    def sync_states(self):
        value = self.data1_value()
        for bit, button in enumerate(self.state_buttons):
            on = value is not None and bool(value & (1 << bit))
            button.setChecked(on); button.setEnabled(value is not None)
            button.setText(f'状态{bit+1} '+('无效' if value is None else ('开' if on else '关')))
            button.setStyleSheet(self.state_style(on))

    def toggle_state(self, bit, checked):
        value = self.data1_value()
        if value is None:
            self.sync_states(); return
        value = (value | (1 << bit)) if checked else (value & ~(1 << bit))
        self.inputs[0].setText(str(value))
        self.send_once(reason='开关')

    def show_lamps(self, value):
        for bit, lamp in enumerate(self.state_lamps):
            on = value is not None and bool(value & (1 << bit))
            lamp.setText(f'状态{bit+1} '+('未知' if value is None else ('开' if on else '关')))
            lamp.setStyleSheet(self.state_style(on))

    def refresh_ports(self):
        selected = self.ports.currentData()
        self.ports.clear()
        for info in QSerialPortInfo.availablePorts():
            # COM9 is the board's reset-only CH340 with its TX physically isolated.
            if info.portName().upper() == 'COM9':
                continue
            self.ports.addItem(f'{info.portName()} — {info.description()}', info.portName())
        index = self.ports.findData(selected or 'COM10')
        if index >= 0: self.ports.setCurrentIndex(index)

    def note(self, text):
        self.log.appendPlainText(f'{datetime.now():%H:%M:%S.%f}'[:-3]+'  '+text)

    def profile_changed(self):
        wireless=self.profile.currentData()==115200
        self.curves.wave_period.setRange(1,1000)
        self.curves.wave_period.setValue(100 if wireless else 1)
        self.curves.wave_status.setText('无线初始周期 100 ms · 请先验证单帧' if wireless else '有线模式 · 连续发送默认关闭')

    def toggle_connection(self):
        if self.port.isOpen():
            self.disconnect_port()
            return
        name = self.ports.currentData()
        if not name:
            self.statusBar().showMessage('未找到可用串口，请刷新')
            return
        self.port.setPortName(name); self.port.setBaudRate(self.profile.currentData())
        self.port.setDataBits(QSerialPort.DataBits.Data8)
        self.port.setParity(QSerialPort.Parity.NoParity)
        self.port.setStopBits(QSerialPort.StopBits.OneStop)
        self.port.setFlowControl(QSerialPort.FlowControl.NoFlowControl)
        if not self.port.open(QIODevice.OpenModeFlag.ReadWrite):
            self.note('连接失败：'+self.port.errorString())
            self.statusBar().showMessage('连接失败，请检查端口是否被占用')
            return
        self.parser = Parser(); self.tx_pending = self.tx_partial = 0
        self.curves.clear()
        self.last_frame = None
        self.show_lamps(None); self.receive_time.setText('本次连接尚未接收')
        for value in self.received: value.setText('—')
        self.profile.setEnabled(False)
        self.connect_button.setText('断开'); self.ports.setEnabled(False); self.scan.setEnabled(False)
        self.send_button.setEnabled(True)
        self.note(f'已连接 {name}，{self.profile.currentData()} 8N1')
        self.statusBar().showMessage(f'{name} 已连接；点击发送一次，或在开发板点击发送')
        self.update_counts()

    def disconnect_port(self):
        self.curves.send_demo.setChecked(False);self.wave_timer.stop()
        self.port.close(); self.tx_pending = self.tx_partial = 0
        self.show_lamps(None); self.receive_time.setText('已断开，数值为历史记录')
        self.profile.setEnabled(True)
        self.connect_button.setText('连接'); self.ports.setEnabled(True); self.scan.setEnabled(True)
        self.send_button.setEnabled(False); self.note('已断开，端口已释放')
        self.statusBar().showMessage('未连接')

    def port_error(self, error):
        if error == QSerialPort.SerialPortError.ResourceError and self.port.isOpen():
            self.note('串口异常：'+self.port.errorString()); self.disconnect_port()

    def write_frame(self, raw, reason):
        if not self.port.isOpen():
            self.statusBar().showMessage('未连接，数据未发送'); self.note('未连接，数据未发送'); return False
        # Qt queues each complete frame, so rapid switch clicks are never merged.
        self.tx_pending += 48
        if self.port.write(raw) != 48:
            self.note('发送失败：'+self.port.errorString()); self.disconnect_port(); return False
        if self.log_enabled.isChecked(): self.note(f'TX {reason}  '+raw.hex(' ').upper())
        return True

    def written(self, count):
        if not self.tx_pending: return
        completed = min(count, self.tx_pending)
        self.tx_pending -= completed; self.tx_partial += completed
        frames, self.tx_partial = divmod(self.tx_partial, 48)
        if frames:
            self.tx += frames
            if not self.wave_timer.isActive():
                self.update_counts();self.statusBar().showMessage('48 字节已写入串口；对端接收请查看开发板接收页')

    def send_once(self, checked=False, reason='手动'):
        try:
            raw = encode([edit.text().strip() for edit in self.inputs])
        except ValueError as error:
            self.statusBar().showMessage(str(error)); self.note('未发送：'+str(error)); return
        self.write_frame(raw, reason)

    def read_ready(self):
        self.consume(bytes(self.port.readAll()))

    def consume(self, data):
        latest=None
        for raw in self.parser.feed(data):
            self.rx += 1; self.last_frame = raw
            latest=WIRE.unpack(raw)[1:-1];self.curves.append(self.rx,latest)
            if self.log_enabled.isChecked(): self.note('RX  '+raw.hex(' ').upper())
            if self.echo.isChecked(): self.write_frame(raw, '回显')
        if latest is not None:
            self.show_lamps(latest[0])
            for i,value in enumerate(latest):self.received[i].setText(str(value) if i<2 else format(value,'.9g'))
            self.receive_time.setText(f'最近接收 {datetime.now():%H:%M:%S.%f}'[:-3])
        self.update_counts()

    def wave_mode(self, enabled):
        if enabled and not self.port.isOpen():
            self.curves.send_demo.setChecked(False);self.statusBar().showMessage('请先连接串口');return
        if enabled:
            c=self.curves
            if c.wave_frequency.value()*c.wave_period.value()/1000>=.5:
                c.send_demo.setChecked(False);self.statusBar().showMessage('发送采样率必须大于 sin 频率的两倍');return
            try:encode([field.text().strip() for field in self.inputs])
            except ValueError as error:
                c.send_demo.setChecked(False);self.statusBar().showMessage(str(error));return
            self.wave_settings=(c.wave_target.currentIndex(),c.wave_amplitude.value(),c.wave_frequency.value(),c.wave_offset.value(),c.wave_period.value())
            self.wave_tick=0;self.wave_tx_base=self.tx;self.wave_clock.start();self.wave_timer.setTimerType(Qt.TimerType.PreciseTimer);self.wave_timer.setInterval(self.wave_settings[4]);c.tx_history.clear();c.pause.setChecked(False);c.tx_preview.setChecked(True);c.sending(True)
            if sys.platform=='win32':
                import ctypes
                self.timer_resolution=ctypes.windll.winmm.timeBeginPeriod(1)==0
            if self.wave_settings[4]<=5:self.log_enabled.setChecked(False)
            for i,channel in enumerate(c.channels):channel.setChecked(self.wave_settings[0]==8 or i==self.wave_settings[0])
            self.wave_stats.start();self.wave_timer.start();self.wave_send();self.note('开始持续 sin 发送；点击“停止发送”结束')
        else:
            self.wave_timer.stop();self.wave_stats.stop();self.release_timer_resolution();self.curves.sending(False);self.update_counts();self.curves.wave_status.setText(f'已停止 · 本次 {self.wave_tick} 帧已排队；在途帧可能仍会到达')

    def release_timer_resolution(self):
        if self.timer_resolution:
            import ctypes
            ctypes.windll.winmm.timeEndPeriod(1);self.timer_resolution=False

    def wave_statistics(self):
        if not self.wave_settings or not self.wave_clock.isValid():return
        elapsed=max(self.wave_clock.nsecsElapsed()/1e9,.001)
        self.update_counts();period=self.wave_settings[4]
        self.curves.wave_status.setText(f'发送中 · 目标 {1000/period:g} 帧/s · 实际TX {(self.tx-self.wave_tx_base)/elapsed:.0f} 帧/s · 已生成 {self.wave_tick} 帧 · 排队 {self.tx_pending//48} 帧')

    def wave_frame(self, seconds):
        target,amplitude,frequency,offset,period=self.wave_settings
        values=[field.text().strip() for field in self.inputs]
        value=offset+amplitude*math.sin(2*math.pi*frequency*seconds)
        for channel in (range(8) if target==8 else [target]):values[channel+2]=str(value)
        return encode(values)

    def wave_send(self):
        if self.wave_settings is None or not self.wave_clock.isValid():return
        period=self.wave_settings[4];due=1+self.wave_clock.nsecsElapsed()//(period*1000000)
        # Limit work per callback to keep Stop responsive; delays do not stop sending.
        for _ in range(min(16,max(0,due-self.wave_tick))):
            try:raw=self.wave_frame(self.wave_tick*period/1000)
            except ValueError as error:
                self.curves.send_demo.setChecked(False);self.statusBar().showMessage(str(error));return
            if not self.write_frame(raw,'sin'):return
            self.wave_tick+=1;self.curves.append_tx(self.wave_tick,WIRE.unpack(raw)[1:-1])

    def update_counts(self):
        self.counts.setText(f'TX {self.tx} 帧    RX {self.rx} 帧    丢弃 {self.parser.discarded} 字节    错误帧尾 {self.parser.bad_tail}')

    def closeEvent(self, event):
        self.wave_timer.stop();self.wave_stats.stop();self.release_timer_resolution();self.curves.demo_timer.stop()
        if self.port.isOpen(): self.port.close()
        event.accept()

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--connect', action='store_true');parser.add_argument('--curve',action='store_true');parser.add_argument('--wired',action='store_true')
    args = parser.parse_args()
    app = QApplication(sys.argv[:1]); app.setFont(QFont('Microsoft YaHei UI', 10))
    window = Window(); window.show()
    if args.wired:window.profile.setCurrentIndex(1)
    if args.curve:window.pages.setCurrentIndex(1)
    if args.connect: QTimer.singleShot(200, window.toggle_connection)
    return app.exec()

if __name__ == '__main__':
    sys.exit(main())
