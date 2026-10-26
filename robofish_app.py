
"""RoboFish app (Qt6 / PySide6).

Run from the project root:   python3 robofish_app.py

Shows what the transmitter prints on its serial port:
  blue  box  SERVO   (gray outline, full colour when the controller is in SERVO mode, faded otherwise)
  red   box  SENSOR  (gray outline, full colour when in SENSOR mode; the camera will go in here later)
  green box  UPDATE  (gray outline, full colour while a receiver firmware update is running)
and has a Build + update button, which runs update_receiver.py and then sends 'u' to the transmitter.

Only one program can hold the serial port at a time, so Build + update disconnects first
(update_receiver.py needs the port), runs the script, reconnects and then sends 'u'.

Picture of the fish: put a picture at  assets/fish.png  (or .jpg / .jpeg / .webp) next to this file.
Without one, a pixel-art clownfish is drawn instead.

Ocean background: an animated ocean (waves + bubbles) is drawn behind everything. To lay a real
ocean photo/picture over it, put one at  assets/ocean.jpg  (or .png / .jpeg / .webp); it is shown
semi-transparent. Tune it with the OCEAN_* constants below.
"""

import math
from datetime import datetime
import random
import sys
from pathlib import Path

from PySide6.QtCore import QIODevice, QPointF, QProcess, QSize, Qt, QTimer
from PySide6.QtGui import QBrush, QColor, QFont, QLinearGradient, QPainter, QPainterPath, QPen, QPixmap, QTextCursor
from PySide6.QtSerialPort import QSerialPort, QSerialPortInfo
from PySide6.QtWidgets import (
    QApplication,
    QComboBox,
    QFrame,
    QGraphicsOpacityEffect,
    QHBoxLayout,
    QLabel,
    QMainWindow,
    QPlainTextEdit,
    QProgressBar,
    QPushButton,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)

from gui_parser import parse_line
from camera_feed import PREFIX as CAMERA_PREFIX, CameraFeed
from pixel_font import FISH_H, FISH_PALETTE, FISH_W, GLYPH_H, fish_sprite, text_columns, text_pixels
import update_receiver  # only for its TRANSMITTER_PORT setting; importing it runs nothing

ROOT = Path(__file__).resolve().parent
SNAPSHOT_DIR = ROOT / "snapshots"
BAUD_RATE = 115200
DEFAULT_PORT = update_receiver.TRANSMITTER_PORT  # the port update_receiver.py uses

# --- colours -------------------------------------------------------------
SEA = "#3261bf"  # ocean blue background
OUTLINE = "#1a2a44"  # title shadow and dark text on the blue background
HIGHLIGHT = "#8CCBFF"  # gray outline around the current box
INACTIVE_OPACITY = 0.5  # boxes that are not the current mode are faded to this
WHITE = "#FFFFFF"

# --- ocean background ------------------------------------------------------
ANIMATE_OCEAN = False  # False = still picture, no timer
OCEAN_FPS = 20
DRAW_WAVES = False  # True = draw waves over the ocean background
DRAW_BUBBLES = False  # True = draw bubbles over the ocean background
OCEAN_PHOTO_OPACITY = 0.35  # how see-through an assets/ocean.* picture is
OCEAN_BUBBLES = 20

# --- text sizes (points) -------------------------------------------------
BASE_FONT_PT = 15  # everything that is not listed below
BOX_TITLE_PT = 24
LOG_FONT_PT = 13
BLUE = "#2f6fe4"
RED = "#e03a2f"
GREEN = "#2faa4a"

FIN_MIN, FIN_MAX = -70, 70
SPEED_MAX = 20


# =========================================================================
# Pixel-art widgets
# =========================================================================


