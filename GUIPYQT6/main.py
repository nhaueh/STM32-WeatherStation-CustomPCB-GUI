import queue
import re
import socket
import sys
import time
from collections import deque
from datetime import datetime

from PyQt6.QtCore import QThread, pyqtSignal
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


TELEMETRY_RE = re.compile(r"RAW=(\d+)\s+mV=(\d+)\s+LUX=(\d+)", re.IGNORECASE)


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

        self.serial_worker: SerialWorker | None = None
        self.tcp_worker: TcpWorker | None = None

        self._chart_max_points = 240
        self._chart_view_points = 80
        self._chart_x = deque(maxlen=self._chart_max_points)
        self._chart_raw = deque(maxlen=self._chart_max_points)
        self._chart_mv = deque(maxlen=self._chart_max_points)
        self._chart_lux = deque(maxlen=self._chart_max_points)
        self._sample_idx = 0

        self._build_ui()
        self._wire_events()
        self.refresh_ports()

    def _build_ui(self):
        root = QWidget()
        root_layout = QVBoxLayout(root)

        sensor_group = QGroupBox("Live Sensor Data")
        sensor_grid = QGridLayout(sensor_group)

        self.lbl_raw = QLabel("--")
        self.lbl_mv = QLabel("--")
        self.lbl_lux = QLabel("--")
        self.lbl_led = QLabel("UNKNOWN")
        self.lbl_last = QLabel("--")

        sensor_grid.addWidget(QLabel("ADC RAW"), 0, 0)
        sensor_grid.addWidget(self.lbl_raw, 0, 1)
        sensor_grid.addWidget(QLabel("Voltage (mV)"), 0, 2)
        sensor_grid.addWidget(self.lbl_mv, 0, 3)
        sensor_grid.addWidget(QLabel("Lux"), 1, 0)
        sensor_grid.addWidget(self.lbl_lux, 1, 1)
        sensor_grid.addWidget(QLabel("PB15 State"), 1, 2)
        sensor_grid.addWidget(self.lbl_led, 1, 3)
        sensor_grid.addWidget(QLabel("Last Update"), 2, 0)
        sensor_grid.addWidget(self.lbl_last, 2, 1, 1, 3)

        root_layout.addWidget(sensor_group)

        chart_group = QGroupBox("Realtime Sensor Chart (shared Y-axis)")
        chart_layout = QVBoxLayout(chart_group)

        self.plot = pg.PlotWidget()
        self.plot.setBackground("#111111")
        self.plot.showGrid(x=True, y=True, alpha=0.25)
        self.plot.addLegend()
        self.plot.setLabel("left", "Value")
        self.plot.setLabel("bottom", "Sample")
        self.plot.setYRange(0, 4095)
        self.plot.disableAutoRange(axis="x")

        self.curve_raw = self.plot.plot(
            [], [], pen=pg.mkPen(color="#00D1FF", width=2), name="RAW"
        )
        self.curve_mv = self.plot.plot(
            [], [], pen=pg.mkPen(color="#00FF7F", width=2), name="mV"
        )
        self.curve_lux = self.plot.plot(
            [], [], pen=pg.mkPen(color="#FFAA00", width=2), name="LUX"
        )

        chart_layout.addWidget(self.plot)
        root_layout.addWidget(chart_group)

        tabs = QTabWidget()
        tabs.addTab(self._build_bt_tab(), "Bluetooth (HC-06)")
        tabs.addTab(self._build_wifi_tab(), "WiFi (TCP)")

        root_layout.addWidget(tabs)

        self.setCentralWidget(root)
        self.statusBar().showMessage("Ready")

    def _build_bt_tab(self):
        tab = QWidget()
        layout = QVBoxLayout(tab)

        conn_group = QGroupBox("Bluetooth Serial")
        conn_row = QHBoxLayout(conn_group)

        self.cmb_ports = QComboBox()
        self.btn_refresh_ports = QPushButton("Refresh Ports")

        self.cmb_baud = QComboBox()
        self.cmb_baud.addItems(["9600", "19200", "38400", "57600", "115200"])
        self.cmb_baud.setCurrentText("9600")

        self.btn_bt_connect = QPushButton("Connect")
        self.btn_bt_disconnect = QPushButton("Disconnect")
        self.btn_bt_disconnect.setEnabled(False)

        conn_row.addWidget(QLabel("COM"))
        conn_row.addWidget(self.cmb_ports, 2)
        conn_row.addWidget(self.btn_refresh_ports)
        conn_row.addWidget(QLabel("Baud"))
        conn_row.addWidget(self.cmb_baud)
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
        self.btn_bt_connect.clicked.connect(self.connect_bt)
        self.btn_bt_disconnect.clicked.connect(self.disconnect_bt)
        self.btn_bt_send.clicked.connect(self.send_bt)
        self.edt_bt_cmd.returnPressed.connect(self.send_bt)
        self.btn_led_on.clicked.connect(lambda: self.send_bt_raw("1"))
        self.btn_led_off.clicked.connect(lambda: self.send_bt_raw("2"))

        self.btn_wifi_connect.clicked.connect(self.connect_wifi)
        self.btn_wifi_disconnect.clicked.connect(self.disconnect_wifi)
        self.btn_wifi_send.clicked.connect(self.send_wifi)
        self.edt_wifi_cmd.returnPressed.connect(self.send_wifi)

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

    def connect_bt(self):
        if self.serial_worker is not None:
            return

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

        self.btn_bt_connect.setEnabled(False)
        self.btn_bt_disconnect.setEnabled(True)

    def disconnect_bt(self):
        if self.serial_worker is None:
            return

        self.serial_worker.stop()
        self.serial_worker.wait(800)
        self.serial_worker = None

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
        if self.serial_worker is None:
            QMessageBox.information(self, "Bluetooth", "Bluetooth is not connected")
            return
        self.serial_worker.send_line(text)
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

        if "PB15 ON" in line.upper() or "LED ON" in line.upper():
            self.lbl_led.setText("ON")
        elif "PB15 OFF" in line.upper() or "LED OFF" in line.upper():
            self.lbl_led.setText("OFF")

    def _parse_telemetry(self, line: str):
        m = TELEMETRY_RE.search(line)
        if not m:
            return

        raw, mv, lux = m.groups()
        self.lbl_raw.setText(raw)
        self.lbl_mv.setText(mv)
        self.lbl_lux.setText(lux)
        self.lbl_last.setText(datetime.now().strftime("%Y-%m-%d %H:%M:%S"))

        raw_v = int(raw)
        mv_v = int(mv)
        lux_v = int(lux)

        self._sample_idx += 1
        self._chart_x.append(self._sample_idx)
        self._chart_raw.append(raw_v)
        self._chart_mv.append(mv_v)
        self._chart_lux.append(lux_v)

        x = list(self._chart_x)
        self.curve_raw.setData(x, list(self._chart_raw))
        self.curve_mv.setData(x, list(self._chart_mv))
        self.curve_lux.setData(x, list(self._chart_lux))

        if x:
            last_x = x[-1]
            first_x = max(0, last_x - self._chart_view_points + 1)
            self.plot.setXRange(first_x, max(self._chart_view_points, last_x + 1), padding=0)

        max_y = max(raw_v, mv_v, lux_v, 100)
        current_min, current_max = self.plot.viewRange()[1]
        if max_y > current_max:
            self.plot.setYRange(0, max_y * 1.15)

    def _append_log(self, box: QPlainTextEdit, tag: str, msg: str):
        ts = time.strftime("%H:%M:%S")
        box.appendPlainText(f"[{ts}] {tag}: {msg}")

    def closeEvent(self, event):
        try:
            self.disconnect_bt()
            self.disconnect_wifi()
        finally:
            super().closeEvent(event)


def main():
    app = QApplication(sys.argv)
    w = MainWindow()
    w.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
