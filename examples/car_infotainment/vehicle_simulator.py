#!/usr/bin/env python3
import sys
import threading
import struct
import time
import math
from datetime import datetime
from collections import deque
from typing import Callable, Optional, Dict, Tuple, List, Any

#sudo ip link add dev vcan0 type vcan
#sudo ip link set up vcan0

try:
    import can
except ImportError:
    print("pip install python-can")
    exit(1)

try:
    from PyQt6.QtWidgets import (QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
                                 QLabel, QPushButton, QSlider, QSpinBox, QDoubleSpinBox,
                                 QCheckBox, QComboBox, QTextEdit, QTabWidget, QGraphicsView,
                                 QGraphicsScene, QFrame, QSizePolicy)
    from PyQt6.QtCore import Qt, pyqtSignal, QObject, QTimer, QRectF, QPointF
    from PyQt6.QtGui import QColor, QPainter, QPen, QBrush, QFont, QPolygonF, QTextCursor
except ImportError:
    msg = "pip install PyQt6"
    print(msg)
    exit(1)

T = {
    "bg": "#FFFFFF", "surface": "#F7F7F7", "surface2": "#EFEFEF",
    "border": "#E0E0E0", "border2": "#CCCCCC", "fg": "#1A1A1A",
    "fg2": "#5E5E5E", "fg3": "#9E9E9E", "red": "#1D4ED8",
    "red_dim": "#FCE4E4", "green": "#2C6B2F", "blue": "#1D4ED8",
    "amber": "#F59E0B", "purple": "#6B21A8", "teal": "#0D9488",
    "log_bg": "#F8F8F8", "log_fg": "#333333", "log_sim": "#6B7280",
    "log_sw": "#1D4ED8", "log_hmi": "#2C6B2F", "log_err": "#DC2626",
}

GROUP_COLORS = {
    "media": T["blue"], "phone": T["green"], "voice": T["purple"],
    "cruise": T["amber"], "info": T["teal"], "misc": T["fg2"],
}


class CANBus:
    def __init__(self) -> None:
        self.bus: Any = None
        self.connected = False
        try:
            self.bus = can.interface.Bus(channel="vcan0", bustype="socketcan")
            self.connected = True
        except Exception as e:
            print(f"vcan0 unavailable ({e}) — simulation-only mode")

    def send(self, arb_id: int, data: bytes) -> Tuple[bool, str]:
        hex_data = " ".join(f"{b:02X}" for b in data)
        if self.connected:
            try:
                msg = can.Message(arbitration_id=arb_id, data=data, is_extended_id=False)
                self.bus.send(msg)
                return True, f"TX  [{hex(arb_id):>6}]  {hex_data}"
            except Exception as e:
                return False, f"ERR [{hex(arb_id):>6}]  {e}"
        else:
            return True, f"SIM [{hex(arb_id):>6}]  {hex_data}"

    def close(self) -> None:
        try:
            if self.bus:
                self.bus.shutdown()
        except:
            pass


SW_BUTTONS = {
    "VOL+": (0x200, 0x01, "media", "Volume up"),
    "VOL-": (0x200, 0x02, "media", "Volume down"),
    "MUTE": (0x200, 0x03, "media", "Mute toggle"),
    "PREV": (0x200, 0x04, "media", "Previous track"),
    "NEXT": (0x200, 0x05, "media", "Next track"),
    "SRC": (0x200, 0x06, "media", "Media source cycle"),
    "PHONE": (0x201, 0x10, "phone", "Answer / end call"),
    "REJECT": (0x201, 0x11, "phone", "Reject call"),
    "VOICE": (0x203, 0x20, "voice", "Voice assistant trigger"),
    "CC ON": (0x202, 0x30, "cruise", "Cruise control on/off"),
    "CC +": (0x202, 0x31, "cruise", "Cruise speed +1 km/h"),
    "CC -": (0x202, 0x32, "cruise", "Cruise speed −1 km/h"),
    "CC RES": (0x202, 0x33, "cruise", "Resume last cruise speed"),
    "CC SET": (0x202, 0x34, "cruise", "Set cruise to current speed"),
    "INFO": (0x204, 0x40, "info", "Instrument cluster info cycle"),
    "TRIP": (0x204, 0x41, "info", "Trip computer reset/cycle"),
    "BRIGHT+": (0x205, 0x50, "misc", "Display brightness up"),
    "BRIGHT-": (0x205, 0x51, "misc", "Display brightness down"),
    "MENU": (0x205, 0x52, "misc", "Infotainment home/menu"),
    "BACK": (0x205, 0x53, "misc", "Back / cancel"),
    "OK": (0x205, 0x54, "misc", "Confirm / OK"),
}

