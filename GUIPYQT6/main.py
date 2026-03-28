import queue
import re
import socket
import sys
import time
from collections import deque
from datetime import datetime

from PyQt6.QtCore import QThread, pyqtSignal, QTimer, Qt
from PyQt6.QtWidgets import (
    QApplication,
    QComboBox,
    QFormLayout,
    QGridLayout,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QMainWindow,
    QMessageBox,
    QPushButton,
    QPlainTextEdit,
    QTabWidget,
    QVBoxLayout,
    QWidget,
)

import serial
import serial.tools.list_ports
import pyqtgraph as pg  # type: ignore[reportMissingImports]

try:
    import bluetooth as pybluez  # type: ignore[reportMissingImports]
except Exception:
    pybluez = None


TELEMETRY_RE = re.compile(r"RAW=(\d+)\s+mV=(\d+)\s+LUX=(\d+)|Temp:\s*([\d.]+)\s*C|Pres:\s*([\d.]+)\s*Pa", re.IGNORECASE)


class ChartWindow(QMainWindow):
    """Separate window for displaying individual chart"""
    def __init__(self, chart_type: str, parent=None):
        super().__init__(parent)
        self.chart_type = chart_type
        self.parent_window = parent
        self._chart_max_points = 240
        self._chart_view_points = 80
        self._chart_x = deque(maxlen=self._chart_max_points)
        self._chart_data = deque(maxlen=self._chart_max_points)
        self._sample_idx = 0
        
        self._setup_ui()
        self._setup_auto_update()
    
    def _setup_auto_update(self):
        """Setup timer to auto-refresh data from parent"""
        self.update_timer = QTimer()
        self.update_timer.timeout.connect(self._auto_update)
        self.update_timer.start(100)  # Update every 100ms
    
    def _auto_update(self):
        """Automatically fetch latest data from parent window"""
        if self.parent_window is None:
            return
        
        x = list(self.parent_window._chart_x)
        if not x:
            return
        
        if self.chart_type == "temp":
            data = list(self.parent_window._chart_temp)
        elif self.chart_type == "pres":
            data = list(self.parent_window._chart_pres)
        elif self.chart_type == "lux":
            data = list(self.parent_window._chart_lux)
        elif self.chart_type == "uv":
            data = list(self.parent_window._chart_mv)
        else:
            return
        
        if len(data) > 2:  # Only update if we have more than 2 points
            self.update_data(x, data)
    
    def _setup_ui(self):
        central = QWidget()
        layout = QVBoxLayout(central)
        
        # Chart titles and colors
        chart_config = {
            "temp": {
                "title": "🌡️ Temperature Chart",
                "ylabel": "Temperature (°C)",
                "color": "#FF0000"
            },
            "pres": {
                "title": "⊙ Pressure Chart",
                "ylabel": "Pressure (Pa / 100)",
                "color": "#00FFFF"
            },
            "lux": {
                "title": "☀️ Light (LUX) Chart",
                "ylabel": "Luminosity (Lux)",
                "color": "#FFAA00"
            },
            "uv": {
                "title": "☢️ UV (ADC) Chart",
                "ylabel": "ADC Voltage (mV)",
                "color": "#00FF7F"
            }
        }
        
        config = chart_config.get(self.chart_type, chart_config["temp"])
        
        self.setWindowTitle(config["title"])
        self.resize(900, 600)
        
        self.plot = pg.PlotWidget()
        self.plot.setBackground("#111111")
        self.plot.showGrid(x=True, y=True, alpha=0.25)
        self.plot.setLabel("left", config["ylabel"])
        self.plot.setLabel("bottom", "Sample")
        self.plot.disableAutoRange(axis="x")
        
        self.curve = self.plot.plot(
            [], [], pen=pg.mkPen(color=config["color"], width=2), name=config["title"]
        )
        
        layout.addWidget(self.plot)
        self.setCentralWidget(central)
    
    def update_data(self, x_data: list, y_data: list):
        """Update chart with new data"""
        if not x_data or len(x_data) < 2:
            return
        
        self.curve.setData(x_data, y_data)
        
        last_x = x_data[-1]
        first_x = max(0, last_x - self._chart_view_points + 1)
        self.plot.setXRange(first_x, max(self._chart_view_points, last_x + 1), padding=0)
        
        if y_data:
            max_y = max(y_data)
            min_y = min(y_data) if min(y_data) >= 0 else 0
            
            # Add some padding to Y axis
            if max_y > min_y:
                y_range = max_y - min_y
                self.plot.setYRange(min_y - y_range * 0.1, max_y + y_range * 0.1)
            else:
                self.plot.setYRange(0, max(max_y * 1.2, 10))
    
    def closeEvent(self, event):
        """Stop timer when window closes"""
        if hasattr(self, 'update_timer'):
            self.update_timer.stop()
        super().closeEvent(event)


