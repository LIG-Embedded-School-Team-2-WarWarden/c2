# 다중 자산 TDD 기록

## Cycle 1 — 자산·세션 식별 및 등록 메시지

- Red: `ProtocolHeaderTest.RequiresAssetAndSessionIdentityForEveryAssetRoute`,
  `ProtobufCodecTest.RoundTripsAssetRegistrationAndUnregister`,
  `ProtobufCodecTest.RejectsRegistrationWithoutIdentityOrWithInvalidContract`를 먼저
  추가했다. `MessageHeader::asset_id/session_id`, `AssetRegistration`, `AssetRole`이
  없어 Release 빌드가 컴파일 실패하는 것을 확인했다.
- Green: 프로토콜을 v2로 올리고 헤더 식별자, 등록·해제 메시지, capability와
  Pan/Tilt 한계 모델, Protobuf codec 및 계약 검증을 최소 구현했다.
- Refactor: 공통 헤더 codec에 식별자 처리를 모으고 등록의 역할-`source_id`, 포트,
  capability, 버전, 한계와 lease 검증을 단일 validator로 정리했다.
- 검증: `MSBuild c2.slnx /m /p:Configuration=Release /p:Platform=x64`,
  `x64/Release/C2.Tests.exe` — 128/128 통과.

## Cycle 2 — 동적 자산 레지스트리와 세션 교체

- Red: 다중 역할 등록, 결정적 목록, 새 세션 교체, 이전 세션 패킷 거부,
  미등록·Endpoint 불일치 거부, lease·보존·용량 정책 테스트를 추가했다.
  `c2/asset_registry.hpp`가 없어 Release 빌드가 실패하는 것을 확인했다.
- Green: `asset_id` 키의 bounded registry, 등록·갱신·세션 교체·해제, 실제 송신
  Endpoint 검증, lease 만료, 종료 정보 보존·정리와 Pose 저장을 구현했다.
- Refactor: `std::map`으로 목록 순서를 결정적으로 만들고 모든 외부 반환을 snapshot으로
  복사해 mutex 밖에서 사용하게 했다. 동시 등록은 `std::latch`로 시작점을 맞춘 8개
  스레드 시험으로 검증했다.

## Cycle 3 — 자산별 Heartbeat 상태

- Red: 자산 ID로 연결 상태를 조회하는 테스트를 추가하고 `connection_state` API 부재로
  컴파일 실패하는 것을 확인했다.
- Green: C2 수신 시각과 자산별 마지막 Heartbeat를 사용해 연결·단절을 독립 판정했다.
- Refactor: snapshot과 직접 조회가 같은 `state_locked` 계산을 공유하게 했다. 중복·역전
  Heartbeat 및 Pose가 최신 상태를 덮어쓰지 않는 회귀시험을 추가했다.
- 검증: registry 시험 8개 통과, 전체 시험 134개 통과.

## Cycle 4 — 등록 기반 서버 수신과 Endpoint 인증

- Red: 같은 IP를 쓰는 두 자산의 독립 등록, 광고된 명령 포트, 미등록 패킷,
  Endpoint 불일치, 세션 교체, 이전 세션 거부 및 Pose 재동기화 초기화를 검증하는
  `ServerRuntimeRegistrationTest`를 먼저 추가했다. runtime registry 설정,
  송신 Endpoint를 받는 `ingest`, 자산 snapshot API가 없어 Release 빌드가 실패했다.
- Green: `AssetRegistry`를 `ServerRuntime`에 연결하고 자산 발신 Heartbeat, Pose,
  Target, status, ACK, error가 기존 상태를 변경하기 전에 `asset_id`, `session_id`,
  역할과 실제 송신 Endpoint를 인증하도록 구현했다. 등록·해제는 registry에서 직접
  처리한다.
- Refactor: 기존 인프로세스 회귀시험용 2-인자 수신 API는 호환 경계로 유지하고,
  인증이 끝난 메시지는 기존 저장 경로를 재사용해 상태 갱신 규칙의 중복을 피했다.
- 검증: `MSBuild.exe c2.slnx /m /p:Configuration=Release /p:Platform=x64` 성공,
  `x64\\Release\\C2.Tests.exe` 139/139 통과.