HMI_COMMANDS = {
    "Nav Home": (0x01, 0x00, 0x00, "Navigate to home address"),
    "Nav Work": (0x01, 0x01, 0x00, "Navigate to work address"),
    "Radio FM": (0x02, 0x01, 0x00, "Switch to FM radio"),
    "Radio AM": (0x02, 0x02, 0x00, "Switch to AM radio"),
    "Bluetooth": (0x02, 0x03, 0x00, "Switch to Bluetooth audio"),
    "Screen Off": (0x03, 0x00, 0x00, "Turn off centre display"),
    "Screen On": (0x03, 0x01, 0x00, "Turn on centre display"),
    "Night Mode": (0x04, 0x01, 0x00, "Enable night/dark mode"),
    "Day Mode": (0x04, 0x00, 0x00, "Enable day/light mode"),
    "Fan +": (0x05, 0x01, 0x00, "HVAC fan speed up"),
    "Fan -": (0x05, 0xFF, 0x00, "HVAC fan speed down"),
    "Defrost": (0x06, 0x01, 0x00, "Toggle rear defrost"),
}


class EVSimulator:
    def __init__(self, bus: "CANBus") -> None:
        self.bus = bus
        self.running = False
        self.soc = 85.0
        self.voltage = 390.0
        self.current = 0.0
        self.speed = 0.0
        self.rpm = 0.0
        self.gear = 0
        self.pedal = 0.0
        self.hvac_on = False
        self.cabin_temp = 22.0
        self.target_temp = 21.0
        self.fan_speed = 3
        self.range_km = 450.0
        self.fault_code = 0
        self.doors = 0
        self.seat_heaters = 0
        self.log_cb: Any = None
        self.log_buffer: deque[str] = deque(maxlen=100)
        self.last_log_update = 0.0
        self.log_update_interval = 0.5

    def set_log_callback(self, cb: Callable[[str, str], None]) -> None:
        self.log_cb = cb

    def _log(self, message: str) -> None:
        if self.log_cb:
            self.log_buffer.append(message)
            current_time = time.time()
            if current_time - self.last_log_update >= self.log_update_interval:
                if self.log_buffer:
                    batch = "\n".join(self.log_buffer)
                    self.log_buffer.clear()
                    ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                    try:
                        self.log_cb(ts, batch)
                    except:
                        pass
                self.last_log_update = current_time

    def start(self) -> None:
        self.running = True
        threading.Thread(target=self._loop, daemon=True).start()

    def _loop(self) -> None:
        while self.running:
            # Drive dynamics
            if self.gear == 3:
                accel = (self.pedal * 0.015) - (self.speed * 0.005) - 0.1
                self.speed = max(0.0, min(240.0, self.speed + accel))
                self.rpm = self.speed * 100
                self.current = self.pedal * 2.0
            elif self.gear == 1:
                accel = (self.pedal * 0.005) - (self.speed * 0.01) - 0.05
                self.speed = max(0.0, min(40.0, self.speed + accel))
                self.rpm = -self.speed * 100
                self.current = self.pedal * 1.5
            else:
                self.speed = max(0.0, self.speed - 0.5)
                self.rpm = 0.0
                self.current = 0.0

            # HVAC simulation
            hvac_draw = 0.0
            if self.hvac_on:
                diff = self.cabin_temp - self.target_temp
                if abs(diff) > 0.5:
                    step = 0.01 * max(1, self.fan_speed)
                    self.cabin_temp -= step if diff > 0 else -step
                hvac_draw = 1.0 * self.fan_speed + (5.0 if abs(diff) > 2.0 else 2.0)
            else:
                self.cabin_temp += 0.005

            seat_heater_draw = bin(self.seat_heaters).count("1") * 0.5
            self.current += hvac_draw + seat_heater_draw

            self.soc = max(0.0, self.soc - self.current * 0.0001)
            self.voltage = 350 + (self.soc / 100) * 50
            self.range_km = self.soc * 5.0

            rpm_i = max(-32768, min(32767, int(self.rpm)))
            self.bus.send(
                0x100,
                struct.pack(
                    ">HhH",
                    int(self.soc * 100),
                    int(self.current * 10),
                    int(self.voltage * 10),
                ),
            )
            self.bus.send(
                0x101,
                struct.pack(
                    ">HiBB", int(self.speed * 10), rpm_i, self.gear, int(self.pedal)
                ),
            )
            self.bus.send(
                0x102,
                struct.pack(
                    ">hBBBBB",
                    int(self.cabin_temp * 10),
                    int(self.hvac_on),
                    self.doors,
                    self.fan_speed,
                    int(self.target_temp),
                    self.seat_heaters,
                ),
            )
            self.bus.send(0x103, struct.pack(">H", int(self.range_km)))
            self.bus.send(0x104, struct.pack(">I", self.fault_code))

            gear_n = {0: "P", 1: "R", 2: "N", 3: "D"}.get(self.gear, "?")
            fault_flag = " ⚠" if self.fault_code else ""
            msg = (
                f"[SIM]  SoC={self.soc:.1f}%  Spd={self.speed:.1f}km/h  "
                f"Gear={gear_n}  Ped={self.pedal:.0f}%  "
                f"AC={'ON' if self.hvac_on else 'OFF'}  T={self.cabin_temp:.1f}°C{fault_flag}"
            )
            self._log(msg)
            time.sleep(0.05)

    def stop(self) -> None:
        self.running = False
        if self.log_buffer and self.log_cb:
            batch = "\n".join(self.log_buffer)
            ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            try:
                self.log_cb(ts, batch)
            except:
                pass