class LedControlWindow(QMainWindow):
    """Separate window for controlling LED pins"""
    def __init__(self, parent=None):
        super().__init__(parent)
        self.parent_window = parent
        self._buttons: dict[str, QPushButton] = {}
        self._setup_ui()

    def _setup_ui(self):
        self.setWindowTitle("LED Control")
        self.resize(420, 220)

        central = QWidget()
        grid = QGridLayout(central)

        pins = ["PB13", "PB14", "PB15", "PC13"]
        for idx, pin in enumerate(pins):
            btn = QPushButton(f"{pin}: OFF")
            btn.setMinimumHeight(48)
            btn.clicked.connect(lambda _checked=False, p=pin: self._on_pin_clicked(p))
            self._buttons[pin] = btn
            grid.addWidget(btn, idx // 2, idx % 2)

        self.setCentralWidget(central)

    def _on_pin_clicked(self, pin: str):
        if self.parent_window is not None:
            self.parent_window._toggle_led_pin(pin)

    def set_pin_state(self, pin: str, is_on: bool):
        btn = self._buttons.get(pin)
        if btn is None:
            return
        btn.setText(f"{pin}: {'ON' if is_on else 'OFF'}")
        if is_on:
            btn.setStyleSheet("background-color: #1E7A1E; color: white; font-weight: bold;")
        else:
            btn.setStyleSheet("")


class SerialWorker(QThread):
    connected = pyqtSignal(str)
    disconnected = pyqtSignal()
    line_received = pyqtSignal(str)
    error = pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self._running = True
        self._port = ""
        self._baud = 9600
        self._tx_queue: queue.Queue[str] = queue.Queue()

    def configure(self, port: str, baud: int):
        self._port = port
        self._baud = baud

    def send_line(self, text: str):
        self._tx_queue.put(text)

    def stop(self):
        self._running = False

    def run(self):
        if not self._port:
            self.error.emit("No COM port selected")
            return

        ser = None
        try:
            ser = serial.Serial(self._port, self._baud, timeout=0.1)
            self.connected.emit(f"{self._port} @ {self._baud}")

            while self._running:
                try:
                    data = ser.readline()
                    if data:
                        line = data.decode(errors="ignore").strip()
                        if line:
                            self.line_received.emit(line)

                    while True:
                        outgoing = self._tx_queue.get_nowait()
                        ser.write((outgoing + "\r\n").encode())
                except queue.Empty:
                    pass
                except Exception as ex:
                    self.error.emit(f"Serial runtime error: {ex}")
                    break

                self.msleep(10)

        except Exception as ex:
            self.error.emit(f"Serial open failed: {ex}")
        finally:
            if ser is not None and ser.is_open:
                ser.close()
            self.disconnected.emit()


class BluetoothRfcommWorker(QThread):
    connected = pyqtSignal(str)
    disconnected = pyqtSignal()
    line_received = pyqtSignal(str)
    error = pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self._running = True
        self._address = ""
        self._channel = 1
        self._tx_queue: queue.Queue[str] = queue.Queue()

    def configure(self, address: str, channel: int):
        self._address = address
        self._channel = channel

    def send_line(self, text: str):
        self._tx_queue.put(text)

    def stop(self):
        self._running = False

    def run(self):
        if pybluez is None:
            self.error.emit("Wireless Bluetooth requires pybluez. Install: pip install pybluez2")
            self.disconnected.emit()
            return

        if not self._address:
            self.error.emit("No Bluetooth device selected")
            self.disconnected.emit()
            return

        sock = None
        try:
            sock = pybluez.BluetoothSocket(pybluez.RFCOMM)
            sock.settimeout(2.0)
            sock.connect((self._address, self._channel))
            sock.settimeout(0.1)
            self.connected.emit(f"{self._address} ch{self._channel}")

            rx_buf = ""
            while self._running:
                try:
                    data = sock.recv(512)
                    if data:
                        rx_buf += data.decode(errors="ignore")
                        while "\n" in rx_buf:
                            line, rx_buf = rx_buf.split("\n", 1)
                            line = line.strip("\r\n ")
                            if line:
                                self.line_received.emit(line)
                except Exception as ex:
                    ex_name = ex.__class__.__name__.lower()
                    if "timeout" not in ex_name:
                        self.error.emit(f"Bluetooth runtime error: {ex}")
                        break

                try:
                    while True:
                        outgoing = self._tx_queue.get_nowait()
                        sock.send((outgoing + "\r\n").encode())
                except queue.Empty:
                    pass
                except Exception as ex:
                    self.error.emit(f"Bluetooth send error: {ex}")
                    break

                self.msleep(10)

        except Exception as ex:
            self.error.emit(f"Bluetooth connect failed: {ex}")
        finally:
            if sock is not None:
                try:
                    sock.close()
                except Exception:
                    pass
            self.disconnected.emit()


class TcpWorker(QThread):
    connected = pyqtSignal(str)
    disconnected = pyqtSignal()
    line_received = pyqtSignal(str)
    error = pyqtSignal(str)

    def __init__(self):
        super().__init__()
        self._running = True
        self._host = ""
        self._port = 0
        self._tx_queue: queue.Queue[str] = queue.Queue()

    def configure(self, host: str, port: int):
        self._host = host
        self._port = port

    def send_line(self, text: str):
        self._tx_queue.put(text)

    def stop(self):
        self._running = False

    def run(self):
        if not self._host or self._port <= 0:
            self.error.emit("Invalid TCP host/port")
            return

        sock = None
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(2.0)
            sock.connect((self._host, self._port))
            sock.settimeout(0.1)
            self.connected.emit(f"{self._host}:{self._port}")

            rx_buf = ""
            while self._running:
                try:
                    chunk = sock.recv(512)
                    if chunk:
                        rx_buf += chunk.decode(errors="ignore")
                        while "\n" in rx_buf:
                            line, rx_buf = rx_buf.split("\n", 1)
                            line = line.strip("\r\n ")
                            if line:
                                self.line_received.emit(line)
                except socket.timeout:
                    pass
                except BlockingIOError:
                    pass
                except Exception as ex:
                    self.error.emit(f"TCP runtime error: {ex}")
                    break

                try:
                    while True:
                        outgoing = self._tx_queue.get_nowait()
                        sock.sendall((outgoing + "\r\n").encode())
                except queue.Empty:
                    pass
                except Exception as ex:
                    self.error.emit(f"TCP send error: {ex}")
                    break

                self.msleep(10)

        except Exception as ex:
            self.error.emit(f"TCP connect failed: {ex}")
        finally:
            if sock is not None:
                try:
                    sock.close()
                except Exception:
                    pass
            self.disconnected.emit()


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("STM32 Weather Station GUI - PyQt6")
        self.resize(1050, 680)
        self._title_colors = [
            "#00F0FF", "#00FF9C", "#F6FF00", "#FF8A00", "#FF2DD1", "#A566FF"
        ]
        self._title_color_index = 0

        self.serial_worker: SerialWorker | None = None
        self.rfcomm_worker: BluetoothRfcommWorker | None = None
        self.tcp_worker: TcpWorker | None = None
        
        self._chart_max_points = 240
        self._chart_windows: dict[str, ChartWindow] = {}  # Keep track of open chart windows
        self._led_control_window: LedControlWindow | None = None
        self._led_states: dict[str, bool] = {
            "PB13": False,
            "PB14": False,
            "PB15": False,
            "PC13": False,
        }
        
        # Cache for current sensor values (use to avoid duplicate chart entries)
        self._last_raw = 0
        self._last_mv = 0
        self._last_lux = 0
        self._last_temp = 0.0
        self._last_pres = 0.0
        self._last_added_sample = -1  # Track when last sample was added to chart
        self._chart_view_points = 80
        self._chart_x = deque(maxlen=self._chart_max_points)
        self._chart_raw = deque(maxlen=self._chart_max_points)
        self._chart_mv = deque(maxlen=self._chart_max_points)
        self._chart_lux = deque(maxlen=self._chart_max_points)
        self._chart_temp = deque(maxlen=self._chart_max_points)
        self._chart_pres = deque(maxlen=self._chart_max_points)
        self._sample_idx = 0

        self._build_ui()
        self._apply_professional_theme()
        self._setup_visual_effects()
        self._wire_events()
        self.refresh_ports()

    def _build_ui(self):
        root = QWidget()
        root_layout = QVBoxLayout(root)

        # Hero banner
        self.lbl_hero_title = QLabel("⚡ WEATHER_STATION ⚡")
        self.lbl_hero_title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.lbl_hero_subtitle = QLabel("Live Telemetry • Smart Control • Professional Dashboard")
        self.lbl_hero_subtitle.setAlignment(Qt.AlignmentFlag.AlignCenter)
        root_layout.addWidget(self.lbl_hero_title)
        root_layout.addWidget(self.lbl_hero_subtitle)

        sensor_group = QGroupBox("Live Sensor Data")
        sensor_grid = QGridLayout(sensor_group)

        self.lbl_raw = QLabel("--")
        self.lbl_mv = QLabel("--")
        self.lbl_lux = QLabel("--")
        self.lbl_temp = QLabel("--")
        self.lbl_pres = QLabel("--")
        self.lbl_led = QLabel("UNKNOWN")
        self.lbl_last = QLabel("--")

        sensor_grid.addWidget(QLabel("ADC RAW"), 0, 0)
        sensor_grid.addWidget(self.lbl_raw, 0, 1)
        sensor_grid.addWidget(QLabel("Voltage (mV)"), 0, 2)
        sensor_grid.addWidget(self.lbl_mv, 0, 3)
        sensor_grid.addWidget(QLabel("Lux"), 1, 0)
        sensor_grid.addWidget(self.lbl_lux, 1, 1)
        sensor_grid.addWidget(QLabel("Temperature (C)"), 1, 2)
        sensor_grid.addWidget(self.lbl_temp, 1, 3)
        sensor_grid.addWidget(QLabel("Pressure (Pa)"), 2, 0)
        sensor_grid.addWidget(self.lbl_pres, 2, 1)
        sensor_grid.addWidget(QLabel("PB15 State"), 2, 2)
        sensor_grid.addWidget(self.lbl_led, 2, 3)
        sensor_grid.addWidget(QLabel("Last Update"), 3, 0)
        sensor_grid.addWidget(self.lbl_last, 3, 1, 1, 3)

        root_layout.addWidget(sensor_group)

        # Chart selection buttons
        chart_btn_group = QGroupBox("Open Chart Windows")
        chart_btn_layout = QHBoxLayout(chart_btn_group)
        
        self.btn_chart_temp = QPushButton("🌡️ Temperature")
        self.btn_chart_pres = QPushButton("⊙ Pressure")
        self.btn_chart_lux = QPushButton("☀️ Light (LUX)")
        self.btn_chart_uv = QPushButton("☢️ UV (ADC)")
        self.btn_led_control = QPushButton("💡 LED Control")
        
        chart_btn_layout.addWidget(self.btn_chart_temp)
        chart_btn_layout.addWidget(self.btn_chart_pres)
        chart_btn_layout.addWidget(self.btn_chart_lux)
        chart_btn_layout.addWidget(self.btn_chart_uv)
        chart_btn_layout.addWidget(self.btn_led_control)
        
        root_layout.addWidget(chart_btn_group)

        tabs = QTabWidget()
        tabs.addTab(self._build_bt_tab(), "Bluetooth (HC-06)")

        root_layout.addWidget(tabs)

        self.setCentralWidget(root)
        self.statusBar().showMessage("Ready")

    def _apply_professional_theme(self):
        self.setStyleSheet(
            """
            QMainWindow {
                background-color: #0A0F1E;
            }
            QWidget {
                color: #E8F1FF;
                font-size: 12px;
            }
            QLabel {
                color: #D5E4FF;
            }
            QLabel#ValueLabel {
                font-size: 22px;
                font-weight: 700;
                color: #00E8FF;
                background-color: #101A33;
                border: 1px solid #294A91;
                border-radius: 8px;
                padding: 6px 10px;
            }
            QGroupBox {
                font-size: 15px;
                font-weight: 700;
                border: 2px solid #2D4F93;
                border-radius: 12px;
                margin-top: 16px;
                padding-top: 10px;
                background-color: #101A33;
            }
            QGroupBox::title {
                subcontrol-origin: margin;
                left: 12px;
                padding: 0 6px;
                color: #74DAFF;
            }
            QPushButton {
                background-color: #153067;
                border: 1px solid #2E60C4;
                border-radius: 10px;
                padding: 8px 14px;
                color: #E8F1FF;
                font-weight: 700;
            }
            QPushButton:hover {
                background-color: #1A3B7E;
                border: 1px solid #52A5FF;
            }
            QPushButton:pressed {
                background-color: #123164;
            }
            QLineEdit, QComboBox, QPlainTextEdit {
                background-color: #0C152B;
                border: 1px solid #2A4C92;
                border-radius: 8px;
                padding: 6px;
                color: #E8F1FF;
            }
            QTabWidget::pane {
                border: 1px solid #26457F;
                background: #0E1730;
                border-radius: 10px;
            }
            QTabBar::tab {
                background: #132A57;
                color: #CFE3FF;
                padding: 10px 14px;
                margin-right: 4px;
                border-top-left-radius: 8px;
                border-top-right-radius: 8px;
            }
            QTabBar::tab:selected {
                background: #1D4186;
                color: #FFFFFF;
            }
            QStatusBar {
                background-color: #0F1A34;
                color: #7DE8FF;
                border-top: 1px solid #2A4B8B;
            }
            """
        )

        # Highlight numeric labels
        self.lbl_raw.setObjectName("ValueLabel")
        self.lbl_mv.setObjectName("ValueLabel")
        self.lbl_lux.setObjectName("ValueLabel")
        self.lbl_temp.setObjectName("ValueLabel")
        self.lbl_pres.setObjectName("ValueLabel")
        self.lbl_led.setObjectName("ValueLabel")

    def _setup_visual_effects(self):
        self.lbl_hero_title.setStyleSheet("font-size: 36px; font-weight: 900; letter-spacing: 2px;")
        self.lbl_hero_subtitle.setStyleSheet("font-size: 14px; color: #9CC7FF; font-weight: 600;")

        self._title_blink_timer = QTimer(self)
        self._title_blink_timer.timeout.connect(self._animate_hero_title)
        self._title_blink_timer.start(180)

    def _animate_hero_title(self):
        color = self._title_colors[self._title_color_index]
        self.lbl_hero_title.setStyleSheet(
            f"font-size: 36px; font-weight: 900; letter-spacing: 2px; color: {color};"
        )
        self._title_color_index = (self._title_color_index + 1) % len(self._title_colors)

    def _build_bt_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)

        conn_group = QGroupBox("Bluetooth Connection")
        conn_row = QHBoxLayout(conn_group)

        self.cmb_bt_mode = QComboBox()
        self.cmb_bt_mode.addItems(["Serial COM", "Wireless RFCOMM"])

        self.cmb_ports = QComboBox()
        self.btn_refresh_ports = QPushButton("Refresh Ports")

        self.cmb_bt_devices = QComboBox()
        self.btn_scan_bt_devices = QPushButton("Scan HC-06")
        self.cmb_bt_channel = QComboBox()
        self.cmb_bt_channel.addItems(["1", "2", "3", "4", "5"])
        self.cmb_bt_channel.setCurrentText("1")

        self.cmb_baud = QComboBox()
        self.cmb_baud.addItems(["9600", "19200", "38400", "57600", "115200"])
        self.cmb_baud.setCurrentText("9600")

        self.btn_bt_connect = QPushButton("Connect")
        self.btn_bt_disconnect = QPushButton("Disconnect")
        self.btn_bt_disconnect.setEnabled(False)

        conn_row.addWidget(QLabel("Mode"))
        conn_row.addWidget(self.cmb_bt_mode)
        conn_row.addWidget(QLabel("COM"))
        conn_row.addWidget(self.cmb_ports, 2)
        conn_row.addWidget(self.btn_refresh_ports)
        conn_row.addWidget(QLabel("Baud"))
        conn_row.addWidget(self.cmb_baud)
        conn_row.addWidget(QLabel("Device"))
        conn_row.addWidget(self.cmb_bt_devices, 2)
        conn_row.addWidget(self.btn_scan_bt_devices)
        conn_row.addWidget(QLabel("Ch"))
        conn_row.addWidget(self.cmb_bt_channel)
        conn_row.addWidget(self.btn_bt_connect)
        conn_row.addWidget(self.btn_bt_disconnect)

        cmd_group = QGroupBox("Command")
        cmd_layout = QHBoxLayout(cmd_group)
        self.edt_bt_cmd = QLineEdit()
        self.edt_bt_cmd.setPlaceholderText("Type command, e.g. 1, 2, READ, HELP")
        self.btn_bt_send = QPushButton("Send")
        self.btn_led_on = QPushButton("PB15 ON (1)")
        self.btn_led_off = QPushButton("PB15 OFF (2)")

        cmd_layout.addWidget(self.edt_bt_cmd, 1)
        cmd_layout.addWidget(self.btn_bt_send)
        cmd_layout.addWidget(self.btn_led_on)
        cmd_layout.addWidget(self.btn_led_off)

        self.log_bt = QPlainTextEdit()
        self.log_bt.setReadOnly(True)

        layout.addWidget(conn_group)
        layout.addWidget(cmd_group)
        layout.addWidget(self.log_bt, 1)

        self._on_bt_mode_changed(self.cmb_bt_mode.currentText())

        return tab

    def _build_wifi_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)

        conn_group = QGroupBox("WiFi TCP")
        form = QFormLayout(conn_group)
        self.edt_host = QLineEdit("192.168.4.1")
        self.edt_port = QLineEdit("5000")

        row = QHBoxLayout()
        self.btn_wifi_connect = QPushButton("Connect")
        self.btn_wifi_disconnect = QPushButton("Disconnect")
        self.btn_wifi_disconnect.setEnabled(False)
        row.addWidget(self.btn_wifi_connect)
        row.addWidget(self.btn_wifi_disconnect)

        row_wrap = QWidget()
        row_wrap.setLayout(row)

        form.addRow("Host", self.edt_host)
        form.addRow("Port", self.edt_port)
        form.addRow("", row_wrap)

        cmd_group = QGroupBox("WiFi Command")
        cmd_layout = QHBoxLayout(cmd_group)
        self.edt_wifi_cmd = QLineEdit()
        self.edt_wifi_cmd.setPlaceholderText("Type command for WiFi endpoint")
        self.btn_wifi_send = QPushButton("Send")
        cmd_layout.addWidget(self.edt_wifi_cmd, 1)
        cmd_layout.addWidget(self.btn_wifi_send)

        self.log_wifi = QPlainTextEdit()
        self.log_wifi.setReadOnly(True)

        layout.addWidget(conn_group)
        layout.addWidget(cmd_group)
        layout.addWidget(self.log_wifi, 1)

        return tab

    def _wire_events(self):
        self.btn_refresh_ports.clicked.connect(self.refresh_ports)
        self.btn_scan_bt_devices.clicked.connect(self.scan_bt_devices)
        self.cmb_bt_mode.currentTextChanged.connect(self._on_bt_mode_changed)
        self.btn_bt_connect.clicked.connect(self.connect_bt)
        self.btn_bt_disconnect.clicked.connect(self.disconnect_bt)
        self.btn_bt_send.clicked.connect(self.send_bt)
        self.edt_bt_cmd.returnPressed.connect(self.send_bt)
        self.btn_led_on.clicked.connect(lambda: self.send_bt_raw("1"))
        self.btn_led_off.clicked.connect(lambda: self.send_bt_raw("2"))
        
        # Chart button connections - open separate windows
        self.btn_chart_temp.clicked.connect(lambda: self._open_chart("temp"))
        self.btn_chart_pres.clicked.connect(lambda: self._open_chart("pres"))
        self.btn_chart_lux.clicked.connect(lambda: self._open_chart("lux"))
        self.btn_chart_uv.clicked.connect(lambda: self._open_chart("uv"))
        self.btn_led_control.clicked.connect(self._open_led_control)

    def refresh_ports(self):
        current = self.cmb_ports.currentText()
        self.cmb_ports.clear()

        ports = sorted(serial.tools.list_ports.comports(), key=lambda p: p.device)
        for p in ports:
            self.cmb_ports.addItem(f"{p.device} - {p.description}", p.device)

        if self.cmb_ports.count() == 0:
            self.cmb_ports.addItem("No COM ports", "")

        if current:
            idx = self.cmb_ports.findText(current)
            if idx >= 0:
                self.cmb_ports.setCurrentIndex(idx)

    def _on_bt_mode_changed(self, mode_text: str):
        serial_mode = (mode_text == "Serial COM")
        self.cmb_ports.setEnabled(serial_mode)
        self.btn_refresh_ports.setEnabled(serial_mode)
        self.cmb_baud.setEnabled(serial_mode or (mode_text == "Wireless RFCOMM" and pybluez is None))

        self.cmb_bt_devices.setEnabled(not serial_mode)
        self.btn_scan_bt_devices.setEnabled(not serial_mode)
        self.cmb_bt_channel.setEnabled(not serial_mode)

    def scan_bt_devices(self):
        self.cmb_bt_devices.clear()

        if pybluez is None:
            # Fallback: discover paired Bluetooth serial COM ports (Windows)
            ports = sorted(serial.tools.list_ports.comports(), key=lambda p: p.device)
            hc06_ports = []
            for p in ports:
                desc = (p.description or "")
                hwid = (p.hwid or "")
                text = f"{desc} {hwid}".upper()
                if "BLUETOOTH" in text or "HC-06" in text or "HC06" in text or "LINVOR" in text:
                    hc06_ports.append(p)

            for p in hc06_ports:
                self.cmb_bt_devices.addItem(f"{p.device} - {p.description}", f"COM:{p.device}")

            if self.cmb_bt_devices.count() == 0:
                self.cmb_bt_devices.addItem("No HC-06 Bluetooth COM found", "")

            QMessageBox.information(
                self,
                "Bluetooth",
                "pybluez chưa có trên Python hiện tại. Đang dùng chế độ fallback qua Bluetooth COM đã pair sẵn.",
            )
            self.statusBar().showMessage("Scan complete (COM fallback mode)")
            return

        self.statusBar().showMessage("Scanning Bluetooth devices...")
        QApplication.setOverrideCursor(Qt.CursorShape.WaitCursor)
        try:
            devices = pybluez.discover_devices(duration=6, lookup_names=True)
        except Exception as ex:
            devices = []
            self._append_log(self.log_bt, "BT", f"Scan error: {ex}")
        finally:
            QApplication.restoreOverrideCursor()

        hc06_devices = []
        for addr, name in devices:
            name_upper = (name or "").upper()
            if "HC-06" in name_upper or "LINVOR" in name_upper or "HC06" in name_upper:
                hc06_devices.append((addr, name or "Unknown"))

        if not hc06_devices:
            for addr, name in devices:
                self.cmb_bt_devices.addItem(f"{name} [{addr}]", addr)
            if self.cmb_bt_devices.count() == 0:
                self.cmb_bt_devices.addItem("No Bluetooth devices found", "")
            self.statusBar().showMessage("Scan complete (HC-06 not auto-detected)")
            return

        for addr, name in hc06_devices:
            self.cmb_bt_devices.addItem(f"{name} [{addr}]", addr)

        self.statusBar().showMessage(f"Found {len(hc06_devices)} HC-06 device(s)")

    def _get_active_bt_worker(self):
        if self.serial_worker is not None:
            return self.serial_worker
        if self.rfcomm_worker is not None:
            return self.rfcomm_worker
        return None

    def connect_bt(self):
        if self._get_active_bt_worker() is not None:
            return

        mode_text = self.cmb_bt_mode.currentText()
        if mode_text == "Serial COM":
            port = self.cmb_ports.currentData()
            if not port:
                QMessageBox.warning(self, "Bluetooth", "Please select a valid COM port")
                return

            baud = int(self.cmb_baud.currentText())
            worker = SerialWorker()
            worker.configure(port, baud)
            worker.connected.connect(lambda x: self._on_bt_connected(x))
            worker.disconnected.connect(self._on_bt_disconnected)
            worker.line_received.connect(lambda line: self._on_line("BT", line))
            worker.error.connect(lambda msg: self._append_log(self.log_bt, "BT", f"ERROR: {msg}"))
            self.serial_worker = worker
            worker.start()
        else:
            target = self.cmb_bt_devices.currentData()
            if not target:
                QMessageBox.warning(self, "Bluetooth", "Please scan and select HC-06 device/port")
                return

            # If pybluez is unavailable, use COM fallback even in wireless mode.
            if isinstance(target, str) and target.startswith("COM:"):
                port = target.split(":", 1)[1]
                baud = int(self.cmb_baud.currentText())
                worker = SerialWorker()
                worker.configure(port, baud)
                worker.connected.connect(lambda x: self._on_bt_connected(f"Wireless(COM) {x}"))
                worker.disconnected.connect(self._on_bt_disconnected)
                worker.line_received.connect(lambda line: self._on_line("BT", line))
                worker.error.connect(lambda msg: self._append_log(self.log_bt, "BT", f"ERROR: {msg}"))
                self.serial_worker = worker
                worker.start()
            else:
                channel = int(self.cmb_bt_channel.currentText())
                worker = BluetoothRfcommWorker()
                worker.configure(str(target), channel)
                worker.connected.connect(lambda x: self._on_bt_connected(f"Wireless {x}"))
                worker.disconnected.connect(self._on_bt_disconnected)
                worker.line_received.connect(lambda line: self._on_line("BT", line))
                worker.error.connect(lambda msg: self._append_log(self.log_bt, "BT", f"ERROR: {msg}"))
                self.rfcomm_worker = worker
                worker.start()

        self.btn_bt_connect.setEnabled(False)
        self.btn_bt_disconnect.setEnabled(True)

    def disconnect_bt(self):
        worker = self._get_active_bt_worker()
        if worker is None:
            return

        worker.stop()
        worker.wait(800)
        self.serial_worker = None
        self.rfcomm_worker = None

        self.btn_bt_connect.setEnabled(True)
        self.btn_bt_disconnect.setEnabled(False)
        self.statusBar().showMessage("Bluetooth disconnected")

    def send_bt(self):
        text = self.edt_bt_cmd.text().strip()
        if not text:
            return
        self.send_bt_raw(text)
        self.edt_bt_cmd.clear()

    def send_bt_raw(self, text: str):
        worker = self._get_active_bt_worker()
        if worker is None:
            QMessageBox.information(self, "Bluetooth", "Bluetooth is not connected")
            return
        worker.send_line(text)
        self._append_log(self.log_bt, "BT-TX", text)

    def connect_wifi(self):
        if self.tcp_worker is not None:
            return

        host = self.edt_host.text().strip()
        port_text = self.edt_port.text().strip()
        try:
            port = int(port_text)
        except ValueError:
            QMessageBox.warning(self, "WiFi", "Port must be a number")
            return

        worker = TcpWorker()
        worker.configure(host, port)
        worker.connected.connect(lambda x: self._on_wifi_connected(x))
        worker.disconnected.connect(self._on_wifi_disconnected)
        worker.line_received.connect(lambda line: self._on_line("WiFi", line))
        worker.error.connect(lambda msg: self._append_log(self.log_wifi, "WiFi", f"ERROR: {msg}"))

        self.tcp_worker = worker
        worker.start()

        self.btn_wifi_connect.setEnabled(False)
        self.btn_wifi_disconnect.setEnabled(True)

    def disconnect_wifi(self):
        if self.tcp_worker is None:
            return

        self.tcp_worker.stop()
        self.tcp_worker.wait(800)
        self.tcp_worker = None

        self.btn_wifi_connect.setEnabled(True)
        self.btn_wifi_disconnect.setEnabled(False)
        self.statusBar().showMessage("WiFi disconnected")

    def send_wifi(self):
        text = self.edt_wifi_cmd.text().strip()
        if not text:
            return

        if self.tcp_worker is None:
            QMessageBox.information(self, "WiFi", "WiFi is not connected")
            return

        self.tcp_worker.send_line(text)
        self._append_log(self.log_wifi, "WiFi-TX", text)
        self.edt_wifi_cmd.clear()

    def _on_bt_connected(self, text: str):
        self._append_log(self.log_bt, "BT", f"Connected: {text}")
        self.statusBar().showMessage(f"Bluetooth connected: {text}")

    def _on_bt_disconnected(self):
        self._append_log(self.log_bt, "BT", "Disconnected")
        self.serial_worker = None
        self.rfcomm_worker = None
        self.btn_bt_connect.setEnabled(True)
        self.btn_bt_disconnect.setEnabled(False)

    def _on_wifi_connected(self, text: str):
        self._append_log(self.log_wifi, "WiFi", f"Connected: {text}")
        self.statusBar().showMessage(f"WiFi connected: {text}")

    def _on_wifi_disconnected(self):
        self._append_log(self.log_wifi, "WiFi", "Disconnected")
        self.btn_wifi_connect.setEnabled(True)
        self.btn_wifi_disconnect.setEnabled(False)

    def _on_line(self, source: str, line: str):
        if source == "BT":
            self._append_log(self.log_bt, "BT-RX", line)
        else:
            self._append_log(self.log_wifi, "WiFi-RX", line)

        self._parse_telemetry(line)

        upper_line = line.upper()
        for pin in ("PB13", "PB14", "PB15", "PC13"):
            if f"{pin} ON" in upper_line:
                self._set_led_state(pin, True)
            elif f"{pin} OFF" in upper_line:
                self._set_led_state(pin, False)

    def _parse_telemetry(self, line: str):
        # Parse telemetry - data comes in separate lines, cache values and add to chart only once per cycle
        raw, mv, lux, temp, pres = None, None, None, None, None
        
        # Try to extract RAW/mV/LUX from line
        if "RAW=" in line and "mV=" in line and "LUX=" in line:
            try:
                parts = line.split()
                for part in parts:
                    if part.startswith("RAW="):
                        raw = part.split("=")[1]
                    elif part.startswith("mV="):
                        mv = part.split("=")[1]
                    elif part.startswith("LUX="):
                        lux = part.split("=")[1]
            except:
                pass
            # Update cache with new raw/mv/lux values
            if raw:
                self._last_raw = int(raw)
            if mv:
                self._last_mv = int(mv)
            if lux:
                self._last_lux = int(lux)
        
        # Try to extract Temperature
        if "Temp:" in line:
            try:
                temp_str = line.split("Temp:")[1].split("C")[0].strip()
                temp = float(temp_str)
                self._last_temp = temp
            except:
                pass
        
        # Try to extract Pressure
        if "Pres:" in line:
            try:
                pres_str = line.split("Pres:")[1].split("Pa")[0].strip()
                pres = float(pres_str)
                self._last_pres = pres
                # When we get Pres line, that's the end of a sensor cycle, add to chart
                self._add_sample_to_chart()
            except:
                pass
        
        # Update labels
        if raw:
            self.lbl_raw.setText(raw)
        if mv:
            self.lbl_mv.setText(mv)
        if lux:
            self.lbl_lux.setText(lux)
        if temp is not None:
            self.lbl_temp.setText(f"{temp:.1f}")
        if pres is not None:
            self.lbl_pres.setText(f"{pres:.1f}")
        
        self.lbl_last.setText(datetime.now().strftime("%Y-%m-%d %H:%M:%S"))
        
    def _add_sample_to_chart(self):
        """Add one sample to chart using cached values"""
        self._sample_idx += 1
        
        # Convert to scaled values
        pres_v = self._last_pres / 100  # Scale pressure for visibility
        
        self._chart_x.append(self._sample_idx)
        self._chart_raw.append(self._last_raw)
        self._chart_mv.append(self._last_mv)
        self._chart_lux.append(self._last_lux)
        self._chart_temp.append(self._last_temp)
        self._chart_pres.append(pres_v)
        
        x = list(self._chart_x)
        
        # Update open chart windows
        if "temp" in self._chart_windows:
            self._chart_windows["temp"].update_data(x, list(self._chart_temp))
        if "pres" in self._chart_windows:
            self._chart_windows["pres"].update_data(x, list(self._chart_pres))
        if "lux" in self._chart_windows:
            self._chart_windows["lux"].update_data(x, list(self._chart_lux))
        if "uv" in self._chart_windows:
            self._chart_windows["uv"].update_data(x, list(self._chart_mv))


    def _append_log(self, box: QPlainTextEdit, tag: str, msg: str):
        ts = time.strftime("%H:%M:%S")
        box.appendPlainText(f"[{ts}] {tag}: {msg}")
    
    def _open_chart(self, chart_type: str):
        """Open or focus on a chart window"""
        if chart_type in self._chart_windows:
            chart_win = self._chart_windows[chart_type]
            chart_win.show()
            chart_win.raise_()
            chart_win.activateWindow()
        else:
            # Create new window
            chart_win = ChartWindow(chart_type, self)
            self._chart_windows[chart_type] = chart_win
            
            # Show window
            chart_win.show()

    def _open_led_control(self):
        """Open or focus LED control window"""
        if self._led_control_window is None:
            self._led_control_window = LedControlWindow(self)

        self._led_control_window.show()
        self._led_control_window.raise_()
        self._led_control_window.activateWindow()

        # Sync current states to window
        for pin, is_on in self._led_states.items():
            self._led_control_window.set_pin_state(pin, is_on)

    def _toggle_led_pin(self, pin: str):
        """Send toggle command for a pin to MCU"""
        self.send_bt_raw(f"{pin}_TOGGLE")

    def _set_led_state(self, pin: str, is_on: bool):
        self._led_states[pin] = is_on

        if pin == "PB15":
            self.lbl_led.setText("ON" if is_on else "OFF")

        if self._led_control_window is not None:
            self._led_control_window.set_pin_state(pin, is_on)

    def closeEvent(self, event):
        try:
            self.disconnect_bt()
            self.disconnect_wifi()
            # Close all chart windows
            for chart_win in self._chart_windows.values():
                chart_win.close()
            if self._led_control_window is not None:
                self._led_control_window.close()
        finally:
            super().closeEvent(event)


def main():
    app = QApplication(sys.argv)
    w = MainWindow()
    w.showMaximized()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
