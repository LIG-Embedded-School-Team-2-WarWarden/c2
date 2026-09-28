# 다중 자산 전환 분석 및 종결 상태

> 이 문서는 전환 전 발견한 단일 자산 가정과 그 해소 결과를 함께 기록한다.
> 아래 "단일 자산 가정"은 현재 구조 설명이 아니라 변경 전 기준선이다.

## 단일 자산 가정

- `StateStore`는 역할별 `observation_pose_`와 `effector_pose_` 한 개만 보관한다.
- `ConnectionMonitor`는 역할별 고정 슬롯 두 개만 사용한다.
- `TelemetryStore`는 관측·타격 상태를 각각 한 개만 보관하고 ACK 키가
  `ComponentId + command_id`다.
- `ServerRuntime`은 관측·타격 Endpoint를 하나씩 설정으로 받고 역할 기반으로
  명령을 전송한다.
- 명령 서비스는 역할별 단일 sequence/command ID 발급기와 최신 단일 상태를
  사용한다.
- 표적 저장 키가 `detection_id`뿐이어서 관측 자산 사이에서 충돌한다.
- 콘솔과 더미 프로그램은 관측 1개·타격 1개 및 고정 포트를 가정한다.

## 동시성과 재동기화

- UDP 수신기는 포트별 스레드에서 `ServerRuntime::ingest`를 호출한다.
- 저장소와 연결 감시기는 내부 mutex를 사용하지만 자산별 키 공간은 없다.
- Heartbeat 판정은 C2 수신 시각을 사용하고 재연결 후 Pose 동기화를 요구한다.
- 송신 콜백은 pending 명령 mutex 밖에서 호출되지만 Endpoint는 고정 설정이다.

## 프로토콜 영향

- 기존 v1 헤더에는 역할만 있고 자산·프로세스 세션 식별자가 없다.
- 모든 자산 발신 메시지와 C2 명령에 `asset_id/session_id`가 필요하다.
- 등록·해제 메시지, 동적 Endpoint, 전역 track ID 및 자산·세션별 ACK 키가
  추가되어야 한다.
- v1과 v2의 조용한 혼용은 금지하고 v1 패킷을 명시적으로 거부한다.

## 구현 순서

1. 프로토콜 v2 식별자와 등록 계약
2. 동시성 안전한 `AssetRegistry`와 세션 교체
3. 등록된 자산만 허용하는 수신 검증과 동적 Endpoint
4. 자산별 Heartbeat·Pose·telemetry 및 전역 track
5. 자산·세션별 pending 명령과 ACK 상태기계
6. 자동 할당과 재할당 안전 정책
7. 콘솔·더미 자산·다중 프로세스 통합
8. 문서·추적표·깨끗한 전체 검증

## 종결 결과

| 기존 간극 | 해소 결과 |
|---|---|
| 역할별 단일 상태와 고정 Endpoint | `AssetRegistry`의 asset/session별 snapshot과 동적 command Endpoint |
| detection ID 충돌 | 관측 asset/session/detection 조합을 64비트 전역 track ID로 매핑 |
| 역할+command ID ACK 키 | asset/session/command ID 복합 키와 진행·종결 상태기계 |
| 단일 타격 자산 | 안전 필터와 점수 기반 자동/수동 할당, 공격 전 재할당 |
| 고정 포트 더미 | 동적 ID/session/ephemeral port 등록과 주기적 lease 갱신 |
| 단일 자산 콘솔 | 자산별 관측·타격 명령, 전체 자산 조회와 ESTOP-ALL |
| 설정 경로 불일치 | 공통 `ServerOptions`가 레거시와 동적 경로에 같은 값을 전달 |

역할별 고정 UDP 포트와 자산 ID를 생략한 콘솔 문법은 기존 smoke/마이그레이션을
위해 남긴 호환 경계다. 새 배치는 공용 자산 포트의 protocol v2 등록 경로를 사용한다.
남은 범위는 실제 장비, GUI 프로세스 계약, 암호학적 인증과 운용 승인 기준이며
소프트웨어 다중 자산 전환의 미완료 항목으로 오인하지 않도록 RTM에서 분리한다.