class SteeringWheelQt(QGraphicsView):
    zone_clicked = pyqtSignal(str)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.scene = QGraphicsScene(self)
        self.setScene(self.scene)
        self.setRenderHint(QPainter.RenderHint.Antialiasing)
        self.setFixedSize(250, 250)
        self.setStyleSheet("background: transparent; border: none;")
        self._active_group = None
        self._zones = {}
        self._draw()

    def highlight_group(self, group: str):
        self._active_group = group
        self.scene.clear()
        self._zones.clear()
        self._draw()

    def _draw(self):
        S = 230
        cx, cy = S // 2, S // 2
        R = 95
        r = 30
        spoke_w = 20

        # Base wheel
        self.scene.addEllipse(cx - R - 3, cy - R - 3, 2 * R + 6, 2 * R + 6, QPen(Qt.PenStyle.NoPen), QBrush(QColor("#E0E0E0")))
        self.scene.addEllipse(cx - R, cy - R, 2 * R, 2 * R, QPen(QColor(T["border"])), QBrush(QColor("#F5F5F5")))
        self.scene.addEllipse(cx - R + 8, cy - R + 8, 2 * R - 16, 2 * R - 16, QPen(QColor("#D0D0D0"), 6), QBrush(Qt.BrushStyle.NoBrush))

        spoke_defs = [
            (210, "MEDIA\n◄  VOL  ►", "media"),
            (330, "PHONE\nVOICE", "phone"),
            (150, "INFO\nTRIP", "info"),
            (30, "CRUISE\nCC SET", "cruise"),
        ]

        for angle_deg, label, group in spoke_defs:
            rad = math.radians(angle_deg)
            perp = math.radians(angle_deg + 90)
            hw = spoke_w / 2

            sx = cx + r * math.cos(rad)
            sy = cy - r * math.sin(rad)
            ex = cx + (R - 9) * math.cos(rad)
            ey = cy - (R - 9) * math.sin(rad)

            poly = QPolygonF([
                QPointF(sx + hw * math.cos(perp), sy - hw * math.sin(perp)),
                QPointF(ex + hw * math.cos(perp), ey - hw * math.sin(perp)),
                QPointF(ex - hw * math.cos(perp), ey + hw * math.sin(perp)),
                QPointF(sx - hw * math.cos(perp), sy + hw * math.sin(perp))
            ])
            shape = self.scene.addPolygon(poly, QPen(QColor(T["border"])), QBrush(QColor("#F5F5F5")))
            self._zones[shape] = group

            mx = (sx + ex) / 2
            my = (sy + ey) / 2
            bw, bh = 34, 22
            bx1, by1 = mx - bw, my - bh / 2

            is_active = (group == self._active_group)
            fill_color = QColor(GROUP_COLORS[group]) if is_active else QColor(T["surface2"])
            txt_color = QColor(T["fg"]) if is_active else QColor(T["fg3"])
            outline_c = QColor(GROUP_COLORS[group]) if is_active else QColor(T["border"])
            out_w = 2 if is_active else 1

            rect = self.scene.addRect(bx1, by1, bw * 2, bh, QPen(outline_c, out_w), QBrush(fill_color))
            self._zones[rect] = group
            
            text = self.scene.addText(label.replace('\n', ' '), QFont("Inter", 6, QFont.Weight.Bold))
            text.setDefaultTextColor(txt_color)
            text.setPos(mx - text.boundingRect().width() / 2, my - text.boundingRect().height() / 2)
            self._zones[text] = group

        center_hub = self.scene.addEllipse(cx - r, cy - r, 2 * r, 2 * r, QPen(QColor(T["border2"])), QBrush(QColor("#EBEBEB")))
        t = self.scene.addText("T", QFont("Inter", 16, QFont.Weight.Bold))
        t.setDefaultTextColor(QColor(T["red"]))
        t.setPos(cx - t.boundingRect().width() / 2, cy - t.boundingRect().height() / 2 - 4)
        m = self.scene.addText("MENU", QFont("Inter", 5))
        m.setDefaultTextColor(QColor(T["fg3"]))
        m.setPos(cx - m.boundingRect().width() / 2, cy + 10)
        self._zones[center_hub] = "misc"
        self._zones[t] = "misc"
        self._zones[m] = "misc"

    def mousePressEvent(self, event):
        item = self.itemAt(event.pos())
        if item in self._zones:
            self.zone_clicked.emit(self._zones[item])
        super().mousePressEvent(event)


