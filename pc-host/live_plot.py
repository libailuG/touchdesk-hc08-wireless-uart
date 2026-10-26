"""Bounded receive plotting, independent of UART transport."""
import math
from collections import deque
from PyQt6.QtCore import Qt, QTimer, QRectF, QPointF, pyqtSignal
from PyQt6.QtGui import QPainter, QColor, QPen, QPainterPath
from PyQt6.QtWidgets import QWidget, QVBoxLayout, QHBoxLayout, QCheckBox, QPushButton, QLabel, QGroupBox, QComboBox, QDoubleSpinBox, QSpinBox

COLORS = ['#2476c9','#d16a14','#26965b','#9453b6','#c44865','#15888b','#8a7616','#5e62be']

class PlotCanvas(QWidget):
    def __init__(self, owner):
        super().__init__(owner); self.owner = owner; self.setMinimumHeight(300)

    def paintEvent(self, event):
        p = QPainter(self); p.setRenderHint(QPainter.RenderHint.Antialiasing)
        box = QRectF(95, 30, max(1,self.width()-120), max(1,self.height()-95))
        p.fillRect(box, QColor('#fbfcff')); p.setPen(QPen(QColor('#d2dce7'),1)); p.drawRect(box)
        data = self.owner.display_data(); selected = [i for i,c in enumerate(self.owner.channels) if c.isChecked()]
        values = [row[1][i+2] for row in data for i in selected if math.isfinite(row[1][i+2])]
        lo,hi = (min(values),max(values)) if values else (0.,1.)
        pad = (hi-lo)*.08 if hi>lo else max(abs(lo)*.05,1e-6)
        lo-=pad; hi+=pad
        xmin,xmax = (data[0][0],data[-1][0]) if data else (0,1)
        if xmin==xmax: xmin-=1; xmax+=1
        for i in range(5):
            t=i/4; x=box.left()+box.width()*t; y=box.bottom()-box.height()*t
            p.setPen(QPen(QColor('#e1e7ee'),1));p.drawLine(QPointF(x,box.top()),QPointF(x,box.bottom()));p.drawLine(QPointF(box.left(),y),QPointF(box.right(),y))
            p.setPen(QColor('#526277'))
            p.drawText(QRectF(0,y-10,box.left()-9,20),Qt.AlignmentFlag.AlignRight|Qt.AlignmentFlag.AlignVCenter,format(lo+(hi-lo)*t,'.4g'))
            label_x=x if i==0 else x-70 if i==4 else x-35
            align=Qt.AlignmentFlag.AlignLeft if i==0 else Qt.AlignmentFlag.AlignRight if i==4 else Qt.AlignmentFlag.AlignHCenter
            p.drawText(QRectF(label_x,box.bottom()+6,70,24),align|Qt.AlignmentFlag.AlignVCenter,str(round(xmin+(xmax-xmin)*t)))
        p.drawText(QRectF(0,2,box.left(),22),Qt.AlignmentFlag.AlignCenter,'数值')
        p.drawText(QRectF(box.left(),box.bottom()+35,box.width(),25),Qt.AlignmentFlag.AlignCenter,'发送样本序号' if self.owner.tx_preview.isChecked() else '模拟样本序号' if self.owner.demo.isChecked() else '接收帧序号（非设备采样时间）')
        p.save();p.setClipRect(box.adjusted(-1,-1,1,1))
        for channel in selected:
            path=QPainterPath(); started=False; last=None
            for seq,row in data:
                value=row[channel+2]
                if not math.isfinite(value): started=False;continue
                point=QPointF(box.left()+(seq-xmin)/(xmax-xmin)*box.width(),box.bottom()-(value-lo)/(hi-lo)*box.height())
                if started:path.lineTo(point)
                else:path.moveTo(point);started=True
                last=point
            p.setPen(QPen(QColor(COLORS[channel]),1.7));p.setBrush(Qt.BrushStyle.NoBrush);p.drawPath(path)
            if last:p.setBrush(QColor(COLORS[channel]));p.drawEllipse(last,2.5,2.5)
        p.restore()
        if not data or not selected:
            p.setPen(QColor('#526277'));p.drawText(box,Qt.AlignmentFlag.AlignCenter,'等待接收数据' if not data else '请选择通道')
        p.end()

