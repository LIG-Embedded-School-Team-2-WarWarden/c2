# C2 구현·시험 추적표

- 기준 문서: `DOC-REQ`, `MFS-ICD-001`
- 갱신일: 2026-09-28
- 대상: CSCI-02 C2 서버와 교육용 다중 더미 자산

## C2 소프트웨어 요구사항

| 요구사항 | 구현 | 자동 검증 | 상태 |
|---|---|---|---|
| SW-C2-001 다중 자산 등록·세션 | `AssetRegistry`, 공용 `ServerUdpIngress` | 등록, lease, 해제, 세션 교체, source Endpoint 위조 거부 | 완료 |
| SW-C2-002 자산 위치·자세·상태 | 자산별 Pose/Heartbeat/EffectorStatus snapshot | 자산 독립성, timeout, 재동기화, 상태 최신성 | 완료 |
| SW-C2-003 표적 수신·전역 트랙 | `TrackStore` | 동일 detection ID 분리, replay/만료/전체·자산별 용량 | 완료 |
| SW-C2-004 표적 상태 조회 | 콘솔 `targets` | 정렬, 원 관측 자산·detection ID 보존 | 코어/콘솔 완료, GUI 미정 |
| SW-C2-005 자동·수동 자산 할당 | `AssetAssignmentService` | 거리·회전·부하 점수, capability/상태/한계 필터, 동점 규칙 | 완료 |
| SW-C2-006 할당 상실·재할당 안전 | 공격 전 재할당, 공격 후 운용자 조치 latch | timeout/unregister/session 교체/공격 중 상실 | 완료 |
| SW-C2-007 타격 기준 변환·Pan/Tilt | PROJECT_FRAME과 할당 자산 Pose 상대변환 | 방위·수직·설치 방위각·구동 한계·일치점 | 완료 |
| SW-C2-008 관측 지향·탐색 | 자산별 SCAN/ABSOLUTE/STOP/HOME | 명령 검증, 상태전이, 중복 명령 | 완료 |
| SW-C2-009 POINT/공격 안전명령 | 자산별 POINT/ARM/START/STOP/ESTOP | Pose/상태/정렬/무장/표적 일치, 공격 latch | 완료 |
| SW-C2-010 명령 ACK 상태기계 | 자산·세션·command ID별 tracker | 진행/종결 ACK, 재전송, 만료, 완료 timeout, 경합 | 완료 |
| SW-C2-011 운용 상태·결과 조회 | `assets`, `status`, `errors`, `outcomes` | assignment/outcome snapshot과 parser | 코어/콘솔 완료, GUI 미정 |
| SW-C2-012 런타임 설정 | `ServerOptions`, `make_server_runtime_config` | 기본값 일관성, 0/overflow/상호모순 거부 | 완료 |
| SW-C2-013 더미 다중 자산 | 동적 ID/session/port 등록 관측·타격 더미 | 2 관측 + 3 타격 실제 프로세스 smoke | 완료 |

## ICD 인터페이스 시험

| 시험 ID | 자동화 근거 | 결과 |
|---|---|---|
| ICD-TC-001 | 전체 메시지 Golden Packet 및 round-trip | 통과 |
| ICD-TC-002 | 숫자·enum·ID·시간 경계값과 protocol v1 거부 | 통과 |
| ICD-TC-003 | UDP loopback 송수신·재시작·최대 datagram | 통과 |
| ICD-TC-004 | 등록 source IP + 광고 command port 라우팅 | 통과 |
| ICD-TC-005 | 미등록·Endpoint/session 불일치 패킷 거부 | 통과 |
| ICD-TC-006 | 등록 갱신·lease·해제·보존기간 정리 | 통과 |
| ICD-TC-007 | 자산별 Heartbeat 단절·복구·Pose 재동기화 | 통과 |
| ICD-TC-008 | 관측별 detection ID→전역 track ID 매핑 | 통과 |
| ICD-TC-009 | PROJECT_FRAME→타격 상대좌표·Pan/Tilt | 통과 |
| ICD-TC-010 | 자동/수동 할당과 독점 자산 이중할당 방지 | 통과 |
| ICD-TC-011 | 탐지→자동 할당→POINT→ARM→START | 통과 |
| ICD-TC-012 | ACK 진행상태·중복·timeout·재전송·소진 | 통과 |
| ICD-TC-013 | session 교체 pending 종료와 늦은 패킷 거부 | 통과 |
| ICD-TC-014 | 특정/전체 STOP·ESTOP와 반복 ID 불변 | 통과 |
| ICD-TC-015 | 관측 SCAN/STOP/HOME 상태전이 | 통과 |
| ICD-TC-016 | 저장소/할당/ACK 경합 동시성 | 통과 |
| ICD-TC-017 | 잘못된 실행 설정과 용량 상한 | 통과 |
| ICD-TC-018 | 관측 2 + 타격 3 UDP, 자산 재시작 session 교체 | 통과 |
| ICD-TC-019 | 실제 LiDAR 로컬→PROJECT_FRAME 변환 | 실제 관측 HW/SW 필요 |

## 의도적으로 남은 항목

- 실제 LiDAR SDK, calibration, 원시 데이터 처리와 탐지 알고리즘
- 실제 Pan/Tilt 모터, limit/encoder/feedback 및 레이저 출력 제어
- 정식 GUI와 C2 프로세스 간 API/IPC 계약, 화면 및 운용 승인 흐름
- 암호학적 자산 인증, 전송 보안, 키 관리, 권한 분리와 감사 로그
- 배포별 실제 IP, 시간 동기화, 지향 오차와 End-to-End 성능 승인 기준
- 실제 교전 규칙과 사람의 공격 승인 조건

더미 자산은 인터페이스와 안전 상태전이를 검증하기 위한 시험 대역이며 실제 장비
알고리즘이나 무기 안전성 검증 완료 근거로 사용하지 않는다.