## Cycle 5 — 다중 관측 표적과 전역 track_id

- Red: 두 관측 자산이 같은 `detection_id`를 보내는 경우, 원본 자산·세션 보존,
  track ID 순서, 갱신·중복·역전, 만료, 전체·관측자별 용량, ID 순환 테스트를 먼저
  추가했다. `track_store.hpp` 부재로 Release 빌드가 실패했다.
- Green: 복합 원본 키와 64비트 전역 ID를 사용하는 bounded `TrackStore`를 구현했다.
  만료 정리와 조회는 mutex 아래 원자적으로 처리하고 외부에는 snapshot만 반환한다.
- Red/Green 통합: 등록 기반 runtime에서 같은 detection ID 두 개를 수신하는 테스트가
  `tracks` 설정/API 부재로 실패한 뒤, 인증된 Target만 TrackStore에 반영하도록 연결했다.
- Refactor: 원본 키 index와 track ID 정렬 map을 분리해 갱신 조회는 유지하면서 반환
  순서를 결정적으로 만들었다. ID 발급은 0 및 활성 ID를 건너뛴다.
- 검증: Release 전체 빌드 성공, `x64\\Release\\C2.Tests.exe` 145/145 통과.

## Cycle 6 — 자산·세션별 pending 명령과 ACK 상태기계

- Red: 같은 command ID의 자산·세션 격리, 이전 세션 ACK 거부, 진행 ACK 유지,
  종결 ACK, 전달 재전송, completion timeout, 명령 만료, 재시도 소진, 전체·자산별
  용량과 이력 상한 테스트를 먼저 추가했다. `command_tracker.hpp` 부재로 빌드 실패를
  확인했다.
- Green: 복합 명령 키와 `awaiting_delivery`/`awaiting_completion` 상태를 갖는
  `CommandTracker`를 구현했다. 재전송은 동일 datagram을 반환하며 각 종료 원인을
  bounded outcome 이력에 보존한다.
- Refactor: 상태 판정과 종결 결과 매핑을 분리하고, `poll`은 잠금 아래에서 snapshot만
  만든 뒤 호출자가 네트워크 송신을 수행할 수 있게 했다.
- 검증: command tracker 시험 6/6 통과.

## Cycle 7 — 동적 관측 자산 명령 라우팅

- Red: 등록 자산의 광고 Endpoint 라우팅, 명령 헤더의 자산·세션 식별, Pose 동기화
  차단, 진행/최종 ACK, 새 세션의 이전 pending 종료 테스트를 먼저 추가했다. 자산 ID를
  받는 명령 API가 없어 Release 빌드 실패를 확인했다.
- Green: registry snapshot 기반 관측 명령 API를 추가하고 `CommandTracker`를 runtime에
  연결했다. 새 세션 등록과 unregister는 해당 이전 세션 pending을 종료한다.
- Refactor: 명령을 tracker에 먼저 등록한 뒤 송신해 즉시 ACK와의 경쟁을 막고, retry의
  송신 callback은 tracker 잠금 밖에서 호출한다. 기존 단일 자산 API는 회귀 호환 경계로
  유지했다.
- 검증: server runtime 관련 시험 15/15 통과.

## Cycle 8 — 자동 할당 후보 필터와 점수

- Red: 연결, Pose, status, fault, capability, Pan/Tilt, 공격·배타 작업 필터와 거리,
  회전량, 가중치 변경, 작업·degraded·실패 penalty, 결정적 동점 테스트를 먼저 추가했다.
  `asset_assignment.hpp` 부재로 Release 빌드 실패를 확인했다.
- Green: registry/track snapshot만 입력받는 순수 후보 정렬 및 선택 함수를 구현했다.
- Refactor: 안전 후보 판정, 최단 각도, 가중치 검증을 작은 함수로 분리하고 점수 결과에
  거리·회전량·계산된 목표 Pan/Tilt를 함께 보존했다.
- 검증: assignment scoring 시험 6/6 통과.

## Cycle 9 — 원자적 할당, 수동 지정 및 재할당 정책

