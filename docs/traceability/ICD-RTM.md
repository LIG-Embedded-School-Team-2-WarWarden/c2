# C2 구현·시험 추적표

- 기준 문서: `DOC-REQ`, `MFS-ICD-001`
- 갱신일: 2026-10-05
- 대상: CSCI-02 C2 서버와 교육용 다중 더미 자산

## 구현 항목 식별자

`SW-C2-*`는 Confluence SRS의 원래 의미를 유지한다. 아래 `C2-IMPL-*`는
서버 구현 항목이며 정식 요구사항을 새로 정의하거나 승인하지 않는다. 이전 버전의
로컬 표가 `SW-C2-001~014`를 다른 의미로 재사용한 문제를 이 구분으로 정정한다.
완료 표시는 서버/더미 시험 범위의 구현 상태이며 GUI·실장비·Gate 승인을 뜻하지 않는다.

## C2 구현 항목

| 구현 항목 | 구현 | 자동 검증 | 상태 |
|---|---|---|---|
| C2-IMPL-001 다중 자산 등록·세션 | `AssetRegistry`, 공용 `ServerUdpIngress` | 등록, lease, 해제, 세션 교체, source Endpoint 위조 거부 | 완료 |
| C2-IMPL-002 자산 위치·자세·상태 | 자산별 Pose/Heartbeat/EffectorStatus snapshot | 자산 독립성, timeout, 재동기화, 상태 최신성 | 완료 |
| C2-IMPL-003 표적 수신·전역 트랙 | `TrackStore`, 위치·속도·측정시각 보존 | 동일 detection ID 분리, 0속도/미제공 구분, NaN/Inf, replay/만료/용량 | 완료 |
| C2-IMPL-004 표적 상태 조회 | 콘솔 `targets` | 정렬, 원 관측 자산·detection ID 보존 | 코어/콘솔 완료, GUI 미정 |
| C2-IMPL-005 자동·수동 자산 할당 | `AssetAssignmentService` | 거리·회전·부하 점수, capability/상태/한계 필터, 동점 규칙 | 완료 |
| C2-IMPL-006 할당 상실·재할당 안전 | 공격 전 재할당, 공격 후 운용자 조치 latch | timeout/unregister/session 교체/공격 중 상실 | 완료 |
| C2-IMPL-007 표적 상태 라우팅·타격 지향 | C2 `TargetTrackUpdate` 라우팅, 타격 자산 dead reckoning/Pan·Tilt | 속도 회전, 예측 위치, 연속 지향, 구동 한계 | 완료 |
| C2-IMPL-008 관측 지향·탐색 | 자산별 SCAN/ABSOLUTE/STOP/HOME | 명령 검증, 상태전이, 중복 명령 | 완료 |
| C2-IMPL-009 공격·추적 분리 | 자산별 ARM/START/STOP/ESTOP, 상태 스트림 분리 | 출력시간 후 추적 지속, 명시적 정지, 표적/예측/Pose/통신 fail-safe | 완료 |
| C2-IMPL-010 명령 ACK 상태기계 | 자산·세션·command ID별 tracker | 진행/종결 ACK, 재전송, 만료, 완료 timeout, 경합 | 완료 |
| C2-IMPL-011 운용 상태·결과 조회 | `assets`, `status`, `errors`, `outcomes`, `events` | assignment/outcome/수신 거부 snapshot과 parser | 코어/콘솔 완료, GUI 미정 |
| C2-IMPL-012 런타임 설정 | `ServerOptions`, `make_server_runtime_config` | 기본값 일관성, 0/overflow/상호모순 거부 | 완료 |
| C2-IMPL-013 더미 다중 자산 | 이동 관측 더미, 고주기 타격 제어 루프 | 2 관측 + 3 타격 실제 프로세스 연속 추적 smoke | 완료 |
| C2-IMPL-014 개발용 위치·방위 설정 | `dev-pose`, `DevelopmentPoseCommand`, capability bit 3, 자산 적용·Pose 재보고 | codec/입력 경계, 두 역할 적용·중복·만료·세션·동작 중 거부, 라우팅·ACK·초기화 | 코어/콘솔·더미 완료, 실제 장비 수신 구현 필요 |