class PixelText(QWidget):
    """Text drawn as chunky pixels, with a pixel drop shadow."""

    def __init__(self, text, scale=4, color=WHITE, shadow=OUTLINE):
        super().__init__()
        self._text = text
        self._scale = scale
        self._color = QColor(color)
        self._shadow = QColor(shadow) if shadow else None
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)

    def sizeHint(self):
        extra = self._scale if self._shadow else 0
        return QSize(text_columns(self._text) * self._scale + extra, GLYPH_H * self._scale + extra)

    def minimumSizeHint(self):
        return self.sizeHint()

    def paintEvent(self, event):
        painter = QPainter(self)
        s = self._scale
        pixels = text_pixels(self._text)
        if self._shadow is not None:
            for col, row in pixels:
                painter.fillRect(col * s + s, row * s + s, s, s, self._shadow)
        for col, row in pixels:
            painter.fillRect(col * s, row * s, s, s, self._color)


class ZeroCenteredBar(QProgressBar):
    """A bar with zero in the centre for positive and negative fin angles."""

    def __init__(self, minimum, maximum):
        super().__init__()
        self.setRange(minimum, maximum)
        self.setValue(0)
        self.setTextVisible(False)
        self.setFixedHeight(24)
        self.setStyleSheet("QProgressBar { border: none; }")

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)

        w, h = self.width(), self.height()
        radius = h / 2

        # Background track
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(0, 0, 0, 80))
        painter.drawRoundedRect(0, 0, w, h, radius, radius)

        # Centre position corresponding to zero
        zero_x = w * (0 - self.minimum()) / (
            self.maximum() - self.minimum()
        )

        # Current position corresponding to the fin angle
        value_x = w * (self.value() - self.minimum()) / (
            self.maximum() - self.minimum()
        )

        # Fill from zero towards the current value
        left = min(zero_x, value_x)
        right = max(zero_x, value_x)

        if self.value() != 0:
            painter.setBrush(QColor(WHITE))
            painter.drawRoundedRect(
                int(left), 0, max(1, int(right - left)), h, radius, radius
            )

        # Centre marker
        painter.setPen(QPen(QColor(255, 255, 255, 150), 1))
        painter.drawLine(int(zero_x), 3, int(zero_x), h - 3)


