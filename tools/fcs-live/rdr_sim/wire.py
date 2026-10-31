"""Local RDR/ECS draft v1. Explicit big-endian serialization; not the approved ICD."""

from dataclasses import dataclass
import math
import struct

ER01, ER02, ER03, ER04, ER05 = 0x0101, 0x0102, 0x0103, 0x0104, 0x0105
RE01, RE02, RE03, RE04, RE05 = 0x0201, 0x0202, 0x0203, 0x0204, 0x0205
HEADER = struct.Struct("!6HQIQ")  # 32 bytes
FRAME = struct.Struct("!hhHH")
TARGET = struct.Struct("!HHhhhH")
WORDS = struct.Struct("!HH")
RESULT = struct.Struct("!HHHH")
ACK = struct.Struct("!IHH")
NAMES = {value: name for name, value in list(globals().items()) if name.startswith(("ER0", "RE0"))}


@dataclass(frozen=True)
class Packet:
    kind: int
    tx: int
    rx: int
    session: int
    frame: int = 0
    captured_ms: int = 0
    body: bytes = b""

    def encode(self):
        length = HEADER.size + len(self.body)
        if length % 4 or not 32 <= length <= 160 or not self.session:
            raise ValueError("Invalid packet length/session")
        return HEADER.pack(self.kind, 1, length, 0, self.tx, self.rx, self.session,
                           self.frame, self.captured_ms) + self.body

    @classmethod
    def decode(cls, data):
        if not 32 <= len(data) <= 160 or len(data) % 4:
            raise ValueError("Invalid datagram length")
        kind, version, length, flags, tx, rx, session, frame, captured = HEADER.unpack_from(data)
        if version != 1 or length != len(data) or flags or not session or kind not in NAMES:
            raise ValueError("Invalid header")
        return cls(kind, tx, rx, session, frame, captured, data[HEADER.size:])


def quantize(value, scale=1000):
    if not math.isfinite(value):
        raise ValueError("좌표는 유한한 숫자여야 합니다.")
    encoded = round(value * scale)
    if not -32768 <= encoded <= 32767:
        raise ValueError("좌표 범위는 -32.768 ~ +32.767 m입니다.")
    return encoded


@dataclass(frozen=True)
class Target:
    id: int
    x: float
    y: float
    z: float
    detected: bool = True

    def encode(self):
        if not 1 <= self.id <= 65535:
            raise ValueError("표적 ID는 1~65535 범위입니다.")
        return TARGET.pack(self.id, int(self.detected), quantize(self.x), quantize(self.y),
                           quantize(self.z), 0)

    def quantized(self):
        self.encode()
        return Target(self.id, *(quantize(v) / 1000 for v in (self.x, self.y, self.z)),
                      self.detected)


def frame_body(targets, pan, tilt, period_ms):
    if len(targets) > 10 or len({t.id for t in targets}) != len(targets):
        raise ValueError("서로 다른 ID의 표적을 최대 10개까지 지원합니다.")
    if not (math.isfinite(pan) and math.isfinite(tilt) and -180 <= pan <= 180 and -90 <= tilt <= 90):
        raise ValueError("방위각 ±180°, 고각 ±90° 범위입니다.")
    if not 100 <= period_ms <= 1000:
        raise ValueError("전송 주기는 100~1000 ms입니다.")
    return FRAME.pack(quantize(pan, 100), quantize(tilt, 100), len(targets), period_ms) + b"".join(
        t.encode() for t in targets)


def parse_frame(body):
    if len(body) < FRAME.size:
        raise ValueError("Missing frame metadata")
    pan, tilt, count, period = FRAME.unpack_from(body)
    if count > 10 or len(body) != FRAME.size + TARGET.size * count:
        raise ValueError("Invalid frame size/count")
    targets = []
    for index in range(count):
        id_, detected, x, y, z, reserved = TARGET.unpack_from(body, FRAME.size + index * TARGET.size)
        if reserved or detected not in (0, 1):
            raise ValueError("Invalid target flags")
        targets.append(Target(id_, x / 1000, y / 1000, z / 1000, bool(detected)))
    frame_body(targets, pan / 100, tilt / 100, period)
    return targets, pan / 100, tilt / 100, period