class Communicate(QObject):
    log_signal = pyqtSignal(str, str, str)

class App(QMainWindow):
    def __init__(self) -> None:
        super().__init__()
        self.setWindowTitle("AromaOS CAN Bus Simulator ")
        self.resize(900, 600)
        self.setStyleSheet(f"background-color: {T['bg']}; color: {T['fg']}; font-family: Inter;")

        self.bus = CANBus()
        self.sim = EVSimulator(self.bus)
        
        self.c = Communicate()
        self.c.log_signal.connect(self._log_raw)
        self.sim.set_log_callback(lambda ts, msg: self.c.log_signal.emit(ts, msg, "sim"))
        self.sim.start()

        self._build()

    def _build(self) -> None:
        widget = QWidget()
        self.setCentralWidget(widget)
        layout = QVBoxLayout(widget)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        # Topbar
        topbar = QFrame()
        topbar.setStyleSheet(f"background-color: {T['surface']};")
        topbar_layout = QHBoxLayout(topbar)
        
        lbl_title = QLabel("AROMAOS")
        lbl_title.setStyleSheet(f"color: {T['red']}; font-weight: bold; font-size: 13pt;")
        topbar_layout.addWidget(lbl_title)
        
        lbl_sub = QLabel("CAN Bus Simulator ")
        lbl_sub.setStyleSheet(f"color: {T['fg3']};")
        topbar_layout.addWidget(lbl_sub)
        
        topbar_layout.addStretch()
        
        vcan_txt = "● vcan0" if self.bus.connected else "○ SIM"
        vcan_color = T["green"] if self.bus.connected else T["amber"]
        lbl_vcan = QLabel(vcan_txt)
        lbl_vcan.setStyleSheet(f"color: {vcan_color};")
        topbar_layout.addWidget(lbl_vcan)
        
        layout.addWidget(topbar)

        # Tabs
        self.tabs = QTabWidget()
        layout.addWidget(self.tabs)

        self.tab_wheel = QWidget()
        self.tab_drive = QWidget()
        self.tab_log = QWidget()
        
        self.tabs.addTab(self.tab_wheel, "Steering Wheel")
        self.tabs.addTab(self.tab_drive, "Vehicle Controls")
        self.tabs.addTab(self.tab_log, "CAN Bus Log")

        self._build_wheel_tab()
        self._build_drive_tab()
        self._build_log_tab()
        
        # Status
        self.lbl_status = QLabel("Ready")
        self.lbl_status.setStyleSheet(f"padding: 5px; background: {T['surface']}; color: {T['fg3']}")
        layout.addWidget(self.lbl_status)

    def _build_wheel_tab(self):
        lyt = QVBoxLayout(self.tab_wheel)
        
        # Center wheel vertically and horizontally
        self.wheel_container = QWidget()
        vbox = QVBoxLayout(self.wheel_container)
        vbox.setAlignment(Qt.AlignmentFlag.AlignCenter)
        
        self.wheel = SteeringWheelQt()
        self.wheel.zone_clicked.connect(self._on_zone_click)
        vbox.addWidget(self.wheel, alignment=Qt.AlignmentFlag.AlignCenter)
        
        self.lbl_wheel_hint = QLabel("Tap a spoke zone to reveal controls")
        self.lbl_wheel_hint.setAlignment(Qt.AlignmentFlag.AlignCenter)
        vbox.addWidget(self.lbl_wheel_hint)
        
        self.zone_title = QLabel("Zone Controls")
        self.zone_title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.zone_title.setStyleSheet("font-weight: bold;")
        vbox.addWidget(self.zone_title)
        
        self.btn_panel = QWidget()
        self.btn_layout = QHBoxLayout(self.btn_panel)
        vbox.addWidget(self.btn_panel)
        
        lyt.addWidget(self.wheel_container)

    def _build_drive_tab(self):
        lyt = QVBoxLayout(self.tab_drive)
        lyt.setAlignment(Qt.AlignmentFlag.AlignTop)

        # Gears
        gear_frame = QFrame()
        gl = QHBoxLayout(gear_frame)
        gl.addWidget(QLabel("GEAR:"))
        self.gear_cb = QComboBox()
        self.gear_cb.addItems(["P", "R", "N", "D"])
        self.gear_cb.currentIndexChanged.connect(self._update_gear)
        gl.addWidget(self.gear_cb)
        gl.addStretch()
        lyt.addWidget(gear_frame)
        
        # Throttle
        pedal_frame = QFrame()
        pl = QHBoxLayout(pedal_frame)
        pl.addWidget(QLabel("THROTTLE:"))
        self.pedal_slider = QSlider(Qt.Orientation.Horizontal)
        self.pedal_slider.setRange(0, 100)
        self.pedal_slider.valueChanged.connect(self._update_pedal)
        pl.addWidget(self.pedal_slider)
        lyt.addWidget(pedal_frame)
        
        # HVAC
        hvac_f = QFrame()
        hl = QHBoxLayout(hvac_f)
        self.chk_ac = QCheckBox("A/C")
        self.chk_ac.stateChanged.connect(self._update_hvac)
        hl.addWidget(self.chk_ac)
        
        hl.addWidget(QLabel("FAN:"))
        self.spin_fan = QSpinBox()
        self.spin_fan.setRange(0, 7)
        self.spin_fan.setValue(3)
        self.spin_fan.valueChanged.connect(self._update_hvac)
        hl.addWidget(self.spin_fan)
        
        hl.addWidget(QLabel("TARGET:"))
        self.spin_temp = QDoubleSpinBox()
        self.spin_temp.setRange(10.0, 30.0)
        self.spin_temp.setSingleStep(0.5)
        self.spin_temp.setValue(21.0)
        self.spin_temp.valueChanged.connect(self._update_hvac)
        hl.addWidget(self.spin_temp)
        
        self.chk_seat = QCheckBox("SEAT HEAT")
        self.chk_seat.stateChanged.connect(self._update_seat_heat)
        hl.addWidget(self.chk_seat)
        hl.addStretch()
        lyt.addWidget(hvac_f)
        
        # Doors
        door_f = QFrame()
        dl = QHBoxLayout(door_f)
        dl.addWidget(QLabel("DOORS:"))
        self.chk_doors = []
        for name in ["FL", "FR", "RL", "RR", "Trunk", "Frunk"]:
            c = QCheckBox(name)
            c.stateChanged.connect(self._update_doors)
            dl.addWidget(c)
            self.chk_doors.append(c)
        dl.addStretch()
        lyt.addWidget(door_f)
        
        # Faults
        fault_f = QFrame()
        fl = QHBoxLayout(fault_f)
        fl.addWidget(QLabel("FAULT:"))
        self.faults = {
            "No Fault": 0x0000, "BMS: Cell Overvoltage": 0xB101, "BMS: Cell Undervoltage": 0xB102,
            "BMS: Isolation Fault": 0xB100, "Motor: Inverter Overtemp": 0xC201,
            "Motor: Drive Inverter Fault": 0xC200, "Battery: Critical Low": 0xA100,
            "HVAC: Compressor Fault": 0xD300,
        }
        self.fault_cb = QComboBox()
        self.fault_cb.addItems(list(self.faults.keys()))
        self.fault_cb.currentIndexChanged.connect(self._on_fault_select)
        fl.addWidget(self.fault_cb)
        btn_clr = QPushButton("Clear")
        btn_clr.clicked.connect(lambda: self.fault_cb.setCurrentIndex(0))
        fl.addWidget(btn_clr)
        fl.addStretch()
        lyt.addWidget(fault_f)
        
        # HMI
        lyt.addWidget(QLabel("<b>HMI COMMANDS</b>"))
        hmi_f = QFrame()
        hml = QVBoxLayout(hmi_f)
        
        r, c_idx = 0, 0
        row_lyt = QHBoxLayout()
        for name in HMI_COMMANDS:
            b = QPushButton(name)
            b.clicked.connect(lambda _, n=name: self._send_hmi(n))
            row_lyt.addWidget(b)
            c_idx += 1
            if c_idx >= 3:
                hml.addLayout(row_lyt)
                row_lyt = QHBoxLayout()
                c_idx = 0
        if c_idx > 0:
            hml.addLayout(row_lyt)
        lyt.addWidget(hmi_f)

    def _build_log_tab(self):
        lyt = QVBoxLayout(self.tab_log)
        self.log_text = QTextEdit()
        self.log_text.setReadOnly(True)
        self.log_text.setStyleSheet(f"background: {T['log_bg']}; color: {T['log_fg']}; font-family: monospace;")
        lyt.addWidget(self.log_text)
        
        controls = QHBoxLayout()
        self.chk_autoscroll = QCheckBox("Auto-scroll")
        self.chk_autoscroll.setChecked(True)
        controls.addWidget(self.chk_autoscroll)
        
        btn_clr = QPushButton("Clear")
        btn_clr.clicked.connect(self.log_text.clear)
        controls.addWidget(btn_clr)
        
        self.chk_show_sim = QCheckBox("Show sim frames")
        self.chk_show_sim.setChecked(True)
        controls.addWidget(self.chk_show_sim)
        controls.addStretch()
        lyt.addLayout(controls)

    def _on_zone_click(self, group: str):
        # clear previous buttons
        while self.btn_layout.count():
            child = self.btn_layout.takeAt(0)
            if child.widget():
                child.widget().deleteLater()
                
        buttons = {k: v for k, v in SW_BUTTONS.items() if v[2] == group}
        self.zone_title.setText(group.upper() + " CONTROLS")
        self.zone_title.setStyleSheet(f"color: {GROUP_COLORS.get(group, 'black')}; font-weight: bold;")
        self.wheel.highlight_group(group)
        
        for label, (arb_id, btn_id, grp, desc) in buttons.items():
            b = QPushButton(label)
            color = GROUP_COLORS.get(group, T["fg2"])
            b.setStyleSheet(f"background-color: {color}; color: white; padding: 5px;")
            b.clicked.connect(lambda _, l=label, aid=arb_id, bid=btn_id, d=desc: self._send_sw(l, aid, bid, d))
            self.btn_layout.addWidget(b)

    def _send_sw(self, label, arb_id, btn_id, desc):
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        ok, msg = self.bus.send(arb_id, bytes([btn_id, 0x01]))
        self.c.log_signal.emit(ts, msg, "sw")

        def _release():
            time.sleep(0.08)
            _, rmsg = self.bus.send(arb_id, bytes([btn_id, 0x00]))
            self.c.log_signal.emit(datetime.now().strftime("%H:%M:%S.%f")[:-3], rmsg, "sw")
            
        threading.Thread(target=_release, daemon=True).start()
        self.c.log_signal.emit(ts, f"SW  [{label}]  {desc}", "sw")
        self.lbl_status.setText(f"{label}  ·  {desc}")

    def _send_hmi(self, name):
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        cmd_id, p1, p2, desc = HMI_COMMANDS[name]
        ok, msg = self.bus.send(cmd_id, bytes([p1, p2, 0x00]))
        self.c.log_signal.emit(ts, msg, "hmi")
        self.c.log_signal.emit(ts, f"HMI [{name}]  {desc}", "hmi")
        self.lbl_status.setText(f"HMI  ·  {name}  ·  {desc}")

    def _update_gear(self, idx): self.sim.gear = idx
    def _update_pedal(self, val): self.sim.pedal = float(val)
    def _update_hvac(self):
        self.sim.hvac_on = self.chk_ac.isChecked()
        self.sim.fan_speed = self.spin_fan.value()
        self.sim.target_temp = self.spin_temp.value()
    def _update_seat_heat(self): self.sim.seat_heaters = 1 if self.chk_seat.isChecked() else 0
    def _update_doors(self):
        mask = 0
        for i, cb in enumerate(self.chk_doors):
            if cb.isChecked(): mask |= (1 << i)
        self.sim.doors = mask
    def _on_fault_select(self):
        self.sim.fault_code = self.faults.get(self.fault_cb.currentText(), 0)

    def _log_raw(self, ts: str, msg: str, tag: str) -> None:
        if tag == "sim" and not self.chk_show_sim.isChecked():
            return
            
        color = T["log_fg"]
        if tag == "err": color = T["log_err"]
        elif tag == "sim": color = T["log_sim"]
        elif tag == "sw": color = T["log_sw"]
        elif tag == "hmi": color = T["log_hmi"]
        
        fmt_msg = f"<span style='color: {color};'>[{ts}] {msg}</span><br>"
        self.log_text.moveCursor(QTextCursor.MoveOperation.End)
        self.log_text.insertHtml(fmt_msg)
        
        if self.chk_autoscroll.isChecked():
            sb = self.log_text.verticalScrollBar()
            sb.setValue(sb.maximum())

    def closeEvent(self, event):
        self.sim.stop()
        self.bus.close()
        event.accept()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    win = App()
    win.show()
    sys.exit(app.exec())