class LivePlot(QWidget):
    waveSendRequested=pyqtSignal(bool)
    def __init__(self):
        super().__init__();self.history=deque(maxlen=2000);self.demo_history=deque(maxlen=2000)
        self.tx_history=deque(maxlen=2000)
        self.frozen=None;self.dirty=True;self.demo_seq=0
        layout=QVBoxLayout(self); row=QHBoxLayout();layout.addLayout(row)
        self.channels=[]
        for i in range(8):
            check=QCheckBox(f'data{i+3}');check.setChecked(i==0);check.setStyleSheet(f'color:{COLORS[i]};')
            check.toggled.connect(self.invalidate);row.addWidget(check);self.channels.append(check)
        row.addStretch();controls=QHBoxLayout();layout.addLayout(controls)
        self.pause=QPushButton('暂停显示');self.pause.setCheckable(True);self.pause.toggled.connect(self.pause_changed);controls.addWidget(self.pause)
        clear=QPushButton('清空曲线');clear.clicked.connect(self.clear);controls.addWidget(clear)
        self.demo=QCheckBox('本地模拟预览');self.demo.toggled.connect(self.mode_changed);controls.addWidget(self.demo)
        controls.addStretch()
        sender=QGroupBox('持续 sin 发送 · y = 偏置 + 幅值 × sin(2π f t)');sender_layout=QVBoxLayout(sender);settings=QHBoxLayout();sender_layout.addLayout(settings);layout.addWidget(sender)
        self.wave_target=QComboBox();self.wave_target.addItems([f'data{i}' for i in range(3,11)]+['data3～data10'])
        self.wave_amplitude=QDoubleSpinBox();self.wave_amplitude.setRange(0,1e6);self.wave_amplitude.setDecimals(4);self.wave_amplitude.setValue(1)
        self.wave_frequency=QDoubleSpinBox();self.wave_frequency.setRange(.01,40);self.wave_frequency.setDecimals(3);self.wave_frequency.setValue(.5);self.wave_frequency.setSuffix(' Hz')
        self.wave_offset=QDoubleSpinBox();self.wave_offset.setRange(-1e6,1e6);self.wave_offset.setDecimals(4)
        self.wave_period=QSpinBox();self.wave_period.setRange(1,1000);self.wave_period.setSingleStep(1);self.wave_period.setValue(1);self.wave_period.setSuffix(' ms')
        self.wave_fields=[self.wave_target,self.wave_amplitude,self.wave_frequency,self.wave_offset,self.wave_period]
        for title,control in zip(['通道','幅值','频率','偏置','周期'],self.wave_fields):settings.addWidget(QLabel(title));settings.addWidget(control)
        self.send_demo=QPushButton('开始持续发送 sin');self.send_demo.setCheckable(True);self.send_demo.toggled.connect(self.waveSendRequested.emit);settings.addWidget(self.send_demo)
        sender_status=QHBoxLayout();sender_layout.addLayout(sender_status)
        self.tx_preview=QCheckBox('显示发送 sin 轨迹');self.tx_preview.toggled.connect(self.tx_mode_changed);sender_status.addWidget(self.tx_preview)
        self.wave_status=QLabel('未发送 · 1 ms/帧，目标 1000 帧/s · 系统调度可能有抖动');sender_status.addWidget(self.wave_status,1)
        self.source=QLabel('串口接收 · 最近 2000 帧');layout.addWidget(self.source)
        self.canvas=PlotCanvas(self);layout.addWidget(self.canvas,1)
        self.summary=QLabel('等待接收数据');self.summary.setWordWrap(True);layout.addWidget(self.summary)
        self.repaint_timer=QTimer(self);self.repaint_timer.setInterval(50);self.repaint_timer.timeout.connect(self.refresh);self.repaint_timer.start()
        self.demo_timer=QTimer(self);self.demo_timer.setInterval(50);self.demo_timer.timeout.connect(self.demo_tick)

    def append(self, sequence, values):
        self.history.append((sequence,tuple(values)));self.dirty=True

    def append_tx(self, sequence, values):
        self.tx_history.append((sequence,tuple(values)));self.dirty=True

    def sending(self, enabled):
        self.send_demo.setText('停止发送' if enabled else '开始持续发送 sin')
        for field in self.wave_fields:field.setEnabled(not enabled)

    def invalidate(self, *args):self.dirty=True

    def display_data(self):
        if self.frozen is not None:return self.frozen
        return list(self.tx_history if self.tx_preview.isChecked() else self.demo_history if self.demo.isChecked() else self.history)

    def pause_changed(self, checked):
        self.frozen=self.display_data() if checked else None
        self.pause.setText('继续显示' if checked else '暂停显示');self.dirty=True

    def clear(self):
        self.history.clear();self.demo_history.clear();self.tx_history.clear()
        if self.frozen is not None:self.frozen=[]
        self.dirty=True;self.refresh(force=True)

    def mode_changed(self, enabled):
        if enabled:self.tx_preview.setChecked(False)
        self.pause.setChecked(False)
        self.source.setText('本地模拟预览 · 不发送串口' if enabled else '串口接收 · 最近 2000 帧')
        if enabled:self.demo_tick();self.demo_timer.start()
        else:self.demo_timer.stop()
        self.dirty=True

    def tx_mode_changed(self, enabled):
        if enabled:self.demo.setChecked(False)
        self.pause.setChecked(False)
        self.source.setText('发送 sin 轨迹 · 已交给串口，不代表设备反馈' if enabled else '本地模拟预览 · 不发送串口' if self.demo.isChecked() else '串口接收 · 最近 2000 帧')
        self.dirty=True

    def demo_tick(self):
        self.demo_seq+=1;t=self.demo_seq*.05
        self.demo_history.append((self.demo_seq,(0,0,*[math.sin(t*(i+1))*(i+1) for i in range(8)])));self.dirty=True

    def refresh(self, force=False):
        if not self.dirty or (not force and not self.isVisible()):return
        data=self.display_data();current=data[-1][1] if data else None
        parts=[f'data{i+3}={current[i+2]:.6g}' for i,c in enumerate(self.channels) if c.isChecked()] if current else []
        self.summary.setText(('暂停显示，串口仍在接收 · ' if self.pause.isChecked() else '')+f'缓存 {len(data)}/2000 · '+'   '.join(parts))
        self.canvas.update();self.dirty=False
