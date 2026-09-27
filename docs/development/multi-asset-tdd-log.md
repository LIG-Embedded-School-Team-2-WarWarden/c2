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
