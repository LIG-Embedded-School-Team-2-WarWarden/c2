# 통제소 서버 운영·진단 절차

## 기동과 종료

README의 Release 빌드 후 `c2_server.exe --bind 127.0.0.1 --asset-port 5000`로 시작한다.
공용 ingress 큐 기본값은 1024 packets / 4194304 payload bytes다.
`--ingress-queue-capacity N --ingress-queue-bytes N`으로 조정한다. 0은 기동 전에 거부한다.
호환 역할별 포트는 기존 직접 처리 경로를 유지하고 공용 ingress 큐 지표에 포함하지 않는다.

종료는 콘솔 `quit`을 사용한다. 서버는 주기 worker와 수신을 중단하고 admitted ingress
작업을 drain/join한 뒤 송신 transport를 닫는다. drain은 handler 반환을 전제로 한다.

## 관찰

`metrics`는 공용 ingress의 누적 지표와 송신/주기 worker 상태를 한 줄로 출력한다.
큐 지표는 한 잠금 아래 snapshot이고 UDP atomic counters는 필드 간 근사 snapshot이다.
재시작 start/stop은 counters를 초기화하지 않으며 프로세스 재기동 때 초기화된다.
`queue_processed`는 성공 여부와 관계없이 handler 실행 완료 횟수다.
`queue_handler_errors`는 처리 예외이며 protocol 거부 건수와 다르다.

| 관찰 | 다음 확인 |
|---|---|
| queue_depth/queue_bytes 증가, queue_wait_max_us 증가 | 느린 처리나 burst 여부, 자산 packet 빈도와 profiling 확인 |
| queue_dropped_full 증가 | application admission 손실. 입력 빈도를 먼저 줄이고 메모리/지연 budget 내에서 한도를 조정 |
| rx_errors 증가 | Winsock 오류 확인. rx timeout은 집계하지 않으며 ICMP 오류는 정상적인 자산 종료에서도 가능 |
| ingress_running=0 | 치명적 수신 오류 또는 종료. 서버 재기동 전 포트/네트워크 환경 확인 |
| rx_handler_errors/queue_handler_errors/worker_errors 증가 | callback/업무 처리/주기 작업 예외. last known 상태와 pending/outcomes를 확인 |
| tx_errors 증가 | 실제 sendto 오류. 자산 경로/Endpoint/소켓 상태 확인 |
| pending 누적 | outcomes의 delivery/completion timeout, 자산 연결과 상태 최신성 확인 |

queue_wait_max_us/high-water는 누적 최고값이라 부하가 해소되어도 내려가지 않는다.
기간별 변화는 두 시점의 counter 차이와 현재 depth를 비교한다.
관리 컨테이너/스레드/OS socket buffer 메모리는 payload byte 한도 밖에 있다.

`assets`는 연결·세션·Pose·상태, `events`는 protocol/등록/인증 거부 이력,
`outcomes`는 명령 종결 결과를 보여준다. 이력은 용량 제한 메모리 snapshot이며
영속 감사 로그가 아니다. 안전명령은 정상 명령보다 느슨한 연결 조건으로 송신하지만
UDP 전달과 실장비 정지를 보장하지 않는다.

## 장애 재현과 검사

- 전체 테스트: `ctest --test-dir build -C Release --output-on-failure`.
- 프로세스 통합: `scripts/run_udp_smoke.ps1 -BinDir ./build/Release`.
- 큐 포화/byte 한도/FIFO/drain/재시작: DatagramProcessorTest.
- consumer 예외 이후 UDP 지속: UdpTransportMetricsTest.
- 늦은 ACK, 명령 timeout, session 교체: CommandTrackerTest와 ServerRuntimeTest.

현재 도구는 콘솔 기반 진단이다. production 외부 metric endpoint,
인증/권한, 자동 restart 및 장비 승인 체계는 추가 요구사항과 배포 환경에 따라 설계한다.


## 명령 부분 실패와 영속 이벤트

