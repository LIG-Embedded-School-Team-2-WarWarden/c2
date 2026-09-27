# C2 구현·시험 추적표

- 기준 문서: `DOC-REQ`, `MFS-ICD-001`
- 기준일: 2026-09-27
- 대상: CSCI-02 C2 서버와 교육용 더미 자산

## C2 소프트웨어 요구사항

| 요구사항 | 구현 | 검증 | 상태 |
|---|---|---|---|
| SW-C2-001 표적 수신·검증 | `ServerRuntime::ingest`, 프로토콜 검증 | `ProtobufCodecTest`, `StateStoreTest`, `ServerRuntimeTest` | 완료 |
| SW-C2-002 표적 상태 관리·전시 | `StateStore`, 콘솔 `targets` | 표적 최신성·용량·만료·정렬 테스트 | 코어/콘솔 완료, 정식 GUI TBD |
| SW-C2-003 표적 선택·할당 | 콘솔 `point TARGET_ID` | `EffectorCommandServiceTest`, E2E 시험 | 단일 운용자 선택 완료, 자동 할당 TBD |
| SW-C2-004 자산 위치·자세 관리 | source별 `AssetPose` 저장·재동기화 | `StateStoreTest`, `ConnectionMonitorTest` | 완료 |
| SW-C2-005 타격 자산 기준 변환 | PROJECT_FRAME 표적과 타격 `AssetPose` 상대변환 | `EffectorPointingTest` | 완료 |
| SW-C2-006 Pan/Tilt 계산 | 설치 방위각 반영 지향각 계산 | 방위·수직·한계·일치점 시험 | 완료 |
| SW-C2-007 지향·발사 명령 생성 | 지향/ARM/START/STOP/비상정지 서비스 | 서비스·상태기계 시험 | 완료 |
| SW-C2-008 UDP 명령 송신 | `UdpTransport`, ACK 재전송 | UDP loopback, 재시도·소진 시험 | 완료 |
| SW-C2-009 연결·두절 상태 표시 | Heartbeat monitor, 콘솔 `status` | 연결·단절·복구·재동기화 시험 | 코어/콘솔 완료, 정식 GUI TBD |
| SW-C2-010 관측 지향·탐색 명령 | `scan`, `observe`, `obs-stop`, `obs-home` | 관측 명령 서비스·더미 상태전이 시험 | 완료 |

## ICD 인터페이스 시험

| 시험 ID | 자동화 근거 | 결과 |
|---|---|---|
| ICD-TC-001 | 모든 메시지 Golden Packet 및 round-trip | 통과 |
| ICD-TC-002 | 숫자·enum·ID·시간 경계값 | 통과 |
| ICD-TC-003 | UDP loopback 송수신·재시작·최대크기 | 통과 |
| ICD-TC-004 | 표적 순번·중복·out-of-order | 통과 |
| ICD-TC-005 | 표적 유효시간 정확 경계 | 통과 |
| ICD-TC-006 | ACK 진행상태·중복 명령·제한 캐시 재생 방지 | 통과 |
| ICD-TC-007 | 양방향 Heartbeat 단절·복구 | 통과 |
| ICD-TC-008 | 실제 LiDAR 로컬→PROJECT_FRAME 변환 | 실제 관측 HW/SW 필요 |
| ICD-TC-009 | PROJECT_FRAME→타격 상대좌표·Pan/Tilt | 통과 |
| ICD-TC-010 | 탐지→선택→지향 E2E | 통과 |
| ICD-TC-011 | ARM→START→지속시간 종료 | 통과 |
| ICD-TC-012 | 통신 단절·비상정지 | 통과 |
| ICD-TC-013 | 자산별 AssetPose 저장·재동기화 | 통과 |
| ICD-TC-014 | 관측 SCAN/STOP/HOME 상태전이 | 통과 |
| ICD-TC-015 | 더미 자산 3프로세스 UDP 통합 | 통과 |
| ICD-TC-016 | ACK timeout 재전송·최대 시도 소진 | 통과 |
| ICD-TC-017 | ErrorReport 생성·수집·운용 조회 | 통과 |

## 의도적으로 남은 항목

다음 항목은 문서상 실제 장비 또는 팀 결정을 요구하므로 더미 구현 완료로 닫지 않는다.

- LiDAR SDK 연결, Calibration, 원시 데이터 처리와 실제 탐지 알고리즘
- 실제 Pan/Tilt 모터, Limit/Encoder/Feedback 및 레이저 출력 제어
- MFC/WPF 등 정식 GUI 기술과 화면 설계
- 실제 IP, 시간 동기화, 지향 오차와 End-to-End 성능 기준
- 자동 표적 할당 정책과 실제 공격 허가 조건

더미 자산은 인터페이스와 안전 상태전이를 검증하기 위한 시험 대역이며 실제 장비
알고리즘의 검증 완료 근거로 사용하지 않는다.
