"""Robofish GUI (Qt6 / PySide6).

Run from the project root:   python3 gui.py

Shows what the transmitter prints on its serial port:
  blue  box  SERVO   (lit when the controller is in SERVO mode)
  red   box  SENSOR  (lit when in SENSOR mode; the camera will go in here later)
  green box  UPDATE  (lit while a receiver firmware update is running)
and has buttons for: Connect, Stop servos ('s'), Update receiver ('u'), Build + update.

Only one program can hold the serial port at a time, so Build + update disconnects first
(update_receiver.py needs the port), runs the script, reconnects and then sends 'u'.
"""

import sys
from pathlib import Path

from PySide6.QtCore import QIODevice, QProcess, QProcessEnvironment, Qt, QTimer
from PySide6.QtGui import QFont, QTextCursor
from PySide6.QtSerialPort import QSerialPort, QSerialPortInfo
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QFrame,
    QGridLayout,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPlainTextEdit,
    QProgressBar,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from gui_parser import parse_line

ROOT = Path(__file__).resolve().parent
BAUD_RATE = 115200
DEFAULT_PORT_HINT = "usbserial-0001"

BLUE = "#3b82f6"
RED = "#ef4444"
GREEN = "#22c55e"

FIN_MIN, FIN_MAX = -70, 70
SPEED_MAX = 20


def rgba(hex_color, alpha):
    h = hex_color.lstrip("#")
    r, g, b = int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16)
    return f"rgba({r}, {g}, {b}, {alpha})"