송신 함수의 성공은 UDP 전달이나 실행 완료를 의미하지 않는다. `delivery_uncertain`은
송신 예외가 발생하여 전달 여부를 확정할 수 없는 상태다. dynamic 명령은 pending을 유지하고
동일 `(asset_id, session_id, command_id)`로 한도 내 재전송한다. `pending`에서
SENT/UNCERTAIN/AWAITING_SEND와 ACK 단계, 시도 횟수를 확인하고 `outcomes`로 종결을 확인한다.
`pending` 목록은 dynamic tracker만 표시하며 legacy pending은 기존 전체 count에 포함된다.
필수 표적 스트림 송신이 START 전에 실패하면 START를 등록하지 않는다. START가 송신 단계에
진입하면 공격 latch를 보수적으로 유지하므로 해제 전에 STOP/ESTOP 등 운영자 조치가 필요하다.
재전송 한 건의 송신 예외는 다른 자산의 재전송을 중단하지 않으며 실패도 시도 한도에 포함된다.
ESTOP 반복 송신은 일부 실패 이후에도 나머지 반복을 실행한다.

기본 로그는 `logs/c2-events.jsonl`이다. `--event-log PATH`, `--event-log-max-bytes N`
(기본 8388608, 최소 1024), `--event-log-retained-files N`(기본 3, 범위 1..100)으로 설정한다.
현재 파일과 `.1`부터 `.N`까지 보존하며 `.1`이 가장 최근 archive다. 파일당 payload 한도를
적용하고 기존 파일에 append한다. 같은 경로는 한 프로세스만 사용한다. 여러 인스턴스는
별도 경로가 필요하다. `--event-log off`로 비활성화할 수 있다.

schema_version=1, run_id, event_id, recorded_at_us(UTC), timestamp_us(업무 clock),
asset_id, session_id, command_id, component_id, type, detail을 기록한다. command_prepared,
command_send_attempt(kind/명령 유형/target/endpoint),
command_send_succeeded/command_delivery_uncertain, command_outcome, inbound,
assignment/unassignment, track_stream_send_failed 이벤트를 같은 key로 연결한다.
command_outcome detail 값은 0 completed, 1 rejected, 2 failed, 3 expired,
4 delivery_exhausted, 5 completion_timeout, 6 session_ended, 7 cancelled_before_send다.
세션 등록·교체·거부와 ACK 결과는 inbound의 kind/result/ack와 identity로 추적한다.

각 이벤트를 동기적으로 flush하므로 로그 I/O는 처리 지연과 큐 대기에 영향을 준다.
flush는 fsync나 전원 장애 내구성을 보장하지 않는다. 전원 장애의 마지막 부분 레코드는
불완전할 수 있으며 분석 시 JSON 파싱 실패를 구분해야 한다. 로그는 복구용 상태 저장소가
아니므로 재기동 시 pending을 자동 복원하지 않는다. 시작 시 파일을 열지 못하면 기동을
실패시키며 실행 중 쓰기·회전 실패와 과대 레코드는 `event_log_write_errors`로 집계한다.
`metrics`의 event_log_written/rotations도 확인한다. callback 예외는 명령 상태 전이에
전파하지 않는다. EventSink는 동시 호출 가능하고 빠르게 반환해야 하며 Runtime 수명주기
메서드로 재진입하면 안 된다. tracker 이벤트 자체는 tracker 잠금 밖에서 호출한다.

실제 socket 장애 재현은 `scripts/run_udp_reliability_probe.ps1`을 실행한다.
200개 STOP의 burst와 명령/ACK 손실·중복·순서 변경을 측정하며 결과는
`build/udp-reliability.json`에 저장한다. 완료율과 simulated unique execution, handler 오류를
검증하고 ACK 지연·queue high-water/drop을 보고한다. 큐 포화의 결정적 검증은 별도
DatagramProcessorTest가 담당하며 실제 UDP 실행에서 drop이 항상 발생하도록 강제하지 않는다.
