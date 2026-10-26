"""Reassembles camera frames that the transmitter forwards as text lines.

One line per piece of a JPEG, pieces of a frame arrive in order:

    [CAM] <frameId> <offset> <total> <base64 data>

Pure Python (no Qt) so it can be tested on its own. Pass every [CAM] line to feed(): it returns the
complete JPEG bytes when a frame finishes, otherwise None. A frame with a missing, broken or
out-of-order piece is dropped and counted; the next frame starts clean.
"""

import base64
import binascii
import time
from collections import deque

PREFIX = "[CAM] "
MAX_FRAME_BYTES = 200_000  # sanity limit; a 640x480 JPEG is far smaller
FPS_WINDOW_S = 5.0


class CameraFeed:
    def __init__(self):
        self.reset()

    def reset(self):
        """Forget any half-received frame and the statistics (e.g. after reconnecting)."""
        self._active = False  # currently assembling a frame
        self._frame_id = None
        self._total = 0
        self._buffer = bytearray()
        self.frames = 0  # complete frames received
        self.dropped = 0  # frames lost to a missing / broken piece
        self.last_size = 0  # bytes in the last complete frame
        self._done_times = deque()

    # ------------------------------------------------------------------ input

    def feed(self, line):
        """Feed one '[CAM] ...' line. Returns JPEG bytes when a frame completes, else None."""
        try:
            _tag, frame_id, offset, total, data = line.split(" ", 4)
            frame_id, offset, total = int(frame_id), int(offset), int(total)
            chunk = base64.b64decode(data.strip(), validate=True)
        except (ValueError, binascii.Error):
            self._drop()
            return None

        if offset == 0:  # first piece of a new frame
            if self._active:
                self._drop()  # the previous frame never finished
            if not 0 < total <= MAX_FRAME_BYTES:
                return None
            self._active = True
            self._frame_id = frame_id
            self._total = total
            self._buffer = bytearray()
        elif not self._active or frame_id != self._frame_id or offset != len(self._buffer):
            self._drop()  # a piece went missing
            return None

        self._buffer += chunk
        if len(self._buffer) > self._total:
            self._drop()
            return None
        if len(self._buffer) < self._total:
            return None

        # frame complete
        self._active = False
        self.frames += 1
        self.last_size = self._total
        now = time.monotonic()
        self._done_times.append(now)
        while self._done_times and now - self._done_times[0] > FPS_WINDOW_S:
            self._done_times.popleft()
        return bytes(self._buffer)

    def _drop(self):
        if self._active:
            self.dropped += 1
        self._active = False

    # ------------------------------------------------------------------ stats

    def fps(self):
        times = self._done_times
        if len(times) < 2 or times[-1] == times[0]:
            return 0.0
        return (len(times) - 1) / (times[-1] - times[0])

    def status_text(self):
        return f"{self.fps():.1f} fps  |  {self.last_size / 1024:.1f} KB/frame  |  {self.dropped} dropped"