class Panel(QFrame):
    """A titled box that lights up in its colour when active and dims when not."""

    def __init__(self, title, color):
        super().__init__()
        self.color = color
        self.setObjectName("panel")

        outer = QVBoxLayout(self)
        outer.setContentsMargins(16, 12, 16, 16)
        outer.setSpacing(10)

        self.title_label = QLabel(title)
        font = QFont()
        font.setPointSize(15)
        font.setBold(True)
        self.title_label.setFont(font)
        outer.addWidget(self.title_label)

        self.body = QVBoxLayout()
        self.body.setSpacing(8)
        outer.addLayout(self.body)
        outer.addStretch(1)

        self.set_active(False)

    def set_active(self, active):
        border = self.color if active else "#3a3f4b"
        width = 3 if active else 1
        background = rgba(self.color, 0.14) if active else "#1a1d24"
        title_color = self.color if active else "#7b8190"
        self.setStyleSheet(
            f"QFrame#panel {{ border: {width}px solid {border}; border-radius: 12px; background: {background}; }}"
            "QLabel { background: transparent; border: none; }"
        )
        self.title_label.setStyleSheet(f"color: {title_color};")


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Robofish")
        self.resize(1100, 720)

        self.serial = QSerialPort(self)
        self.serial.readyRead.connect(self.on_ready_read)
        self.serial.errorOccurred.connect(self.on_serial_error)
        self.rx_buffer = b""

        self.process = None  # update_receiver.py while "Build + update" runs
        self.pending_update = False  # send 'u' as soon as the transmitter has rebooted
        self.mode = "SERVO"
        self.updating = False

        self.build_ui()
        self.refresh_ports()
        self.refresh_highlight()
        self.refresh_buttons()

    # ------------------------------------------------------------------ UI

    def build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(16, 16, 16, 16)
        root.setSpacing(14)

        # --- top bar -------------------------------------------------
        top = QHBoxLayout()
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(320)
        self.refresh_button = QPushButton("Refresh")
        self.refresh_button.clicked.connect(self.refresh_ports)
        self.connect_button = QPushButton("Connect")
        self.connect_button.clicked.connect(self.toggle_connection)
        self.status_label = QLabel("Disconnected")
        self.status_label.setStyleSheet("color: #9aa1b2;")
        self.stop_button = QPushButton("Stop servos (s)")
        self.stop_button.setStyleSheet(f"QPushButton {{ background: {rgba(RED, 0.25)}; border: 1px solid {RED}; }}")
        self.stop_button.clicked.connect(lambda: self.send_char("s"))

        top.addWidget(QLabel("Port"))
        top.addWidget(self.port_combo)
        top.addWidget(self.refresh_button)
        top.addWidget(self.connect_button)
        top.addWidget(self.status_label, 1)
        top.addWidget(self.stop_button)
        root.addLayout(top)

        # --- three boxes -------------------------------------------------
        boxes = QHBoxLayout()
        boxes.setSpacing(14)

        # servo (blue)
        self.servo_panel = Panel("SERVO", BLUE)
        self.left_bar = self.make_bar(FIN_MIN, FIN_MAX, BLUE, "%v°")
        self.right_bar = self.make_bar(FIN_MIN, FIN_MAX, BLUE, "%v°")
        self.speed_bar = self.make_bar(0, SPEED_MAX, BLUE, "%v")
        for name, bar in (("Left fin", self.left_bar), ("Right fin", self.right_bar), ("Speed", self.speed_bar)):
            self.servo_panel.body.addWidget(QLabel(name))
            self.servo_panel.body.addWidget(bar)
        boxes.addWidget(self.servo_panel, 1)

        # sensor (red)
        self.sensor_panel = Panel("SENSOR", RED)
        self.sensor_text = QLabel("No sensors yet")
        self.sensor_text.setWordWrap(True)
        self.sensor_panel.body.addWidget(self.sensor_text)
        self.camera_view = QLabel("Camera: not configured yet")  # the camera image will be shown here later
        self.camera_view.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.camera_view.setMinimumHeight(170)
        self.camera_view.setStyleSheet("color: #7b8190; border: 1px dashed #3a3f4b; border-radius: 8px;")
        self.sensor_panel.body.addWidget(self.camera_view)
        boxes.addWidget(self.sensor_panel, 1)

        # update (green)
        self.update_panel = Panel("UPDATE", GREEN)
        self.update_status = QLabel("Idle")
        self.update_status.setWordWrap(True)
        self.update_bar = self.make_bar(0, 100, GREEN, "")
        self.update_bar.setFormat("0 / 0 bytes")
        self.update_button = QPushButton("Update receiver (u)")
        self.update_button.clicked.connect(self.start_update)
        self.build_button = QPushButton("Build + update")
        self.build_button.clicked.connect(self.build_and_update)
        for button in (self.update_button, self.build_button):
            button.setStyleSheet(f"QPushButton {{ background: {rgba(GREEN, 0.22)}; border: 1px solid {GREEN}; }}")
        self.update_panel.body.addWidget(self.update_status)
        self.update_panel.body.addWidget(self.update_bar)
        self.update_panel.body.addWidget(self.update_button)
        self.update_panel.body.addWidget(self.build_button)
        boxes.addWidget(self.update_panel, 1)

        root.addLayout(boxes, 3)

        # --- raw log -------------------------------------------------
        log_header = QHBoxLayout()
        log_header.addWidget(QLabel("Log"))
        log_header.addStretch(1)
        clear_button = QPushButton("Clear")
        log_header.addWidget(clear_button)
        root.addLayout(log_header)

        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(3000)
        mono = QFont("Menlo")
        mono.setStyleHint(QFont.StyleHint.Monospace)
        mono.setPointSize(11)
        self.log.setFont(mono)
        clear_button.clicked.connect(self.log.clear)
        root.addWidget(self.log, 2)

        self.setStyleSheet(
            "QMainWindow, QWidget { background: #12141a; color: #e6e8ee; }"
            "QPushButton { background: #232733; border: 1px solid #3a3f4b; border-radius: 6px; padding: 7px 14px; }"
            "QPushButton:hover { background: #2c3140; }"
            "QPushButton:disabled { color: #5d6372; background: #1a1d24; border-color: #2a2e38; }"
            "QComboBox { background: #232733; border: 1px solid #3a3f4b; border-radius: 6px; padding: 6px 10px; }"
            "QPlainTextEdit { background: #0d0f14; border: 1px solid #2a2e38; border-radius: 8px; }"
        )

    @staticmethod
    def make_bar(minimum, maximum, color, fmt):
        bar = QProgressBar()
        bar.setRange(minimum, maximum)
        bar.setValue(0)
        bar.setAlignment(Qt.AlignmentFlag.AlignCenter)
        bar.setFormat(fmt)
        bar.setFixedHeight(24)
        bar.setStyleSheet(
            "QProgressBar { background: #0d0f14; border: 1px solid #2a2e38; border-radius: 6px; color: white; }"
            f"QProgressBar::chunk {{ background: {color}; border-radius: 5px; }}"
        )
        return bar

    # ------------------------------------------------------------------ serial

    def refresh_ports(self):
        previous = self.port_combo.currentData()
        self.port_combo.clear()
        wanted = 0
        for info in QSerialPortInfo.availablePorts():
            location = info.systemLocation()
            if location.startswith("/dev/tty."):  # macOS lists every port twice; use the cu.* one
                continue
            self.port_combo.addItem(f"{location}  {info.description()}".strip(), info)
            index = self.port_combo.count() - 1
            if previous is not None and location == previous.systemLocation():
                wanted = index
            elif previous is None and DEFAULT_PORT_HINT in location:
                wanted = index
        if self.port_combo.count():
            self.port_combo.setCurrentIndex(wanted)

    def selected_location(self):
        info = self.port_combo.currentData()
        return info.systemLocation() if info is not None else ""

    def toggle_connection(self):
        if self.serial.isOpen():
            self.disconnect_serial()
        else:
            self.connect_serial()

    def connect_serial(self):
        info = self.port_combo.currentData()
        if info is None:
            self.status_label.setText("No serial port found. Plug in the transmitter and press Refresh.")
            return False

        self.serial.setPort(info)
        self.serial.setBaudRate(BAUD_RATE)
        if not self.serial.open(QIODevice.OpenModeFlag.ReadWrite):
            self.status_label.setText(f"Could not open port: {self.serial.errorString()}  (is the serial monitor open?)")
            return False

        self.rx_buffer = b""
        self.mode = "SERVO"  # the transmitter starts in SERVO mode
        self.status_label.setText(f"Connected to {info.systemLocation()}")
        self.refresh_highlight()
        self.refresh_buttons()
        return True

    def disconnect_serial(self):
        if self.serial.isOpen():
            self.serial.close()
        self.status_label.setText("Disconnected")
        self.refresh_buttons()

    def on_serial_error(self, error):
        if error == QSerialPort.SerialPortError.ResourceError:  # cable pulled
            self.append_log("[GUI] serial port lost")
            self.disconnect_serial()

    def send_char(self, char):
        if not self.serial.isOpen():
            return
        self.serial.write(char.encode("ascii"))
        self.append_log(f"[GUI] sent '{char}'")

    def on_ready_read(self):
        self.rx_buffer += bytes(self.serial.readAll())
        while b"\n" in self.rx_buffer:
            raw, self.rx_buffer = self.rx_buffer.split(b"\n", 1)
            line = raw.decode("utf-8", errors="replace").strip()
            if line:
                self.handle_line(line)
        if len(self.rx_buffer) > 65536:  # junk without newlines
            self.rx_buffer = b""

    # ------------------------------------------------------------------ events

    def handle_line(self, line):
        self.append_log(line)
        event = parse_line(line)
        kind, data = event.kind, event.data

        if kind == "mode":
            self.mode = data["mode"]
            self.refresh_highlight()
        elif kind == "servo":
            self.left_bar.setValue(max(FIN_MIN, min(FIN_MAX, data["left"])))
            self.right_bar.setValue(max(FIN_MIN, min(FIN_MAX, data["right"])))
            self.speed_bar.setValue(max(0, min(SPEED_MAX, data["speed"])))
        elif kind == "sensor":
            self.sensor_text.setText(data["text"] or "(no data)")
        elif kind == "update_start":
            self.set_updating(True)
            self.update_bar.setRange(0, max(1, data["total"]))
            self.update_bar.setValue(0)
            self.update_bar.setFormat(f"0 / {data['total']} bytes")
            self.update_status.setText("Starting update...")
        elif kind == "progress":
            self.set_updating(True)
            self.update_bar.setRange(0, max(1, data["total"]))
            self.update_bar.setValue(min(data["sent"], data["total"]))
            self.update_bar.setFormat(f"{data['sent']} / {data['total']} bytes")
        elif kind == "update_status":
            self.update_status.setText(data["text"])
            color = {"error": RED, "warn": "#f59e0b"}.get(data["level"], "#e6e8ee")
            self.update_status.setStyleSheet(f"color: {color};")
        elif kind == "update_done":
            self.set_updating(False)
            if data["ok"]:
                self.update_bar.setValue(self.update_bar.maximum())
                self.update_status.setText("Update successful. The receiver is restarting.")
                self.update_status.setStyleSheet(f"color: {GREEN};")
            else:
                self.update_status.setText("Update failed. See the log.")
                self.update_status.setStyleSheet(f"color: {RED};")
        elif kind == "ready":
            if self.pending_update:  # transmitter finished rebooting after the upload
                self.pending_update = False
                self.start_update()

    def set_updating(self, value):
        if self.updating != value:
            self.updating = value
            self.refresh_highlight()
            self.refresh_buttons()

    def refresh_highlight(self):
        self.servo_panel.set_active(self.mode == "SERVO" and not self.updating)
        self.sensor_panel.set_active(self.mode == "SENSOR" and not self.updating)
        self.update_panel.set_active(self.updating)

    def refresh_buttons(self):
        connected = self.serial.isOpen()
        busy = self.process is not None
        self.connect_button.setText("Disconnect" if connected else "Connect")
        self.connect_button.setEnabled(not busy)
        self.refresh_button.setEnabled(not connected and not busy)
        self.port_combo.setEnabled(not connected and not busy)
        self.stop_button.setEnabled(connected and not self.updating)
        self.update_button.setEnabled(connected and not self.updating and not busy)
        self.build_button.setEnabled(not busy and not self.updating)

    # ------------------------------------------------------------------ update buttons

    def start_update(self):
        if not self.serial.isOpen():
            return
        self.update_status.setStyleSheet("")
        self.update_status.setText("Sending 'u'...")
        self.update_bar.setValue(0)
        self.send_char("u")

    def build_and_update(self):
        """Run update_receiver.py (build receiver, embed, build + upload transmitter), then send 'u'."""
        if self.process is not None:
            return

        location = self.selected_location()
        self.disconnect_serial()  # the script needs the serial port to itself

        env = QProcessEnvironment.systemEnvironment()
        if location:
            env.insert("ROBOFISH_PORT", location)

        self.process = QProcess(self)
        self.process.setProcessEnvironment(env)
        self.process.setWorkingDirectory(str(ROOT))
        self.process.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        self.process.readyReadStandardOutput.connect(self.on_process_output)
        self.process.finished.connect(self.on_process_finished)

        self.update_status.setStyleSheet("")
        self.update_status.setText("Building (this takes a while)...")
        self.update_bar.setRange(0, 100)
        self.update_bar.setValue(0)
        self.update_bar.setFormat("building...")
        self.append_log("[GUI] running update_receiver.py")
        self.process.start(sys.executable, ["-u", str(ROOT / "update_receiver.py")])
        self.refresh_buttons()

    def on_process_output(self):
        text = bytes(self.process.readAllStandardOutput()).decode("utf-8", errors="replace")
        for line in text.replace("\r", "\n").split("\n"):
            if line.strip():
                self.append_log(line)

    def on_process_finished(self, exit_code, _status):
        ok = exit_code == 0
        self.process = None
        if not ok:
            self.update_status.setText("Build / upload failed. See the log.")
            self.update_status.setStyleSheet(f"color: {RED};")
            self.refresh_buttons()
            return

        self.append_log("[GUI] transmitter uploaded, reconnecting")
        self.update_status.setText("Transmitter uploaded. Reconnecting...")
        self.pending_update = True
        QTimer.singleShot(1500, self.reconnect_after_upload)

    def reconnect_after_upload(self):
        self.refresh_ports()
        if not self.connect_serial():
            self.pending_update = False
            self.append_log("[GUI] could not reconnect; press Connect, then Update receiver")
            return
        # Normally the transmitter's "Ready." line triggers the update. Fallback if it was missed:
        QTimer.singleShot(8000, self.update_fallback)

    def update_fallback(self):
        if self.pending_update:
            self.pending_update = False
            self.start_update()

    # ------------------------------------------------------------------ misc

    def append_log(self, text):
        self.log.appendPlainText(text)
        self.log.moveCursor(QTextCursor.MoveOperation.End)

    def closeEvent(self, event):
        if self.process is not None:
            self.process.kill()
        if self.serial.isOpen():
            self.serial.close()
        super().closeEvent(event)


def main():
    app = QApplication(sys.argv)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
