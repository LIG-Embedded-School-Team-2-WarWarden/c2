# ADR-012: 수동 지향의 표적 ID 제거

수동 절대각 이동은 표적 지정과 별도 동작이다. EffectorTurretCommand에서 target_id를
제거하고 Protobuf 번호 3과 이름을 reserved로 유지한다. codec은 기존 field 3을 읽어도
표적 상태로 적용하지 않으며 새 송신에는 포함하지 않는다.

자산은 수동 지향 실행 시 기존 출력·출력 준비·추적·표적 스트림을 해제하고 각도를
적용한다. 명령 ID·세션·기한·기구 한계 검사 및 중복 결과 회신은 유지한다.
ARM/START는 현재 유효한 TargetTrackUpdate.track_id와 AttackCommand.target_id가
일치해야 한다. C2의 RoutedPointStore와 수동 지향 이력 기반 출력 허용 경로는 제거한다.
호환 단일 자산 경로에서도 보고된 tracking_track_id를 사용하며 수동 명령으로 표적을 지정하지 않는다.

검증: 서버 213개와 공용 프로토콜 31개 시험, 수동 이동 중 출력·추적 상태 해제,
수동 이동 후 ARM/START 거부, 만료 스트림 거부, 제거된 field 3 무시·송신 제외 Golden
Packet, CMake Release, 실제 2 OBS + 3 EFF UDP smoke, burst/faults probe.
구 자산 수신기는 ID 없는 수동 명령을 거부할 수 있으므로 C2와 자산을 함께 갱신한다.
정상 자동추적에는 TargetTrackUpdate를 계속 사용한다.
