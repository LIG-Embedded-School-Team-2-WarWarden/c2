# 프로젝트–Confluence 정합성 확인 (2026-10-05)

초기 조사 판정: **핵심 v3 wire 필드와 주요 기본값은 일치하지만 요구사항 추적과 최신 설계·운영·증거 설명을 동기화해야 했다.**

## 수정 후 상태

아래 조사 결과는 수정 전 기록이다. 2026-10-05 후속 작업에서 코드로 확정되는 항목을
수정했다. 요구사항의 의미·정식 승인 상태와 과거 날짜의 시험 기록은 보존했다.

| 항목 | 조치 |
|---|---|
| A01 추적 ID | 로컬 구현 표를 C2-IMPL-001~014로 분리, 원래 SW-C2 요구사항별 구현 연결을 로컬/Confluence RTM에 추가 |
| A02 경로 | ICD 참조를 공용 submodule의 실제 schema/model 경로로 정정 |
| A03 실패 처리 | START 준비 실패와 전달 불확실성, retry 격리, 세션 퇴역 및 운영 기록 설명 추가 |
| A04 설계 | 현재 facade·라우터·명령 서비스·tracker·ingress·EventSink 구성과 동시성 추가, 기존 계산 배분 예시를 역사적 내용으로 명시 |
| A05 오류 | 0x5003/0x5004 발생 조건, 0x5005 선언만 존재함을 구분하여 기록 |
| A06 증거 | C2/shared commit, 서버 213 + 공용 30 시험과 CI·UDP smoke/probe 근거를 DT/검증/RTM에 추가 |
| A07 용어 | C2/Confluence ADR 인용 범위, 운영 JSONL과 보안 감사의 차이, 유효 Heartbeat 기준 단절 판단 정리 |

수정한 Confluence 페이지와 확인한 version:

| 페이지 | version |
|---|---|
| [SRS](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/491548) | 15 |
| [통합설계](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/491568) | 6 |
| [C2 상세설계](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/524320) | 5 |
| [제품 구조 매핑](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/6979592) | 4 |
| [기술 가이드](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/3375106) | 4 |
| [RTM](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/65697) | 7 |
| [DT](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/131102) | 6 |
| [결정 로그](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/557060) | 6 |
| [ICD 개요](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10649601) | 11 |
| [ICD 데이터](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10682369) | 30 |
| [ICD 운용](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10649641) | 21 |
| [ICD 검증](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10715137) | 22 |

변경 후 12개 페이지를 다시 읽어 본문, 기존 task ID·checkbox·panel의 보존을 확인했다.
날짜 표시와 새 표의 thead/tbody 정규화 외에는 제출 HTML과 일치하는지 확인했다.
데이터 필드 119개를 현행 schema와 재대조했다. 실행 코드 변경은 없고 기존 CI 근거를 연결했다.

WPF 초안의 단일 자산·고정 표적·270° 가정은 실제 목표 사양인지 사용자에게 질문했다.
응답 전 해당 두 초안은 수정하지 않는다. GUI IPC·실장비 사양·시간 동기화·Gate 승인은
코드만으로 결정할 수 없으므로 현재 미확정/미구현 상태를 유지한다.

## 확인 기준과 범위

- 프로젝트: `C:/Users/user/Documents/projects/c2`, main `9af1529a2cfa19ef11b13bcd096dfb3af4ad797f`.
- 공용 프로토콜: `external/warwarden-protocol`, 고정 커밋 `711d071df742e251dd4d9304fb2c0baf1c65379c`.
- Confluence: MFS 공간의 current 페이지 60개를 목록 조회 후 각각 본문·version으로 읽었다. 핵심 ICD 상위/하위 6개, C2/통합 설계, SRS, RTM, DT, 기술 가이드, WPF 초안을 코드 및 로컬 문서와 대조했다.
- 확인은 문서와 코드의 정합성 조사다. Confluence, 코드, 기존 로컬 문서는 수정하지 않았다. 이 보고서와 읽기 전용 조사 결과만 새로 작성했다.
- 첨부 PDF/스프레드시트, whiteboard·이미지의 시각 요소, 별도 WPF/실장비 저장소와 HW 실제 동작은 검증하지 않았다. 학교 실습용 더미의 PASS를 실장비 승인으로 해석하지 않았다.
- `docs/icd/ICD-before-editorial-cleanup-2026-10-03.md`는 명시적 과거 백업이고 `docs/confluence/MFS-continuous-tracking-audit.md`는 9월 30일 조사 기록이다. 두 파일을 현행 규격으로 보거나 현재 Confluence와 바이트 일치를 요구하지 않았다.
- 프로토콜 필드표는 스키마 파싱과 Markdown 표 비교, 동작·설정·추적은 본문과 구현 읽기로 확인했다. 이번 조사에서 추가로 테스트를 실행하지는 않았다. 직전 통합 검증과 CI 기록을 확인 근거로 연결했다.