- Red: 배타 자산 중복 점유, latch 기반 동시 할당 경쟁, 수동 지정 안전검사, 공격 전
  단절 재할당, 공격 중 자동 인계 금지, active attack unassign 거부, 완료 상태,
  `no_candidate`와 `temporarily_unavailable` 구분 테스트를 먼저 추가했다. 서비스 API
  부재로 Release 빌드 실패를 확인했다.
- Green: 선택과 점유를 단일 임계구역에서 처리하는 `AssetAssignmentService`와 할당
  상태 전이를 구현했다.
- Refactor: 활성 점유 계산을 서비스 내부 snapshot에 합산해 호출자가 전달한 stale
  작업 수만 신뢰하지 않게 했고, 자산·세션이 모두 일치할 때만 단절 상태를 전파한다.
- 검증: assignment service 시험 6/6 통과.

## Cycle 10 — 타격 자산별 상태와 freshness

- Red: 여러 타격 자산의 `EffectorStatus`가 서로 덮어쓰이지 않고, 중복·역전 메시지를
  거부하며, 수신 시각 기준 freshness가 자산별로 달라지는 테스트를 먼저 추가했다.
  registry 설정과 상태 갱신 API가 없어 Release 빌드 실패를 확인했다.
- Green: 상태를 자산 registry entry에 저장하고 자산·세션·역할·송신 Endpoint를
  검증한 뒤에만 갱신하도록 구현했다. 새 세션 등록, 명시적 해제, lease 만료 시에는
  이전 상태를 제거한다.
- Refactor: snapshot 생성 시 설정된 상태 timeout으로 `status_current`를 계산해
  할당 정책이 수신 시각이나 전역 상태 저장소를 직접 해석하지 않도록 했다.
- 검증: asset registry 시험 10/10 통과.

## Cycle 11 — 런타임 다중 타격 자산 할당

- Red: 등록된 두 타격 자산 중 건강하고 가까운 자산 자동 선택, 안전 조건을 위반한
  수동 지정 거부, unregister 시 할당 상실, heartbeat timeout 후 생존 자산 재할당
  테스트를 먼저 추가했다. runtime 설정과 할당 API가 없어 Release 빌드 실패를
  확인했다.
- Green: runtime이 registry의 자산별 Pose와 `EffectorStatus` snapshot으로 후보를
  구성하고 `AssetAssignmentService`에 자동·수동 할당을 위임하도록 연결했다.
  unregister와 session 교체는 해당 자산의 할당을 즉시 unavailable로 전환한다.
- Refactor: 기존 할당을 반환하기 전에 동일 안전 필터로 재검증하고, 공격 시작 전
  heartbeat timeout·상태 노후화·고장 등으로 부적합해진 경우 다른 자산을 선택한다.
  후보 snapshot 구성은 공통 함수로 분리했다.
- 검증: Release 전체 빌드 성공, 전체 시험 169/169 통과.

## Cycle 12 — 타격 명령의 64비트 전역 track 식별자

- Red: 32비트 범위를 넘는 전역 `track_id`를 POINT와 ATTACK 명령으로 codec
  round-trip했을 때 값이 6으로 잘리는 테스트 실패를 먼저 확인했다.
- Green: 두 명령의 `target_id`를 C++ 모델과 Protobuf 계약에서 `uint64`로 통일하고
  decoder의 32비트 축소 변환을 제거했다.
- Refactor: 기존 detection ID 기반 호환 서비스는 32비트 범위를 명시적으로 검사해
  조용한 truncation 대신 `target_unavailable`을 반환하도록 했다.
- 검증: Release 전체 빌드 성공, 전체 시험 170/170 통과.

## Cycle 13 — 할당 자산별 POINT와 공격 안전 라우팅

- Red: POINT가 할당된 자산의 Endpoint와 세션으로 전송되는지, POINT 없는 ARM 거부,
  READY/ALIGNED 및 armed 상태 검사, START 후 unassign 거부, 공격 중 단절 시
  `operator_action_required`, 단절 자산 STOP 전송 테스트를 먼저 추가했다.
- Green: 전역 track과 할당 snapshot으로 지향각을 계산하고 자산·세션별 pending 명령을
  생성했다. ARM/START는 현재 등록 세션, 연결, Pose, status, 지향 명령 만료와 target
  일치를 모두 검증한다.
