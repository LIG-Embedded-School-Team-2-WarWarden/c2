# Live FCS test station

[한국어 빌드·실행·IP 설정·테스트 가이드](QUICKSTART.md)

The standalone station uses manual Big Endian EF/FE packets and an unmodified upstream GUI.
The Python simulator and IPC regression client are included in this directory.

ECS maps motor north 180 degrees to GUI north 0 degrees at the display boundary:
`((motor - 180) % 360 + 360) % 360`. Motor commands and alignment checks use raw angles.
The upstream GUI's numeric label accepts this angle, but its direction graphic still
uses `Clamp(azimuth, 0, 270) - 135`. Its owner must update that graphic for compass bearings.