## 일치한 항목

| 확인 항목 | 결과와 근거 |
|---|---|
| protocol_version=3, mfs.icd.v3 | 현행 공용 모델·스키마와 ICD 일치 |
| 메시지 필드 | MessageHeader 및 14개 payload, 합계 119개 필드의 번호·이름·Protobuf 자료형이 모두 동일. 직접 비교에서 누락·추가·타입 불일치 0 |
| TargetCoordinate/TargetTrackUpdate | 위치·속도·measurement_time, velocity_valid의 미제공/정지 구분, 전역 track_id 및 할당 자산·세션 스트림이 일치 |
| 책임 경계 | 현재 ICD는 관측 자산이 PROJECT_FRAME을 생성하고 C2가 관리·할당·전달하며 포인터 자산이 dead reckoning/상대좌표/Pan·Tilt를 계산한다고 정의. 정상 연속추적 경로와 일치. 수동·진단 지향 경로는 유지 |
| DevelopmentPoseCommand | Envelope oneof 14, capability bit 3, PROJECT_FRAME, 유한 좌표·방위, 현재 세션, 정지 상태 적용, ACK/Pose 보고, 재시작 옵션값 복원이 일치 |
| ACK 수락/완료 기한 | 최초 진행 ACK 전 valid_until, 이후 첫 진행 ACK에서 시작한 별도 completion timeout, 반복 ACK 비연장, 종결 ACK 및 경계 시각 만료가 일치 |
| UDP 기준 경로 | 공용 ingress 기본 5000과 등록 command_port 송신이 일치. 기존 역할별 포트가 코드에 남은 사실도 ICD가 정리 대상으로 명시 |
| 안전·연속 추적 | READY/aligned/armed 및 최신 상태 검사, duration_ms 출력 종료와 추적 종료 분리, STOP/ESTOP 및 표적·통신 이상 시 더미 safe_stop 규칙이 일치 |
| 성능 승인 범위 | 실제 센서·모터·레이저와 시간 동기화·실측 정확도·정식 GUI는 미완료/TBD라고 구분하여 저장소 범위와 일치 |

직접 필드표가 없는 `Envelope` 14개 oneof와 `PanTiltLimits` 4개 필드는 위의 119개 비교 숫자에 포함하지 않았다. DevelopmentPose의 Envelope 번호 14는 문장과 스키마를 별도로 대조했다. 전체 enum 숫자·모든 packet에 대한 새로운 golden 시험을 실행했다는 의미가 아니다.

### 기본값 대조

[ICD 운용·성능](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10649641) 15절과 [ServerOptions](/C:/Users/user/Documents/projects/c2/C2.Core/include/c2/server_options.hpp:25), 더미 실행 옵션을 비교했다.