## 정식 요구사항과 구현 연결

기준: [Confluence SRS](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/491548),
[Confluence RTM](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/65697).
기존 요구사항·시험 ID와 승인 상태는 보존한다.

| 정식 요구사항 | 구현 항목 | 현재 검증 범위 및 남은 항목 |
|---|---|---|
| SW-C2-001 표적 보고 수신·검증 | C2-IMPL-003, 등록 기반 C2-IMPL-001 | 서버/UDP 검증 완료 |
| SW-C2-002 표적 관리·GUI 전시 | C2-IMPL-003/004 | 코어/콘솔 완료, GUI 구현·검증 필요 |
| SW-C2-003 표적 선택·할당 | C2-IMPL-005/006 | 코어/콘솔 완료, GUI 운용 흐름 검증 필요 |
| SW-C2-004 자산 위치·자세 관리 | C2-IMPL-002 | 서버/더미 완료, 실장비 연동 필요 |
| SW-C2-005 좌표변환 | C2-IMPL-007 | v3 정상 경로는 포인터 자산 책임; C2는 운동상태 전달, 더미 계산 검증 완료 |
| SW-C2-006 Pan/Tilt 목표각 계산 | C2-IMPL-007 | v3 정상 경로는 포인터 자산 책임; 실장비 지향 오차 검증 필요 |
| SW-C2-007 제어 명령 생성 | C2-IMPL-008/009/010 | 서버/더미 완료, 실장비 수신 구현 필요 |
| SW-C2-008 UDP 명령 송신 | C2-IMPL-008/009/010 | 실제 UDP 송수신·손실·중복·순서 변경 시험 완료 |
| SW-C2-009 자산 상태 GUI 전시 | C2-IMPL-002/011 | 코어/콘솔 완료, GUI 구현·검증 필요 |
| SW-C2-010 관측 SCAN/STOP/HOME | C2-IMPL-008 | 서버/더미 완료, 실장비 탐색 패턴·구동 검증 필요 |
| SW-C2-011 시간 동기화 상태·시각 편차 | 직접 대응 구현 없음 | 시간 동기화·편차 감시 미구현; timestamp 유효성 검사는 이 요구의 완료 근거가 아님 |
| SW-C2-014 개발 Pose (ICD 보충 식별자) | C2-IMPL-014 | IF-C2-ASSET-DEV-001, ICD-TC-020; SRS 정식 표의 추가·승인 여부는 별도 결정 |

C2-IMPL-012는 공통 런타임 설정, C2-IMPL-013은 시험 대역이며 이를 독립적인
정식 SRS 요구사항으로 간주하지 않는다. v3 책임 배분은 ICD 기준이며 SRS의 원래 ID를
조용히 다른 의미로 바꾸지 않는다.

## ICD 인터페이스 시험