- Refactor: track별 마지막 지향 명령을 bounded assignment 수명에 결합하고 안전 해제 시
  제거한다. 공격 시작 상태는 START 명령 등록 성공 뒤에만 전환한다.

## Cycle 14 — 보존 자산 STOP과 ESTOP

- Red: unregister 자산이 보존 기간 동안 조회되는지, 특정 자산 ESTOP과 `estop-all`이
  보존 Endpoint로 반복 전송되며 반복 패킷의 command ID가 같은지 테스트했다.
- Green: 역할별 `known_assets` snapshot과 특정 자산/전체 자산 비상정지 API를 구현했다.
  보존 기간이 끝난 자산은 자동 정리돼 이후 전체 비상정지 대상에서 제외된다.
- Refactor: STOP과 ESTOP의 헤더 생성, 세션 지정, 반복 송신을 공통 안전 명령 경로로
  통합했다. STOP은 ACK 추적하고 ESTOP은 동일 datagram을 설정 횟수만큼 즉시 보낸다.
- 검증: Release 전체 빌드 성공, 전체 시험 172/172 통과.

## Cycle 15 — 다중 자산 콘솔 문법

- Red: 자산·표적 조회, 자산 ID 기반 관측 명령, 자동/수동 할당, 해제, POINT,
  ARM/START, 자산별 STOP/ESTOP, ESTOP-ALL 문법과 잘못된 인수 거부 테스트를 먼저
  추가하고 parser 헤더 부재로 빌드 실패를 확인했다.
- Green: 입력 문자열을 네트워크와 무관한 typed `ConsoleCommand`로 변환하는 parser를
  구현하고 서버 콘솔을 동적 runtime API에 연결했다.
- Refactor: 64비트 ID, 유한 각도, 양수 duration, 잉여 인수를 parser 경계에서
  일관되게 검증한다. `assets`, `targets`, `status`, `errors` 출력에 자산·세션과 전역
  track 원본 정보를 포함했다.

## Cycle 16 — 실제 송신 Endpoint 기반 UDP ingress와 Heartbeat

- Red: loopback UDP 등록 패킷의 실제 source IP/port가 registry에 저장되는 통합 테스트와
  등록 자산별 command Endpoint·session으로 C2 Heartbeat가 전송되는 테스트를 먼저
  추가했다. ingress 타입 부재 및 고정 Endpoint Heartbeat로 실패를 확인했다.
- Green: 예외 격리된 `ServerUdpIngress`를 추가하고 서버에 공용 자산 수신 포트를
  연결했다. 등록 자산이 있으면 각 자산 snapshot으로 Heartbeat를 라우팅한다.
- Refactor: 수신 시각은 주입 가능한 clock으로 얻고 실제 UDP source를 secure runtime
  ingest에 그대로 전달한다. 등록 자산이 없는 동안은 기존 고정 Endpoint heartbeat를
  호환 경계로 유지한다. 역할별 기존 UDP 포트와 단일 자산 콘솔 문법도 더미 자산
  전환이 완료될 때까지 명시적 migration 경계로 분리했다.
- 검증: Release 전체 빌드 성공, loopback 통합시험과 기존 프로세스 smoke test 포함
  전체 179/179 통과.

## Cycle 17 — 더미 자산·세션 격리

- Red: non-default 자산·세션으로 생성한 더미가 모든 Pose/status/Heartbeat/target/ACK에
  동일 ID를 유지하고, 다른 자산 또는 세션 대상 명령과 C2 Heartbeat를 거부하는
  테스트를 먼저 추가했다. 기존 구현이 헤더를 1/1로 되돌리고 잘못된 명령을 실행하는
  실패를 확인했다.
- Green: 생성 Pose의 identity를 더미 내부 불변 상태로 저장하고 모든 발신 헤더에
  적용했다. 명령 deduplication보다 먼저 목적 자산·세션을 검사한다.
- Refactor: 관측/타격 더미의 watchdog Heartbeat에도 같은 identity 검증을 적용했다.

## Cycle 18 — 다중 더미 프로세스 동적 등록

- Red: 기존 프로세스 smoke를 관측 2대와 타격 3대, 공용 등록 포트, 동적 command
  port, 전역 track 2개, 자동 할당과 ESTOP-ALL 시나리오로 확장했다.