| 항목 | Confluence | 코드 | 판정 |
|---|---:|---:|---|
| 더미 표적 갱신 | 1000 ms | 1000 ms | 일치 |
| 더미 상태 보고 | 100 ms | 100 ms | 일치 |
| C2 Heartbeat | 1000 ms | 1000 ms | 일치 |
| Heartbeat timeout | 3000 ms 초과 | 3000 ms 초과 | 수치·경계 일치; 기준 표현은 아래 보완 |
| 상태 최신성 | 1000 ms | 1000 ms | 일치 |
| 트랙 유효시간 | 2000 ms | 2000 ms | 일치 |
| 최대 전체 트랙 | 256 | 256 | 일치 |
| 관측별 최대 트랙 | 64 | 64 | 일치 |
| 명령 수락 기한 | 500 ms | 500 ms | 일치 |
| delivery ACK timeout | 200 ms | 200 ms | 일치 |
| 최초 포함 시도 | 3 | 3 | 일치 |
| completion timeout | 2000 ms | 2000 ms | 일치 |
| ESTOP 동일 ID 반복 | 3 | 3 | 일치 |

## 수정이 필요한 항목

### A01 — 요구사항 ID의 의미가 로컬과 Confluence에서 다름 (높음)

[Confluence SRS](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/491548) 및 [Confluence RTM](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/65697)과 [로컬 추적표](/C:/Users/user/Documents/projects/c2/docs/traceability/ICD-RTM.md:11)가 같은 식별자를 서로 다른 요구에 사용한다.

| ID | Confluence SRS | 로컬 구현 추적표 |
|---|---|---|
| SW-C2-001 | 표적 보고 수신·검증 | 다중 자산 등록·세션 |
| SW-C2-004 | 자산 위치·자세 관리 | 표적 상태 조회 |
| SW-C2-005 | 좌표변환 | 자동·수동 할당 |
| SW-C2-006 | Pan/Tilt 계산 | 할당 상실·재할당 안전 |
| SW-C2-008 | 타격 명령 UDP 전송 | 관측 지향·탐색 |
| SW-C2-010 | 관측 SCAN/STOP/HOME 명령 | ACK 상태기계 |
| SW-C2-011 | 시간 동기화 상태·시각 편차 확인 | 운용 상태·결과 조회 |

SW-C2-002/003/007/009 역시 제목·범위가 서로 다르다. 로컬 SW-C2-012~014는 SRS 요구사항 표에 없고, 개발 Pose의 SW-C2-014는 ICD 검증 페이지에만 별도로 연결되어 있다. v3 대체 절은 계산 책임을 설명하지만 이 ID 재사용을 해소하는 추적 mapping을 제공하지 않는다.

영향: 같은 ID를 검색하면 다른 구현·시험이 연결된다. 특히 로컬 SW-C2-011 완료 상태를 Confluence의 시간 동기화 요구 완료로 읽으면 잘못된 검증 판정이 된다.

조치: 권위 SRS ID를 보존하여 명시적 개정·allocation 이력과 구현/시험 mapping을 만들거나, 로컬 행을 별도 구현 식별자로 구분하고 SRS 연결 열을 추가한다. 단순 문구 치환으로 기존 요구와 증거를 덮어쓰지 않는다.

### A02 — 삭제된 스키마·헤더 경로를 현행 원본으로 가리킴 (보통)

[ICD 개요](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10649601) 3절은 `protocol/mfs.proto`, [메시지 정의](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10682369) 11절은 이 경로와 `C2.Core/include/c2/protocol.hpp`를 원본으로 지목한다. 두 파일은 PR #63 병합 후 없다.

현재 원본:
- [공용 스키마](/C:/Users/user/Documents/projects/c2/external/warwarden-protocol/proto/mfs/icd/v3/mfs.proto:1)
- [공용 C++ 모델](/C:/Users/user/Documents/projects/c2/external/warwarden-protocol/cpp/include/c2/protocol.hpp:1)
- [.gitmodules](/C:/Users/user/Documents/projects/c2/.gitmodules:1) 및 C2 gitlink가 고정한 공용 저장소 커밋

조치: Confluence에 새 경로·공용 저장소·고정 커밋과 `git submodule update --init --recursive` 절차를 반영한다. 필드 자체는 위에서 확인한 대로 동일하다.

### A03 — 최근 명령 부분 실패·관측 규칙이 Confluence에 없음 (보통)

[ICD 운용](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10649641)과 [C2 상세설계](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/524320)는 ACK 기한을 설명하지만 PR #67의 다음 규칙을 설명하지 않는다.