| 시험 ID | 자동화 근거 | 결과 |
|---|---|---|
| ICD-TC-001 | 전체 메시지 Golden Packet 및 round-trip | 통과 |
| ICD-TC-002 | 숫자·enum·ID·시간 경계값, protocol v3 및 구버전 거부 | 통과 |
| ICD-TC-003 | UDP loopback 송수신·재시작·최대 datagram | 통과 |
| ICD-TC-004 | 등록 source IP + 광고 command port 라우팅 | 통과 |
| ICD-TC-005 | 미등록·Endpoint/session 불일치 패킷 거부 | 통과 |
| ICD-TC-006 | 등록 갱신·lease·해제·보존기간 정리 | 통과 |
| ICD-TC-007 | 자산별 Heartbeat 단절·복구·Pose 재동기화 | 통과 |
| ICD-TC-008 | 관측별 detection ID→전역 track ID와 위치·속도 보존 | 통과 |
| ICD-TC-009 | C2 라우팅→타격 dead reckoning→상대좌표·Pan/Tilt | 통과 |
| ICD-TC-010 | 자동/수동 할당과 독점 자산 이중할당 방지 | 통과 |
| ICD-TC-011 | 탐지→자동 할당→상태 스트림→ARM→START→출력 종료 후 추적 | 통과 |
| ICD-TC-012 | ACK 진행상태·중복·timeout·재전송·소진, 수락 기한과 완료 기한 분리, 늦은 ACK 거부 | 통과 |
| ICD-TC-013 | session 교체 pending 종료와 늦은 패킷 거부 | 통과 |
| ICD-TC-014 | 특정/전체 STOP·ESTOP와 반복 ID 불변 | 통과 |
| ICD-TC-015 | 관측 SCAN/STOP/HOME 상태전이 | 통과 |
| ICD-TC-016 | 저장소/할당/ACK 경합 동시성 | 통과 |
| ICD-TC-017 | 잘못된 실행 설정과 용량 상한 | 통과 |
| ICD-TC-018 | 관측 2 + 타격 3 UDP, 이동표적 연속 추적, 자산 재시작 session 교체 | 통과 |
| ICD-TC-019 | 관측 자산의 LiDAR 로컬 위치·속도→PROJECT_FRAME 변환 및 장착 오프셋 반영 | 실제 관측 HW/SW 필요 |
| ICD-TC-020 | `DevelopmentPoseTest`, `ServerRuntimeDevelopmentPoseTest`, 콘솔 parser 및 두 역할 실제 UDP 설정·재보고 smoke; 계약은 `docs/icd/development-pose-command.md` | 통과 |

## 운영 기록과 시험 기준선

- 2026-10-06 수동 지향 변경: `EffectorTurretCommand.target_id` 제거, field 3 reserved.
  수동 이동은 기존 출력·추적을 해제하며 출력 대상 지정 근거가 아니다.
  계약은 `docs/icd/manual-pointing-command.md`, 결정은 C2-ADR-012를 따른다.
  변경 후 서버 213 + 공용 31 = 244 시험, 실제 UDP smoke와 burst/faults probe 통과.
  아래 commit·CI 숫자는 변경 전 병합 기준선의 기록이다.

- C2 기준 commit: `9af1529a2cfa19ef11b13bcd096dfb3af4ad797f`
- 공용 프로토콜 gitlink: `711d071df742e251dd4d9304fb2c0baf1c65379c`
- 서버 213 + 공용 프로토콜 30 = 243 자동 시험 통과. 공용으로 이동한 시험을 서버 수에 중복 집계하지 않는다.
- [CI 근거](https://github.com/LIG-Embedded-School-Team-2-WarWarden/c2/actions/runs/37289261282): CMake/CTest, VS Release, 다중 프로세스 UDP smoke와 부하·장애 probe.
- `EventSink`/`EventLog`의 회전 JSONL 운영 기록과 쓰기 오류 metrics는 구현되어 있다.
  보안 감사, fsync 내구성, 프로세스 상태 복원이나 실장비 안전 승인과는 별도다.
- START 필수 스트림 준비 실패는 START를 pending으로 남기지 않는다. 전송 예외는
  전달 불확실 상태로 기록하여 제한된 retry를 유지하고, 자산별 실패를 격리한다.

## 의도적으로 남은 항목

- 실제 LiDAR SDK, 장착 오프셋을 포함한 좌표변환, 실측 calibration 값,
  원시 데이터 처리와 탐지 알고리즘
- 실제 Pan/Tilt 모터, limit/encoder/feedback 및 레이저 출력 제어
- 정식 GUI와 C2 프로세스 간 API/IPC 계약, 화면 및 운용 승인 흐름
- 암호학적 자산 인증, 전송 보안, 키 관리, 권한 분리와 위변조 방지·내구성·접근 통제를 갖춘 보안 감사 체계
- 배포별 실제 IP, 시간 동기화, 지향 오차와 End-to-End 성능 승인 기준
- 실제 교전 규칙과 사람의 공격 승인 조건

더미 자산은 인터페이스와 안전 상태전이를 검증하기 위한 시험 대역이며 실제 장비
알고리즘이나 무기 안전성 검증 완료 근거로 사용하지 않는다.