- Green: `--asset-id` 동적 모드에서 새 session ID 생성, port 0의 실제 할당 포트 광고,
  주기적 등록 갱신, Pose/status/Heartbeat/target 공용 포트 전송과 종료 unregister를
  구현했다. 위치·자세·capability·구동 한계·lease 설정을 CLI로 외부화했다.
- Refactor: `--asset-id`가 없는 기존 실행은 명시적 legacy migration 모드로 유지한다.
- 검증: Release 전체 빌드 성공, 전체 시험 180/180 및 관측 2/타격 3 실제 프로세스
  UDP smoke test 통과. 동일 asset ID의 타격 프로세스를 종료·재시작해 서로 다른
  session으로 교체되는 것도 확인했다.

## Cycle 19 — POINT 자동 할당

- Red: 사전 `assign` 없이 `point TRACK_ID`를 호출하면 가장 가까운 건강한 자산을
  자동 할당하고 그 Endpoint로 지향 명령을 보내는 테스트로 변경해 기존 고정 자산
  fallback 실패를 확인했다.
- Green: 유효한 전역 track이지만 할당이 없으면 runtime이 원자적 자동 할당을 먼저
  수행하고 동일 호출에서 POINT를 전송하도록 했다.
- Refactor: legacy detection ID는 TrackStore에 없으므로 기존 단일 자산 지향 경로로
  계속 분기된다. 다중 프로세스 smoke에서 명시적 assign을 제거해 자동 경로를 검증했다.
- 검증: Release 전체 빌드, 전체 시험 180/180, 다중 프로세스 smoke 통과.

## Cycle 20 — 운용 상태와 명령 결과 조회

- Red: 기본 할당이 지향 capability만 가진 자산을 허용하는 문제, 전체 할당과 명령
  결과를 runtime에서 조회할 수 없는 문제, ACK와 재시도 소진 경합 테스트를 먼저
  추가해 API 부재로 빌드 실패를 확인했다.
- Green: 기본 할당 capability를 POINT+ATTACK으로 강화하고 결정적 할당 snapshot과
  bounded 명령 outcome snapshot을 공개했다. `assets`, `status`, `outcomes` 콘솔 출력에
  자산 상태·할당·종결 사유를 연결했다.
- Refactor: 연결과 command terminal state를 숫자 대신 운용자가 읽을 수 있는 이름으로
  표시하고 프로세스 smoke의 상태 검증을 새 출력 계약에 맞췄다.
- 검증: Release 전체 빌드, 전체 시험 184/184, 다중 프로세스 smoke 통과.

## Cycle 21 — 다중 자산 런타임 설정 일원화

- Red: 실행 옵션을 바꿔도 동적 TrackStore/AssetRegistry/CommandTracker에는 기본값이
  남는 문제를 옵션→runtime config 전파 테스트로 고정했다. parser 모듈 부재로 빌드
  실패를 확인했다.
- Green: `ServerOptions` 파서와 config factory를 코어로 분리하고 자산·트랙·상태·명령·
  할당 용량과 timeout을 모두 외부화했다.
- Refactor: 0, 시간 변환 overflow, 전체 상한보다 큰 자산별 상한을 서버 시작 전에
  일관되게 거부하고 레거시/동적 경로의 공통 옵션값을 한 곳에서 생성한다.
- 검증: Release 전체 빌드, 전체 시험 187/187, 다중 프로세스 smoke 통과.

## Cycle 22 — 할당 정책과 오류 이력 실행 설정

- Red: 점수 가중치와 자동 재할당 정책이 실행 옵션에 없고 runtime 오류 이력 상한이
  고정된 문제를 config 전파, 재할당 비활성화, bounded 오류 이력 테스트로 재현했다.
- Green: 다섯 점수 가중치, `--auto-reassign`, `--max-error-history`를 옵션과 runtime
  config에 연결했다. 자동 재할당을 끄면 상실한 할당을 유지해 운용자 판단을 기다린다.
- Refactor: 유한한 0 이상 실수와 명시적 true/false만 parser 경계에서 허용하고
  TelemetryStore 용량도 runtime 생성 시 주입한다.
- 검증: Release 전체 빌드, 전체 시험 189/189, 다중 프로세스 smoke 통과.