- 송신 예외의 `delivery_uncertain`과 ACK 단계의 구분, 동일 key pending 유지 및 bounded retry.
- 필요한 stream 송신 실패 시 START 미등록, 등록 후 latch 실패 시 미송신 명령만 취소.
- START 전송 단계에서 보수적 latch 유지 및 운영자 조치 필요.
- 개별 자산 send 실패가 후속 retry나 ESTOP 반복을 중단하지 않음.
- assign/unassign/retry/session 교체/표적 갱신의 수명주기 직렬화.
- `pending` 명령, JSONL append/flush·회전 보존·쓰기 오류 metrics, callback 예외 격리.

근거: [런타임](/C:/Users/user/Documents/projects/c2/C2.Core/src/server_runtime.cpp:225), [명령 tracker](/C:/Users/user/Documents/projects/c2/C2.Core/src/command_tracker.cpp:180), [운영 절차](/C:/Users/user/Documents/projects/c2/docs/operations.md:1), [ADR-011](/C:/Users/user/Documents/projects/c2/docs/decisions/ADR-011-command-partial-failure-and-events.md:1).

이는 기존 wire contract의 변경이 아니라 최신 내부 동작·운영 설명의 누락이다. 상세설계와 운영 자료에 동기화하고, ICD에는 필요한 외부 관찰 규칙만 반영한다.

### A04 — 상세설계의 현행 구현 구조가 오래됨 (보통)

[C2 상세설계](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/524320) v4는 구 CoordinateTransformer/PanTiltCalculator 중심 CSU, `Framework MFC/WPF TBD`, `Language C++/C# TBD`, Queue/Dispatcher 초안을 유지한다. v3 책임 대체 절은 있으므로 옛 계산 책임을 현재의 확정 규칙이라고 단정하지는 않는다. 그러나 본문 구조는 현재 코어 구현 설명으로 사용할 수 없다.

현재 구현은 C++20 Windows 서버이며 `ServerRuntime` facade, `AssetMessageRouter`/`LegacyMessageRouter`, `AssignmentCoordinator`, 명령 tracker·identity, 상태 저장소·publisher·UDP 어댑터로 나뉜다. 공용 ingress에 bounded `DatagramProcessor`(1024 datagrams / 4 MiB payload)가 있고 `EventSink`/`EventLog`가 있다. GUI 선택/연동 미결과 이미 구현된 서버 기술을 분리해야 한다.

근거: [ADR-009](/C:/Users/user/Documents/projects/c2/docs/decisions/ADR-009-runtime-command-responsibilities.md:1), [ADR-010](/C:/Users/user/Documents/projects/c2/docs/decisions/ADR-010-bounded-ingress-and-operability.md:1), [queue](/C:/Users/user/Documents/projects/c2/C2.Core/include/c2/datagram_processor.hpp:10).

[SRS](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/491548), [통합 설계](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/491568), [CSCI 매핑](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/6979592)의 위쪽 본문에도 이전 계산 책임이 남아 있다. 명시적인 v3 supersession이 있어 현재 ICD와의 활성 계약 충돌로 집계하지 않았지만, 현행 절과 과거 이력을 분리할 필요가 있다.

### A05 — 연속추적 오류코드 표 누락 (보통)

[메시지 정의](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10682369) 11.15는 0x5002까지만 표로 설명한다. [더미 오류 정의](/C:/Users/user/Documents/projects/c2/C2.Core/include/c2/dummy_assets.hpp:22)와 제어 루프는 다음 값을 추가로 사용한다.

- 0x5003 `target_expired`: 표적 만료 또는 최대 예측시간 초과.
- 0x5004 `invalid_target_state`: Pose/표적 상태 이상.
- 0x5005 `track_mismatch`도 상수로 선언되지만 현재 C2 소스에서 실제 발생 경로를 확인하지 못했다. 사용 중인 값으로 단정하지 말고 예약/미사용 여부를 문서화해야 한다.

조치: 실장비 코드와 혼동하지 않도록 개발용 오류 표와 TrackingStopReason 연결을 보완한다.

### A06 — 최신 형상·시험 증거 연결 부족 (보통)

