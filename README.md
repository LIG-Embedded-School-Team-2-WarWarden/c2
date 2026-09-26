# C2

C++20 command-and-control components for the MFS demonstration system.

The accepted coordinate contract is:

1. The observation asset converts a detected target into `PROJECT_FRAME` world coordinates.
2. It sends the world coordinate as `TargetCoordinate` to C2.
3. C2 uses the effector `AssetPose` to calculate the effector-relative vector and Pan/Tilt.
4. C2 sends the result as `EffectorTurretCommand`.

The versioned Protobuf contract is maintained in `protocol/mfs.proto`. See
`docs/decisions/ADR-001-project-frame-targets.md` for the coordinate-frame decision.
