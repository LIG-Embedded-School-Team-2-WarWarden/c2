# ADR-001: TargetCoordinate uses PROJECT_FRAME

## Status

Accepted

## Decision

The observation asset converts sensor- and turret-local detections into the common
`PROJECT_FRAME` before transmitting `TargetCoordinate` to C2.

C2 validates and stores the received world coordinate without applying the observation
asset pose again. C2 uses the effector `AssetPose` to convert the world coordinate into
an effector-relative vector and calculates the target Pan/Tilt angles.

Both observation and effector assets send `AssetPose` in `PROJECT_FRAME`. The observation
pose remains useful for initialization, display, diagnostics, and consistency checks; it
is not part of the normal target-coordinate conversion path.

Messages with an unspecified or unsupported coordinate frame are rejected.

## Consequences

- Sensor calibration and local-to-world conversion belong to the observation asset.
- C2 is independent of observation sensor geometry.
- C2 must not perform Observation Frame to Project Frame conversion a second time.
- Effector pointing requires a valid effector `AssetPose`.