[RTM](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/65697)의 마지막 구현 스냅샷은 2026-09-27이며 자동할당 미완료를 기록한다. [DT](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/131102)는 동일 날짜의 125개 시험과 단일 자산 시험을 기록한다. 날짜가 명시된 과거 기록 자체는 잘못된 결과가 아니며 삭제할 필요가 없다.

다만 최신 형상은 서버 213개 + 공용 프로토콜 30개 시험으로 분리되었고, 2 관측/3 포인터 프로세스 smoke와 실제 UDP burst/손실·중복·순서 변경 probe가 통과했다. 자동할당, 경쟁·실패 처리, 실측 JSON을 가리키는 새 기준선이 없다.

근거: [PR #63 CI](https://github.com/LIG-Embedded-School-Team-2-WarWarden/c2/actions/runs/37289261282), [UDP 실측](/C:/Users/user/Documents/projects/c2/docs/portfolio/measurements/2026-10-05-udp.json:1), [측정 범위](/C:/Users/user/Documents/projects/c2/docs/portfolio/benchmark.md:1).

조치: 과거 기록을 보존하고 새 commit/test/evidence 스냅샷을 추가한다. 테스트 분리로 서버 숫자가 줄었다는 점을 설명한다. 물리 장비 E2E 성능이나 공식 Gate 승인으로 올리지 않는다.

### A07 — ADR 번호와 로컬 미결 항목 표현 보완 (보통/낮음)

[Confluence 의사결정 로그](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/557060)의 ADR-005는 Pan/Tilt 책임 결정이다. 로컬 [ADR-005](/C:/Users/user/Documents/projects/c2/docs/decisions/ADR-005-command-ack-state-machine.md:1)는 자산·세션별 ACK 상태기계이다. 두 ADR은 같은 이름의 문서가 아니며 ID만으로 인용하면 혼동한다. namespace·원본 링크 또는 대응표가 필요하다.

[로컬 RTM](/C:/Users/user/Documents/projects/c2/docs/traceability/ICD-RTM.md:57)은 감사 로그를 잔여 작업에 포함한다. 현재 JSONL 운영 이벤트 로그는 구현되어 있다. 보안·권한·위변조 방지·내구성을 갖춘 감사 체계는 여전히 미완료이므로, 운영 이벤트 로그 완료와 보안 감사 체계 미완료를 구분해서 적어야 한다.

[ICD 운용](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10649641) 15절의 “마지막 정상 수신”은 실제 `last_heartbeat_received_at_us` 기준이라고 명시하면 좋다. 일반 telemetry 수신이 Heartbeat timeout을 연장하는 것은 아니다.

## 불일치로 오인하지 않은 항목

- [WPF 인계 초안](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/18513927)은 첫 문장에서 예상·임의 초안이라고 명시하고 단일 자산/고정 목표/270도 핑퐁을 가정한다. 현재 서버는 다중 자산·이동 표적·등록 기구한계를 지원한다. 이는 확정 계약 간 충돌이 아니라 아직 정합화하지 않은 GUI 기획이다.
- [WPF 인터페이스 구성 요소](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/18481153)의 TCP/UDP, JSON/Binary 및 API는 TBD 초안이다. C2에 해당 GUI transport/API는 구현되지 않았으며 현재 ICD도 GUI 연동을 잔여 작업으로 명시한다. 이를 연결 완료로 표현할 근거는 없다.
- 조사 마지막에 새로 확인된 [UI 관측 영역 요구사항](/C:/Users/user/Documents/projects/c2/docs/requirements/UI-observation-coverage.md:1)은 미커밋 초안이다. 지도 지향/Tilt 미리보기와 시야각·감지거리 모델을 정의하지만 GUI↔C2 계약은 KAN-38 협의 대상으로 명시한다. 현재 Confluence에 UI-OBS-01~09의 대응표는 없다. 확정된 구현 불일치로 판정하지 않고, 초안 승인 시 SRS/WPF 문서와 연결할 항목으로 구분한다. 이 파일을 수정하지 않았다.
- SCAN의 실장비 범위·패턴이 미정이고 더미는 상태만 변경한다는 ICD 설명은 현재 구현과 맞는다.
- 과거 조사와 DT 기록, 날짜가 있는 회의록, 일반 작성 템플릿, HW 조사·배선·제조사 자료는 현행 C2 요구나 wire 계약의 원본으로 취급하지 않았다.

## 동기화 우선순위

1. 요구사항 ID와 승인/추적 mapping을 정리한다.
2. 삭제된 원본 경로를 공용 저장소·고정 커밋으로 바꾼다.
3. 상세설계의 현재 클래스·queue·로그·명령 부분 실패 규칙을 반영한다.
4. 연속추적 오류코드와 최신 시험·실측 증거 스냅샷을 추가한다.
5. ADR namespace와 운영 이벤트/보안 감사 구분을 정리한다.
6. 별도 WPF ICD를 실제 다중 자산/트랙·운영 상태 모델에 맞춰 확정한다.

## 조회 페이지 목록

아래 60개 페이지는 모두 본문을 읽었다. “직접 대상 아님”은 읽기 실패가 아니라 C2 저장소와 비교할 확정 기술 규격이 없거나 다른 영역의 자료라는 의미이다.

| 페이지 | ID | version | 수정시각(UTC) | 분류 |
|---|---:|---:|---|---|
| [05 요구사항 추적표 (RTM)](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/65697) | 65697 | 6 | 2026-09-30T03:11:58.019Z | 설계·요구·추적: 갱신 필요 |
| [대공방어 무기체계 프로젝트 개발 문서](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/131081) | 131081 | 7 | 2026-09-30T03:12:23.542Z | 상위 가이드/예시: v3 우선 및 실장비 범위 구분 |
| [06 개발시험평가 결과서 (DT)](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/131102) | 131102 | 5 | 2026-09-30T03:12:21.950Z | 설계·요구·추적: 갱신 필요 |
| [WarWarden Project Home](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/295029) | 295029 | 2 | 2026-09-23T08:50:23.863Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [템플릿 - 프로젝트 계획](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/295085) | 295085 | 1 | 2026-09-09T09:45:47.422Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [템플릿 - 미팅 메모](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/295098) | 295098 | 1 | 2026-09-09T09:45:47.543Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [템플릿 - 주간 상태 보고서](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/295111) | 295111 | 1 | 2026-09-09T09:45:47.630Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [04 시험·검증 계획서 (TRR)](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/295136) | 295136 | 5 | 2026-09-30T03:12:20.255Z | 상위 가이드/예시: v3 우선 및 실장비 범위 구분 |
| [03 인터페이스 명세서 (ICD)](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/360475) | 360475 | 4 | 2026-09-30T03:12:16.006Z | 상위 가이드/예시: v3 우선 및 실장비 범위 구분 |
| [02-A CSCI-01 감시 SW 상세설계](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/393235) | 393235 | 2 | 2026-09-17T05:46:54.466Z | 상위 가이드/예시: v3 우선 및 실장비 범위 구분 |
| [팀 프로젝트 관련 논의 사항](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/393269) | 393269 | 2 | 2026-09-10T08:37:46.232Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [08 최종 시현·Validation 및 발표 체크리스트](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/458770) | 458770 | 3 | 2026-09-30T03:12:14.084Z | 상위 가이드/예시: v3 우선 및 실장비 범위 구분 |
| [02-C CSCI-03 타격 제어 SW 상세설계](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/458807) | 458807 | 3 | 2026-09-30T03:12:12.357Z | 상위 가이드/예시: v3 우선 및 실장비 범위 구분 |
| [07-A Architecture Decision Record (ADR) 상세 템플릿](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/458838) | 458838 | 2 | 2026-09-17T05:54:10.950Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [00 프로젝트 운영 및 산출물 관리](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/491528) | 491528 | 6 | 2026-09-30T03:12:10.209Z | 상위 가이드/예시: v3 우선 및 실장비 범위 구분 |
| [01 요구사항 정의·분석서 (SRR·SFR·SSR)](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/491548) | 491548 | 14 | 2026-09-30T03:12:08.162Z | 설계·요구·추적: 갱신 필요 |
| [02 통합 설계서 (PDR·CDR)](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/491568) | 491568 | 5 | 2026-09-30T03:12:06.262Z | 설계·요구·추적: 갱신 필요 |
| [02-B CSCI-02 통제 SW 상세설계](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/524320) | 524320 | 4 | 2026-09-30T03:12:03.859Z | 설계·요구·추적: 갱신 필요 |
| [07 회의·의사결정·변경·리스크 로그](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/557060) | 557060 | 5 | 2026-09-30T03:12:02.101Z | 설계·요구·추적: 갱신 필요 |
| [04-A 시험 케이스 상세 작성 템플릿](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/557095) | 557095 | 3 | 2026-09-30T03:12:00.077Z | 상위 가이드/예시: v3 우선 및 실장비 범위 구분 |
| [00-A V-Model 기반 반복·점진적 Agile 개발 프로세스](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/655361) | 655361 | 2 | 2026-09-17T05:42:59.029Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [🚀 START HERE — 방산 V-Model + Agile 프로젝트 실무 가이드](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/655402) | 655402 | 3 | 2026-09-17T06:32:01.073Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [그라운드룰](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/1409027) | 1409027 | 2 | 2026-09-16T12:32:25.268Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [멘토링 시 질문해야될 것들](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/2916522) | 2916522 | 12 | 2026-09-19T08:29:08.839Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [00-D 개발 기술 스택 및 개발자 테스트 전략](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/3375106) | 3375106 | 3 | 2026-09-30T03:12:18.044Z | 설계·요구·추적: 갱신 필요 |
| [Unity_시뮬레이션_환경_조사](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/3506180) | 3506180 | 3 | 2026-09-15T03:22:42.903Z | HW/센서 조사: C2 계약 비교의 직접 대상 아님 |
| [Pan/Tilt 터렛 구동부 1차 설계](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/3997698) | 3997698 | 27 | 2026-09-18T16:39:35.539Z | HW/센서 조사: C2 계약 비교의 직접 대상 아님 |
| [FLASH LIDAR 조사](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/5996558) | 5996558 | 1 | 2026-09-16T07:26:38.646Z | HW/센서 조사: C2 계약 비교의 직접 대상 아님 |
| [02-D CSCI·CSC·CSU 설계·요구사항 매핑](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/6979592) | 6979592 | 3 | 2026-09-30T03:11:55.995Z | 설계·요구·추적: 갱신 필요 |
| [01-A Software Requirements Specification (SRS) 작성 가이드](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/7241729) | 7241729 | 3 | 2026-09-30T03:11:54.357Z | 상위 가이드/예시: v3 우선 및 실장비 범위 구분 |
| [00-B 방산 SW 제품구조(CSCI·CSC·CSU)와 Agile 적용 가이드](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/7274497) | 7274497 | 3 | 2026-09-30T03:11:52.659Z | 상위 가이드/예시: v3 우선 및 실장비 범위 구분 |
| [00-C Sprint 운영 템플릿 — 방산 V-Model + Agile](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/7307277) | 7307277 | 1 | 2026-09-17T05:07:28.984Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [00-E 방산 SW/SE 용어·약어 기준집](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/7471130) | 7471130 | 2 | 2026-09-30T03:11:50.600Z | 상위 가이드/예시: v3 우선 및 실장비 범위 구분 |
| [07-C 회의록 기본 양식 — 복제해서 사용](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/8060930) | 8060930 | 1 | 2026-09-18T00:11:01.813Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [07-B 회의록 운영 및 정리](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/8159402) | 8159402 | 1 | 2026-09-18T00:09:33.995Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [[MFS-ICD-001] 관측 자산–통제소–포인터 자산 인터페이스 통제 문서](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/9306122) | 9306122 | 13 | 2026-10-03T11:27:47.217Z | 핵심 ICD 직접 대조 |
| [모터 리스트 자료조사](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/9371662) | 9371662 | 8 | 2026-09-21T02:49:45.202Z | HW/센서 조사: C2 계약 비교의 직접 대상 아님 |
| [Daily Meeting 양식](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/9371705) | 9371705 | 2 | 2026-09-20T23:46:14.259Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [09월21일](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/9666659) | 9666659 | 6 | 2026-09-21T00:48:02.791Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [프로젝트 소개](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10158081) | 10158081 | 1 | 2026-09-21T00:39:38.656Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [09월23일](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10420235) | 10420235 | 4 | 2026-09-23T00:25:38.012Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [[MFS-ICD-001-01] 개요 및 상위 인터페이스 요구사항](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10649601) | 10649601 | 10 | 2026-10-05T07:59:49.706Z | 핵심 ICD 직접 대조 |
| [[MFS-ICD-001-02] 네트워크·논리 인터페이스·좌표계](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10649621) | 10649621 | 23 | 2026-10-05T07:59:51.709Z | 핵심 ICD 직접 대조 |
| [[MFS-ICD-001-04] 운용 시퀀스·상태·안전·성능](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10649641) | 10649641 | 20 | 2026-10-05T08:00:21.319Z | 핵심 ICD 직접 대조 |
| [[MFS-ICD-001-03] 데이터 인터페이스 및 Protobuf 메시지](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10682369) | 10682369 | 29 | 2026-10-05T07:59:53.524Z | 핵심 ICD 직접 대조 |
| [[MFS-ICD-001-05] 검증·추적성·형상 및 변경관리](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10715137) | 10715137 | 21 | 2026-10-05T07:59:57.268Z | 핵심 ICD 직접 대조 |
| [09월22일](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10813441) | 10813441 | 3 | 2026-09-22T00:33:21.717Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [사본 Daily Meeting 양식](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/10911819) | 10911819 | 1 | 2026-09-22T06:50:00.990Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [Git Commit Convention](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/11993132) | 11993132 | 1 | 2026-09-23T08:49:12.433Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [09월24일](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/11993153) | 11993153 | 1 | 2026-09-23T14:58:52.173Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [09월28일](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/13467743) | 13467743 | 2 | 2026-09-28T00:16:58.963Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [서보 모터 자료](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/14090241) | 14090241 | 2 | 2026-09-28T08:34:29.701Z | HW/센서 조사: C2 계약 비교의 직접 대상 아님 |
| [Zynq 핀맵·HW 인터페이스 및 배선표](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/15073283) | 15073283 | 5 | 2026-10-01T04:27:26.041Z | HW/센서 조사: C2 계약 비교의 직접 대상 아님 |
| [09월29일](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/15204354) | 15204354 | 2 | 2026-09-29T00:37:51.937Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [09월30일](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/17235969) | 17235969 | 2 | 2026-09-30T00:33:22.110Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [WPF 인터페이스 구성 요소](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/18481153) | 18481153 | 2 | 2026-09-30T10:22:39.578Z | WPF 초안: 현재 C2 미구현 영역 |
| [[인계자료] WPF - Backend 간 통신 함수 정리](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/18513927) | 18513927 | 5 | 2026-10-05T07:09:14.998Z | WPF 초안: 현재 C2 미구현 영역 |
| [10월01일](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/18939905) | 18939905 | 3 | 2026-10-01T00:28:04.810Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [10월02일](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/19988482) | 19988482 | 3 | 2026-10-02T00:09:51.687Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |
| [10월 6일자 멘토링 정리](https://ligproject2team.atlassian.net/wiki/spaces/MFS/pages/20414465) | 20414465 | 3 | 2026-10-02T07:40:48.558Z | 운영·템플릿·회의 이력: 현행 C2 규격 아님 |

## 조사 산출물

- `build/confluence-audit-live.json`: 60개 페이지의 읽은 본문/version snapshot. Markdown 변환은 날짜 노드·매크로·그림을 완전히 표현하지 않으므로 원본 페이지 링크도 함께 참조한다.
- `build/confluence-audit-results.json`: 필드 비교, 삭제 경로 참조, 코드 기본값, 요구사항 ID 대조, 페이지 version 목록.
- `build/audit_confluence.py`: 본문 snapshot과 고정 코드 형상으로 정적 비교를 재현하는 읽기 전용 스크립트.
