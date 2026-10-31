"""Threaded RDR double with atomic snapshots and application-level ECS acknowledgements."""

from collections import OrderedDict, deque
import socket
import threading
import time
from . import wire as w
from .geometry import (Calibration, SENSOR, FIXED, fixed_target, input_target,
                       transmitted_targets)


class RdrServer:
    def __init__(self, bind=("127.0.0.1", 1001), ecs=("127.0.0.1", 5000)):
        # Resolve once; incoming commands must match both configured ECS IP and port.
        self.ecs = (socket.gethostbyname(ecs[0]), ecs[1])
        if not 1 <= ecs[1] <= 65535:
            raise ValueError("ECS port must be 1..65535")
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        try:
            self.sock.bind(bind)
            self.sock.settimeout(.02)
        except Exception:
            self.sock.close()
            raise
        self.address = self.sock.getsockname()
        self.lock = threading.RLock()
        self.running = False
        self.thread = None
        self.targets = OrderedDict({1: w.Target(1, 3, .2, .1)})
        self.pan = self.tilt = 0.0
        self.input_frame = SENSOR
        self.calibration = Calibration()
        self.period_ms = 1000
        self.session = 0
        self.tx = self.rx = self.frame = 0
        self.enabled = False
        self.suppressed = set()
        self.retired = deque(maxlen=64)
        self.last_command = self.last_send = 0.0
        self.dirty = True
        self.sent = self.acked = self.errors = 0
        self.last_ack_frame = 0
        self.last_ack_at = 0.0
        self.confirmed = []
        self.pending = OrderedDict()
        self.logs = deque(maxlen=120)
        self.last_packet = b""

    def log(self, text):
        self.logs.append(time.strftime("%H:%M:%S") + "  " + text)

    def start(self):
        self.running = True
        self.thread = threading.Thread(target=self.run, name="rdr-udp", daemon=True)
        self.thread.start()
        return self

    def set_target(self, target):
        target = target.quantized()
        with self.lock:
            if target.id not in self.targets and len(self.targets) >= 10:
                raise ValueError("표적은 최대 10개까지 추가할 수 있습니다.")
            transmitted_targets([target], self.input_frame, self.pan, self.tilt, self.calibration)
            self.targets[target.id] = target
            self.dirty = True

    def remove_target(self, id_):
        with self.lock:
            del self.targets[id_]
            self.dirty = True

    def set_pose(self, pan, tilt):
        w.frame_body([], pan, tilt, self.period_ms)
        with self.lock:
            pan, tilt = round(pan, 2), round(tilt, 2)
            transmitted_targets(self.targets.values(), self.input_frame, pan, tilt, self.calibration)
            self.pan, self.tilt = pan, tilt
            self.dirty = True

    def set_input_frame(self, frame):
        if frame not in (SENSOR, FIXED):
            raise ValueError("알 수 없는 입력 좌표계입니다.")
        with self.lock:
            converted = [input_target(fixed_target(t, self.input_frame, self.pan, self.tilt,
                                                   self.calibration), frame, self.pan, self.tilt,
                                      self.calibration).quantized() for t in self.targets.values()]
            transmitted_targets(converted, frame, self.pan, self.tilt, self.calibration)
            self.targets = OrderedDict((t.id, t) for t in converted)
            self.input_frame = frame
            self.dirty = True

    def set_calibration(self, calibration):
        with self.lock:
            transmitted_targets(self.targets.values(), self.input_frame, self.pan, self.tilt,
                                calibration)
            self.calibration = calibration
            self.dirty = True

    def send(self, kind, body=b"", frame=0, captured_ms=None):
        self.tx = (self.tx + 1) & 65535
        packet = w.Packet(kind, self.tx, self.rx, self.session, frame,
                          int(time.time() * 1000) if captured_ms is None else captured_ms, body)
        self.last_packet = packet.encode()
        self.sock.sendto(self.last_packet, self.ecs)

    def receive(self, data, source):
        if source != self.ecs:
            raise ValueError("Command from unexpected ECS endpoint")
        packet = w.Packet.decode(data)
        if packet.kind == w.ER01:
            if packet.body:
                raise ValueError("ER01 body must be empty")
            if packet.session in self.retired:
                raise ValueError("Retired session")
            if packet.session != self.session:
                if self.session:
                    self.retired.append(self.session)
                self.session = packet.session
                self.tx = self.frame = 0
                self.enabled = False
                self.pending.clear()
                self.confirmed = []
                self.last_ack_at = self.last_ack_frame = 0
                self.suppressed.clear()
                self.log("ECS 연결: 새 세션")
            self.rx = packet.tx
            self.last_command = time.monotonic()
            self.send(w.RE01)
            return
        if not self.session or packet.session != self.session:
            raise ValueError("Wrong session")
        difference = (packet.tx - self.rx) & 65535
        if not 0 < difference < 32768:
            raise ValueError("Duplicate/out-of-order command")
        # Validate a complete command before advancing receive state.
        if packet.kind == w.ER02:
            if packet.body:
                raise ValueError("ER02 body must be empty")
        elif packet.kind in (w.ER03, w.ER04):
            if len(packet.body) != w.WORDS.size:
                raise ValueError("Invalid control body length")
            a, b = w.WORDS.unpack(packet.body)
            if packet.kind == w.ER03 and (a not in (0, 1) or not 100 <= b <= 1000):
                raise ValueError("Invalid streaming settings")
            if packet.kind == w.ER04 and (b not in (0, 1) or not 1 <= a <= 65535):
                raise ValueError("Invalid per-target control")
        elif packet.kind == w.ER05:
            if len(packet.body) != w.ACK.size:
                raise ValueError("Invalid frame acknowledgement")
            frame, count, result = w.ACK.unpack(packet.body)
            if result or packet.frame != frame or frame not in self.pending:
                raise ValueError("Unknown frame acknowledgement")
            expected = [t for t in self.pending[frame] if t.detected]
            if count != len(expected):
                raise ValueError("ECS applied count mismatch")
        else:
            raise ValueError("Unsupported ECS command")
        self.rx = packet.tx
        self.last_command = time.monotonic()
        if packet.kind == w.ER02:
            self.send(w.RE02, w.FRAME.pack(round(self.pan * 100), round(self.tilt * 100),
                                           0, int(self.enabled)))
        elif packet.kind == w.ER03:
            self.enabled, self.period_ms = bool(a), b
            self.dirty = True
            self.send(w.RE05, w.RESULT.pack(packet.tx, 0, a, b))
            self.log(f"ECS 전송 {'허용' if a else '중지'} / {1000 / b:g} Hz")
        elif packet.kind == w.ER04:
            if b:
                self.suppressed.discard(a)
            else:
                self.suppressed.add(a)
            self.dirty = True
            self.send(w.RE04, w.RESULT.pack(packet.tx, 0, a, b))
        elif packet.kind == w.ER05:
            self.last_ack_frame = frame
            self.last_ack_at = time.monotonic()
            self.confirmed = expected
            self.acked += 1
            while self.pending and next(iter(self.pending)) <= frame:
                self.pending.popitem(last=False)

    def emit_frame(self):
        if self.frame == 0xFFFFFFFF:
            self.enabled = False
            raise ValueError("Frame counter exhausted; reconnect ECS")
        targets = transmitted_targets(
            [t for t in self.targets.values() if t.id not in self.suppressed],
            self.input_frame, self.pan, self.tilt, self.calibration)
        self.frame += 1
        self.send(w.RE03, w.frame_body(targets, self.pan, self.tilt, self.period_ms), self.frame)
        self.pending[self.frame] = targets
        while len(self.pending) > 32:
            self.pending.popitem(last=False)
        self.sent += 1
        self.last_send = time.monotonic()
        self.dirty = False

    def run(self):
        while self.running:
            try:
                try:
                    data, source = self.sock.recvfrom(65536)
                except socket.timeout:
                    data = None
                with self.lock:
                    if data is not None:
                        try:
                            self.receive(data, source)
                        except (ValueError, OSError) as error:
                            self.errors += 1
                            self.log(str(error))
                    now = time.monotonic()
                    if self.session and now - self.last_command > 3.5:
                        self.retired.append(self.session)
                        self.session = 0
                        self.enabled = False
                        self.pending.clear()
                        self.log("ECS 응답 시간 초과: 전송 중지")
                    # GUI edits produce a fresh complete frame promptly; cap bursts at 20 Hz.
                    if self.enabled and (now - self.last_send >= self.period_ms / 1000 or
                                         (self.dirty and now - self.last_send >= .05)):
                        self.emit_frame()
            except OSError as error:
                if self.running:
                    with self.lock:
                        self.errors += 1
                        self.log(str(error))
                    time.sleep(.05)
            except Exception as error:
                with self.lock:
                    self.errors += 1
                    self.enabled = False
                    self.log(str(error))

    def snapshot(self):
        with self.lock:
            return dict(targets=list(self.targets.values()), pan=self.pan, tilt=self.tilt,
                        input_frame=self.input_frame,
                        period_ms=self.period_ms, connected=bool(self.session), enabled=self.enabled,
                        frame=self.frame, sent=self.sent, acked=self.acked, errors=self.errors,
                        confirmed=list(self.confirmed), last_ack_frame=self.last_ack_frame,
                        ack_age=time.monotonic() - self.last_ack_at if self.last_ack_at else None,
                        logs=list(self.logs), packet=self.last_packet)

    def close(self):
        self.running = False
        if self.thread:
            self.thread.join(timeout=1)
        self.sock.close()
