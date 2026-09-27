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
