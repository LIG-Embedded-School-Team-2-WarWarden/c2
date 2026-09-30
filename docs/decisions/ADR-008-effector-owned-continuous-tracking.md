# ADR-008: Effector-owned continuous target tracking

## Status

Accepted — supersedes the C2-owned Pan/Tilt portion of ADR-001.

## Context

An effector must continue to point at a moving target between surveillance reports and
after the finite attack-output interval. A one-shot C2 Pan/Tilt command cannot express
that behavior safely. It also makes the C2 depend on effector-local geometry and control
timing.

## Decision

- A surveillance/observation asset sends `TargetCoordinate` in `PROJECT_FRAME` with
  position in metres, velocity in metres per second, measurement time, confidence, and
  an explicit `velocity_valid` flag.
- C2 owns global track identity, freshness, assignment, authorization, and routing. It
  preserves the latest motion state without predicting or converting it to Pan/Tilt.
- C2 sends `TargetTrackUpdate` only to the currently assigned effector asset/session.
  This stream is state data and does not enter the command ACK/retry state machine.
- The effector combines the motion state with its local pose, dead reckons to the current
  time, computes relative geometry and Pan/Tilt, and runs the continuous control loop.
- `START.duration_ms` controls attack output only. Automatic tracking remains active
  after that interval until STOP/ESTOP or a fail-safe condition.
- A stationary target is valid velocity with three zeros. Unavailable velocity is not
  stationary and cannot enable automatic continuous tracking.
- Expired or future measurements, excessive prediction age, stale pose, communication
  loss, non-finite values, replay, track mismatch, and turret-limit violations stop
  attack output and automatic tracking. Status reports the track, predicted position,
  target age, active flag, and stop reason.
- `EffectorTurretCommand` remains only as a manual/diagnostic compatibility path.

## Compatibility

The wire protocol version is 3. New fields were appended where compatible, and old
message and enum identifiers were retained. Version 2 peers are rejected at the protocol
boundary instead of being silently treated as stationary targets.

## Consequences

C2 no longer needs effector-specific pointing math in the automatic engagement path.
Each effector is responsible for its own control-loop rate, pose freshness, mechanical
limits, actuator feedback, and final hardware safety enforcement.
