# 수동 절대각 지향과 표적 추적의 분리

`EffectorTurretCommand`는 각도 이동만 요청한다. `target_id`를 제거하고 기존
Protobuf field 3과 이름을 reserved로 보존한다. 다른 필드 번호는 변경하지 않는다.

| 필드 | 의미 |
|---|---|
| header | 대상 자산·세션, 메시지 순번·생성 시각 |
| command_id | 개별 요청 식별자; 재전송은 동일 ID |
| target_pan_deg | 목표 수평각, degree |
| target_tilt_deg | 목표 수직각, degree |
| valid_until_us | 명령 수신·수락 기한, UTC μs |

수동 명령은 유효한 ID·세션·각도·기한을 검사한다. 실행 시 이전 자동추적·출력·출력
준비를 해제하고 대상 트랙을 지운 뒤 각도를 적용한다. 중복 명령은 결과만 재회신한다.
수동 이동만으로 ARM/START의 표적 조건을 충족하지 않는다.

자동추적은 `TargetTrackUpdate.track_id`를 사용하고 ARM/START는
`AttackCommand.target_id`와 유효한 표적 스트림의 track_id가 일치해야 한다.
C2도 수동 지향 이력으로 표적 일치 검사를 우회하지 않는다. STOP/ESTOP은 표적 없이
허용하는 기존 안전 경로를 유지한다.

Protocol v3의 제거된 필드는 새 수신기에서 무시한다. 구 수신기는 target_id 없는
명령을 거부할 수 있으므로 자산과 C2를 함께 갱신해야 한다. field 3은 재사용하지 않는다.
AttackCommand의 표적 ID와 자동추적 track_id는 제거하지 않는다.
