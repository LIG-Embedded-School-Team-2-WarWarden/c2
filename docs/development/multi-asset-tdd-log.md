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
