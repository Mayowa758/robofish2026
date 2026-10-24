"""Turns the transmitter's serial text lines into events for the GUI.

Pure Python (no Qt, no serial) so it can be tested on its own: python3 tests/test_gui_parser.py

Lines the transmitter prints, and what they become:

    [MODE] SERVO                          -> mode    {"mode": "SERVO"}
    [SERVO] left=5 right=0 speed=1        -> servo   {"left": 5, "right": 0, "speed": 1}
    [SENSOR] ...                          -> sensor  {"text": "..."}
    Sending receiver firmware: 412000 bytes -> update_start {"total": 412000}
    [TX] Sent 16384 / 412000 bytes        -> progress {"sent": 16384, "total": 412000}
    [TX] ... / [WARN] ... / [ERROR] ...   -> update_status {"text": ..., "level": "info"|"warn"|"error"}
    Update successful / Update failed     -> update_done {"ok": True/False}
    [TX] stop sent (servos to 0)          -> stop_ack
    Ready. ...                            -> ready   (transmitter finished booting)
    anything else                         -> raw
"""

import re
from dataclasses import dataclass, field

RE_MODE = re.compile(r"^\[MODE\]\s+(SERVO|SENSOR)\b")
RE_SERVO = re.compile(r"^\[SERVO\]\s+left=(-?\d+)\s+right=(-?\d+)\s+speed=(-?\d+)")
RE_SENSOR = re.compile(r"^\[SENSOR\]\s*(.*)$")
RE_SENT = re.compile(r"^\[TX\]\s+Sent\s+(\d+)\s*/\s*(\d+)\s+bytes")
RE_TOTAL = re.compile(r"^Sending receiver firmware:\s+(\d+)\s+bytes")


@dataclass
class Event:
    kind: str
    data: dict = field(default_factory=dict)


def parse_line(line: str) -> Event:
    line = line.strip()

    m = RE_MODE.match(line)
    if m:
        return Event("mode", {"mode": m.group(1)})

    m = RE_SERVO.match(line)
    if m:
        return Event("servo", {"left": int(m.group(1)), "right": int(m.group(2)), "speed": int(m.group(3))})

    m = RE_SENSOR.match(line)
    if m:
        return Event("sensor", {"text": m.group(1)})

    m = RE_SENT.match(line)
    if m:
        return Event("progress", {"sent": int(m.group(1)), "total": int(m.group(2))})

    m = RE_TOTAL.match(line)
    if m:
        return Event("update_start", {"total": int(m.group(1))})

    if line.startswith("Update successful"):
        return Event("update_done", {"ok": True})
    if line.startswith("Update failed"):
        return Event("update_done", {"ok": False})

    if line.startswith("[TX] stop sent"):
        return Event("stop_ack")

    if line.startswith("[ERROR]"):
        return Event("update_status", {"text": line, "level": "error"})
    if line.startswith("[WARN]"):
        return Event("update_status", {"text": line, "level": "warn"})
    if line.startswith("[TX]"):
        return Event("update_status", {"text": line[4:].strip(), "level": "info"})

    if line.startswith("Ready."):
        return Event("ready")

    return Event("raw", {"text": line})
