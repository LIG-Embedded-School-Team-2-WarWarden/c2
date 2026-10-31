"""Pan/tilt kinematics. Metres; right-handed +X forward, +Y left, +Z up.

Pan is positive left around +Z. Tilt is positive up, i.e. a rotation around -Y.
Offsets describe base->pan (base frame), pan->tilt (pan frame), tilt->sensor
(tilt frame). Mount rotation maps sensor axes into the zero-tilt bracket frame.
"""

from dataclasses import dataclass, replace
import math

SENSOR, FIXED = "sensor", "fixed"
IDENTITY = (1., 0., 0., 0., 1., 0., 0., 0., 1.)


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def subtract(a, b):
    return tuple(x - y for x, y in zip(a, b))


def rotate_z(p, angle):
    c, s = math.cos(angle), math.sin(angle)
    x, y, z = p
    return c*x-s*y, s*x+c*y, z


def rotate_y(p, angle):
    c, s = math.cos(angle), math.sin(angle)
    x, y, z = p
    return c*x+s*z, y, -s*x+c*z


def multiply(matrix, p):
    return tuple(sum(matrix[3*r+c]*p[c] for c in range(3)) for r in range(3))


@dataclass(frozen=True)
class Calibration:
    pan_origin: tuple = (0., 0., 0.)
    tilt_offset: tuple = (0., 0., 0.)
    sensor_offset: tuple = (0., 0., 0.)
    mount_rotation: tuple = IDENTITY

    def __post_init__(self):
        for name in ("pan_origin", "tilt_offset", "sensor_offset", "mount_rotation"):
            values = tuple(getattr(self, name))
            if len(values) != (9 if name == "mount_rotation" else 3) or not all(
                    math.isfinite(v) for v in values):
                raise ValueError("보정값은 유한한 3차원 벡터와 3×3 회전행렬이어야 합니다.")
            object.__setattr__(self, name, values)
        r = self.mount_rotation
        for a in range(3):
            for b in range(3):
                dot = sum(r[3*a+k]*r[3*b+k] for k in range(3))
                if abs(dot - int(a == b)) > 1e-6:
                    raise ValueError("센서 장착 회전행렬은 직교행렬이어야 합니다.")
        det = (r[0]*(r[4]*r[8]-r[5]*r[7]) - r[1]*(r[3]*r[8]-r[5]*r[6])
               + r[2]*(r[3]*r[7]-r[4]*r[6]))
        if abs(det-1) > 1e-6:
            raise ValueError("센서 장착 회전행렬은 오른손 회전(det=1)이어야 합니다.")

    def to_fixed(self, p, pan_deg, tilt_deg):
        q = add(multiply(self.mount_rotation, p), self.sensor_offset)
        q = add(rotate_y(q, -math.radians(tilt_deg)), self.tilt_offset)
        return add(rotate_z(q, math.radians(pan_deg)), self.pan_origin)

    def to_sensor(self, p, pan_deg, tilt_deg):
        q = subtract(rotate_z(subtract(p, self.pan_origin), -math.radians(pan_deg)),
                     self.tilt_offset)
        q = subtract(rotate_y(q, math.radians(tilt_deg)), self.sensor_offset)
        transpose = tuple(self.mount_rotation[3*c+r] for r in range(3) for c in range(3))
        return multiply(transpose, q)


def fixed_target(target, input_frame, pan, tilt, calibration):
    if input_frame == FIXED:
        return target
    if input_frame != SENSOR:
        raise ValueError("알 수 없는 입력 좌표계입니다.")
    return replace(target, **dict(zip(("x", "y", "z"),
        calibration.to_fixed((target.x, target.y, target.z), pan, tilt))))


def input_target(target, input_frame, pan, tilt, calibration):
    if input_frame == FIXED:
        return target
    if input_frame != SENSOR:
        raise ValueError("알 수 없는 입력 좌표계입니다.")
    return replace(target, **dict(zip(("x", "y", "z"),
        calibration.to_sensor((target.x, target.y, target.z), pan, tilt))))


def transmitted_targets(targets, input_frame, pan, tilt, calibration):
    # Quantize only after rotation/translation. Validate all before publishing anything.
    return [fixed_target(t, input_frame, pan, tilt, calibration).quantized() for t in targets]