class OceanBackground(QWidget):
    """The window background: a blue water gradient with soft moving waves and rising bubbles.

    If assets/ocean.(jpg|png|jpeg|webp) exists it is drawn over the gradient, semi-transparent.
    """

    def __init__(self):
        super().__init__()
        self._phase = 0.0
        rng = random.Random(7)
        # each bubble: [x fraction, y fraction, speed (fraction of window height per frame), radius px]
        self._bubbles = [
            [rng.random(), rng.random(), rng.uniform(0.0015, 0.004), rng.uniform(4, 11)]
            for _ in range(OCEAN_BUBBLES)
        ]
        self._photo = None
        for name in ("ocean.jpg", "ocean.png", "ocean.jpeg", "ocean.webp"):
            path = ROOT / "assets" / name
            if path.exists():
                pixmap = QPixmap(str(path))
                if not pixmap.isNull():
                    self._photo = pixmap
                    break
        self._scaled = None
        self._scaled_for = None

        if ANIMATE_OCEAN:
            self._timer = QTimer(self)
            self._timer.timeout.connect(self.advance)
            self._timer.start(int(1000 / OCEAN_FPS))

    def advance(self):
        self._phase += 0.07
        for bubble in self._bubbles:
            bubble[1] -= bubble[2]
            if bubble[1] < -0.05:
                bubble[1] = 1.05
        self.update()

    def _wave(self, painter, w, h, base, amplitude, wavelength, phase, alpha):
        path = QPainterPath()
        path.moveTo(0, h)
        x = 0
        while x <= w + 8:
            path.lineTo(x, base + amplitude * math.sin(x / wavelength * 2 * math.pi + phase))
            x += 8
        path.lineTo(w + 8, h)
        path.closeSubpath()
        painter.fillPath(path, QBrush(QColor(255, 255, 255, alpha)))

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        w, h = self.width(), self.height()

        gradient = QLinearGradient(0, 0, 0, h)
        gradient.setColorAt(0.0, QColor(SEA))
        gradient.setColorAt(1.0, QColor(SEA).darker(150))
        painter.fillRect(0, 0, w, h, QBrush(gradient))

        if self._photo is not None:  # real ocean picture, semi-transparent, scaled to cover the window
            if self._scaled_for != (w, h):
                self._scaled = self._photo.scaled(
                    w, h, Qt.AspectRatioMode.KeepAspectRatioByExpanding, Qt.TransformationMode.SmoothTransformation
                )
                self._scaled_for = (w, h)
            painter.setOpacity(OCEAN_PHOTO_OPACITY)
            painter.drawPixmap((w - self._scaled.width()) // 2, (h - self._scaled.height()) // 2, self._scaled)
            painter.setOpacity(1.0)

        p = self._phase

        if DRAW_WAVES:
            self._wave(painter, w, h, h * 0.55, 12, 260, p, 26)
            self._wave(painter, w, h, h * 0.68, 16, 340, -p * 0.8 + 1.0, 32)
            self._wave(painter, w, h, h * 0.82, 20, 430, p * 0.6 + 2.0, 42)

        if DRAW_BUBBLES:
            painter.setPen(QPen(QColor(255, 255, 255, 130), 2))
            painter.setBrush(QBrush(QColor(255, 255, 255, 35)))
            for bx, by, _speed, radius in self._bubbles:
                x = bx * w + 7 * math.sin(p * 1.6 + bx * 20)
                painter.drawEllipse(QPointF(x, by * h), radius, radius)



class FishPicture(QWidget):
    """Picture of the fish: assets/fish.(png|jpg|jpeg|webp) if present, otherwise a pixel-art clownfish."""

    HEIGHT = 112

    def __init__(self):
        super().__init__()
        self._pixmap = None
        for name in ("fish.png", "fish.jpg", "fish.jpeg", "fish.webp"):
            path = ROOT / "assets" / name
            if path.exists():
                pixmap = QPixmap(str(path))
                if not pixmap.isNull():
                    self._pixmap = pixmap.scaledToHeight(
                        self.HEIGHT - 12,
                        Qt.TransformationMode.SmoothTransformation
                    )
                    break

    def sizeHint(self):
        if self._pixmap is not None:
            return QSize(self._pixmap.width() + 12, self.HEIGHT)
        return QSize(FISH_W * self._scale + 28, FISH_H * self._scale + 28)

    def minimumSizeHint(self):
        return self.sizeHint()

    def resizeEvent(self, event):
        super().resizeEvent(event)

    def paintEvent(self, event):
        painter = QPainter(self)
        w, h = self.width(), self.height()

        painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        painter.setPen(Qt.PenStyle.NoPen)
        painter.setBrush(QColor(52, 201, 235, 50))
        painter.drawRoundedRect(0, 0, w, h, 14, 14)

        painter.setRenderHint(QPainter.RenderHint.Antialiasing, False)

        if self._pixmap is not None:
            painter.drawPixmap(6, 6, self._pixmap)
            return

        s = self._scale
        ox = (w - FISH_W * s) // 2
        oy = (h - FISH_H * s) // 2

        for y, line in enumerate(self._sprite):
            for x, key in enumerate(line):
                if key != ".":
                    painter.fillRect(
                        ox + x * s,
                        oy + y * s,
                        s,
                        s,
                        QColor(FISH_PALETTE[key])
                    )



class Panel(QFrame):
    """A solid-colour rounded box. Current mode = full colour with a gray outline; otherwise faded."""

    def __init__(self, title, color):
        super().__init__()
        self.color = color
        self.setObjectName("panel")

        outer = QVBoxLayout(self)
        outer.setContentsMargins(20, 16, 20, 20)
        outer.setSpacing(12)

        self.title_label = QLabel(title)
        title_font = QFont()
        title_font.setPointSize(BOX_TITLE_PT)
        title_font.setBold(True)
        self.title_label.setFont(title_font)
        self.title_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        outer.addWidget(self.title_label)

        self.body = QVBoxLayout()
        self.body.setSpacing(8)
        outer.addLayout(self.body)
        outer.addStretch(1)

        # self.fade = QGraphicsOpacityEffect(self)
        # self.setGraphicsEffect(self.fade)
        self.set_active(False)

    def set_active(self, active):
        # Fade the background and each item separately, so an item marked keepOpaque (the camera) stays clear.
        opacity = 1.0 if active else INACTIVE_OPACITY
        bg = QColor(self.color)
        background = f"rgba({bg.red()}, {bg.green()}, {bg.blue()}, {int(opacity * 255)})"
        # same border width either way, so nothing moves; inactive border = box colour = no visible outline
        border = HIGHLIGHT if active else background
        self.setStyleSheet(
            f"QFrame#panel {{ background: {background}; border: 5px solid {border}; border-radius: 20px; }}"
            "QLabel { background: transparent; color: #ffffff; border: none; }"
        )
        for child in self.children():
            if not isinstance(child, QWidget) or child.property("keepOpaque"):
                continue
            effect = child.graphicsEffect()
            if not isinstance(effect, QGraphicsOpacityEffect):
                effect = QGraphicsOpacityEffect(child)
                child.setGraphicsEffect(effect)
            effect.setOpacity(opacity)


def make_bar(minimum, maximum):
    bar = QProgressBar()
    bar.setRange(minimum, maximum)
    bar.setValue(minimum)
    bar.setTextVisible(False)
    bar.setFixedHeight(24)
    bar.setStyleSheet(
        "QProgressBar { background: rgba(0, 0, 0, 80); border: none; border-radius: 12px; }"
        "QProgressBar::chunk { background: #ffffff; border-radius: 12px; }"
    )
    return bar


# =========================================================================
# Main window
# =========================================================================


class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("RoboFish")
        self.resize(1240, 960)

        self.serial = QSerialPort(self)
        self.serial.readyRead.connect(self.on_ready_read)
        self.serial.errorOccurred.connect(self.on_serial_error)
        self.rx_buffer = b""

        self.process = None  # update_receiver.py while Build + update runs
        self.pending_update = False  # send 'u' as soon as the transmitter has rebooted
        self.mode = "SERVO"
        self.updating = False

        self.build_ui()
        self.refresh_ports()
        self.refresh_highlight()
        self.refresh_buttons()
        
        self.rx_buffer = b""
        self.camera = CameraFeed()
        self.save_next_frame = False  # set by Snap: save the next picture that arrives

    # ------------------------------------------------------------------ UI

    def build_ui(self):
        central = OceanBackground()
        self.setCentralWidget(central)

        content = QVBoxLayout(central)
        content.setContentsMargins(24, 18, 24, 18)
        content.setSpacing(16)

        # --- header: title on the left, fish picture on the right ----------
        header = QHBoxLayout()
        header.addWidget(PixelText("ROBOFISH", scale=11, color=WHITE, shadow=OUTLINE), 0, Qt.AlignmentFlag.AlignVCenter)
        header.addStretch(1)

        fish_container = QWidget()
        fish_layout = QVBoxLayout(fish_container)
        fish_layout.setContentsMargins(0, 0, 0, 0)
        fish_layout.setSpacing(2)

        fish_picture = FishPicture()
        fish_layout.addWidget(fish_picture)

        version_text = QLabel("v2.0")
        version_text.setAlignment(Qt.AlignmentFlag.AlignCenter)
        version_text.setStyleSheet(
            "color: white; background: transparent;"
        )
        fish_layout.addWidget(version_text)

        header.addWidget(
            fish_container,
            0,
            Qt.AlignmentFlag.AlignVCenter
        )

        content.addLayout(header)


        # --- connection bar ----------------------------------------------
        top = QHBoxLayout()
        top.setSpacing(10)
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(400)
        self.refresh_button = QPushButton("Refresh")
        self.refresh_button.clicked.connect(self.refresh_ports)
        self.connect_button = QPushButton("Connect")
        self.connect_button.clicked.connect(self.toggle_connection)
        self.status_label = QLabel("Disconnected")
        self.status_label.setStyleSheet(f"color: {WHITE}; background: transparent;")
        top.addWidget(self.port_combo)
        top.addWidget(self.refresh_button)
        top.addWidget(self.connect_button)
        top.addWidget(self.status_label, 1)
        content.addLayout(top)

        # --- three boxes -------------------------------------------------
        boxes = QHBoxLayout()
        boxes.setSpacing(18)

        # servo (blue)
        self.servo_panel = Panel("SERVO", BLUE)
        self.left_bar, self.left_value = self.add_bar_row(self.servo_panel, "Left fin", FIN_MIN, FIN_MAX)
        self.right_bar, self.right_value = self.add_bar_row(self.servo_panel, "Right fin", FIN_MIN, FIN_MAX)
        self.speed_bar, self.speed_value = self.add_bar_row(self.servo_panel, "Speed", 0, SPEED_MAX)
        self.servo_panel.body.addSpacing(15)
        self.calibrate_button = QPushButton("Calibrate")
        self.calibrate_button.clicked.connect(lambda: self.send_char("s"))
        self.servo_panel.body.addWidget(self.calibrate_button)

        boxes.addWidget(self.servo_panel, 1)

        # sensor (red)
        self.sensor_panel = Panel("SENSOR", RED)
        self.sensor_text = QLabel("No sensors yet")
        self.sensor_text.setWordWrap(True)
        self.sensor_panel.body.addWidget(self.sensor_text)
        self.camera_view = QLabel("No picture yet")
        self.camera_view.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.camera_view.setSizePolicy(QSizePolicy.Policy.Ignored, QSizePolicy.Policy.Fixed)  # width: don't grow with the picture
        self.camera_view.setFixedHeight(180)
        self.camera_view.setProperty("keepOpaque", True)
        self.camera_view.setStyleSheet(
            "background: rgba(0, 0, 0, 60); color: #ffffff; border: 2px dashed rgba(255, 255, 255, 140); border-radius: 12px;"
        )
        self.sensor_panel.body.addWidget(self.camera_view)
        self.camera_status = QLabel("Camera off")
        self.sensor_panel.body.addWidget(self.camera_status)
        camera_buttons = QHBoxLayout()
        self.camera_start_button = QPushButton("Start")
        self.camera_stop_button = QPushButton("Stop")
        self.camera_snap_button = QPushButton("Snap")
        self.camera_start_button.clicked.connect(lambda: self.send_char("v"))
        self.camera_stop_button.clicked.connect(lambda: self.send_char("x"))
        self.camera_snap_button.clicked.connect(self.snap)
        for button in (self.camera_start_button, self.camera_stop_button, self.camera_snap_button):
            camera_buttons.addWidget(button)
        self.sensor_panel.body.addLayout(camera_buttons)
        boxes.addWidget(self.sensor_panel, 1)

        # update (green)
        self.update_panel = Panel("UPDATE", GREEN)
        self.update_status = QLabel("Idle")
        self.update_status.setWordWrap(True)
        self.update_bar = make_bar(0, 100)
        self.update_bytes = QLabel("0 / 0 bytes")
        self.build_button = QPushButton("Build + Update")
        self.build_button.clicked.connect(self.build_and_update)
        self.update_panel.body.addWidget(self.update_status)
        self.update_panel.body.addWidget(self.update_bar)
        self.update_panel.body.addWidget(self.update_bytes)
        self.update_panel.body.addSpacing(6)
        self.update_panel.body.addWidget(self.build_button)
        boxes.addWidget(self.update_panel, 1)

        content.addLayout(boxes, 3)

        # --- raw log (pixel title on the left, Clear on the right) -----------
        clear_button = QPushButton("Clear")
        log_header = QHBoxLayout()
        log_header.addWidget(PixelText("LOG", scale=6, color=WHITE, shadow=OUTLINE), 0, Qt.AlignmentFlag.AlignVCenter)
        log_header.addStretch(1)
        log_header.addWidget(clear_button)
        content.addLayout(log_header)

        self.log = QPlainTextEdit()
        self.log.setReadOnly(True)
        self.log.setMaximumBlockCount(3000)
        mono = QFont("Menlo")
        mono.setStyleHint(QFont.StyleHint.Monospace)
        mono.setPointSize(LOG_FONT_PT)
        self.log.setFont(mono)
        self.log.setStyleSheet(
            "QPlainTextEdit { background: #0d0f14; color: #e6e8ee; border: 1px solid #2a2e38; border-radius: 8px; }"
        )
        clear_button.clicked.connect(self.log.clear)
        content.addWidget(self.log, 1)

        # normal Qt buttons and drop-down, same look as the first version of the app
        self.setStyleSheet(
            "QPushButton { background: #232733; color: #e6e8ee; border: 1px solid #3a3f4b; border-radius: 8px; padding: 9px 20px; }"
            "QPushButton:hover { background: #2c3140; }"
            "QPushButton:disabled { color: #5d6372; background: #1a1d24; border-color: #2a2e38; }"
            "QComboBox { background: #232733; color: #e6e8ee; border: 1px solid #3a3f4b; border-radius: 8px; padding: 8px 12px; }"
            "QComboBox QAbstractItemView { background: #232733; color: #e6e8ee; selection-background-color: #3a3f4b; }"
        )

    def add_bar_row(self, panel, name, minimum, maximum):
        panel.body.addWidget(QLabel(name))
        row = QHBoxLayout()

        if name in ("Left fin", "Right fin"):
            bar = ZeroCenteredBar(minimum, maximum)
        else:
            bar = make_bar(minimum, maximum)

        value = QLabel("0")
        value.setMinimumWidth(72)
        value.setAlignment(Qt.AlignmentFlag.AlignRight | Qt.AlignmentFlag.AlignVCenter)
        row.addWidget(bar, 1)
        row.addWidget(value)
        panel.body.addLayout(row)
        return bar, value

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
            elif previous is None and location == DEFAULT_PORT:
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
        self.camera.reset()
        self.mode = "SERVO"  # the transmitter starts in SERVO mode
        self.status_label.setText(f"Connected to {info.systemLocation()}")
        self.refresh_highlight()
        self.refresh_buttons()
        return True

    def disconnect_serial(self):
        if self.serial.isOpen():
            self.serial.close()
        self.status_label.setText("Disconnected")
        self.status_label
        self.refresh_buttons()

    def on_serial_error(self, error):
        if error == QSerialPort.SerialPortError.ResourceError:  # cable pulled
            self.append_log("[APP] serial port lost")
            self.disconnect_serial()

    def send_char(self, char):
        if not self.serial.isOpen():
            return
        self.serial.write(char.encode("ascii"))
        self.append_log(f"[APP] sent '{char}'")

    def calibrate_servos(self):
        if not self.serial.isOpen():
            self.append_log("[APP] Connect to the transmitter first.")
            return

        self.send_char("c")

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
        if line.startswith(CAMERA_PREFIX):  # camera pieces: not logged, there are many
            jpeg = self.camera.feed(line)
            if jpeg is not None:
                self.show_camera_frame(jpeg)
            return
        if line.startswith("[CAMERA]"):  # receiver camera messages: also shown under the picture
            self.camera_status.setText(line[len("[CAMERA]"):].strip())
        self.append_log(line)
        event = parse_line(line)
        kind, data = event.kind, event.data

        if kind == "mode":
            self.mode = data["mode"]
            self.refresh_highlight()
        elif kind == "servo":
            left = max(FIN_MIN, min(FIN_MAX, data["left"]))
            right = max(FIN_MIN, min(FIN_MAX, data["right"]))
            speed = max(0, min(SPEED_MAX, data["speed"]))
            self.left_bar.setValue(left)
            self.right_bar.setValue(right)
            self.speed_bar.setValue(speed)
            self.left_value.setText(f"{left}°")
            self.right_value.setText(f"{right}°")
            self.speed_value.setText(str(speed))
        elif kind == "sensor":
            self.sensor_text.setText(data["text"] or "(no data)")
        elif kind == "update_start":
            self.set_updating(True)
            self.update_bar.setRange(0, max(1, data["total"]))
            self.update_bar.setValue(0)
            self.update_bytes.setText(f"0 / {data['total']} bytes")
            self.update_status.setText("Starting update...")
        elif kind == "progress":
            self.set_updating(True)
            self.update_bar.setRange(0, max(1, data["total"]))
            self.update_bar.setValue(min(data["sent"], data["total"]))
            self.update_bytes.setText(f"{data['sent']} / {data['total']} bytes")
        elif kind == "update_status":
            self.update_status.setText(data["text"])
        elif kind == "update_done":
            self.set_updating(False)
            if data["ok"]:
                self.update_bar.setValue(self.update_bar.maximum())
                self.update_status.setText("Update successful. The receiver is restarting.")
            else:
                self.update_status.setText("Update failed. See the log.")
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
        self.build_button.setEnabled(not busy and not self.updating)
        self.calibrate_button.setEnabled(connected and not busy and not self.updating)
        for button in (self.camera_start_button, self.camera_stop_button, self.camera_snap_button):
            button.setEnabled(connected and not busy and not self.updating)

    def show_camera_frame(self, jpeg):
        pixmap = QPixmap()
        if not pixmap.loadFromData(jpeg, "JPG"):
            return
        self.camera_view.setPixmap(
            pixmap.scaled(
                self.camera_view.size(), Qt.AspectRatioMode.KeepAspectRatio, Qt.TransformationMode.SmoothTransformation
            )
        )
        self.camera_status.setText(self.camera.status_text())
        if self.save_next_frame:
            self.save_next_frame = False
            self.save_snapshot(jpeg)
    def snap(self):
        """Ask the fish for a picture and save it when it arrives."""
        self.save_next_frame = True
        self.send_char("p")

    def save_snapshot(self, jpeg):
        SNAPSHOT_DIR.mkdir(exist_ok=True)
        path = SNAPSHOT_DIR / datetime.now().strftime("snap_%Y-%m-%d_%H-%M-%S.jpg")
        path.write_bytes(jpeg)
        self.append_log(f"[APP] saved {path}")
    # ------------------------------------------------------------------ Build + update

    def start_update(self):
        """Send 'u': the transmitter sends the firmware it has embedded to the receiver."""
        if not self.serial.isOpen():
            return
        self.update_status.setText("Sending 'u'...")
        self.update_bar.setValue(0)
        self.send_char("u")

    def build_and_update(self):
        """Run update_receiver.py (build receiver, embed, build + upload transmitter), then send 'u'."""
        if self.process is not None:
            return

        location = self.selected_location()
        if location != update_receiver.TRANSMITTER_PORT:
            # update_receiver.py has its own TRANSMITTER_PORT; it is left as it was, so the two must agree.
            self.append_log(
                f"[APP] selected port {location or '(none)'} is not TRANSMITTER_PORT in update_receiver.py "
                f"({update_receiver.TRANSMITTER_PORT}). Select that port, or change it in the script."
            )
            self.update_status.setText("Port does not match update_receiver.py. See the log.")
            return

        self.disconnect_serial()  # the script needs the serial port to itself

        self.process = QProcess(self)
        self.process.setWorkingDirectory(str(ROOT))
        self.process.setProcessChannelMode(QProcess.ProcessChannelMode.MergedChannels)
        self.process.readyReadStandardOutput.connect(self.on_process_output)
        self.process.finished.connect(self.on_process_finished)

        self.update_status.setText("Building (this takes a while)...")
        self.update_bar.setRange(0, 100)
        self.update_bar.setValue(0)
        self.update_bytes.setText("building...")
        self.append_log("[APP] running update_receiver.py")
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
            self.refresh_buttons()
            return

        self.append_log("[APP] transmitter uploaded, reconnecting")
        self.update_status.setText("Transmitter uploaded. Reconnecting...")
        self.pending_update = True
        QTimer.singleShot(1500, self.reconnect_after_upload)

    def reconnect_after_upload(self):
        self.refresh_ports()
        if not self.connect_serial():
            self.pending_update = False
            self.append_log("[APP] could not reconnect; press Connect, then Build + update again")
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
    font = app.font()
    font.setPointSize(BASE_FONT_PT)
    app.setFont(font)
    window = MainWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
