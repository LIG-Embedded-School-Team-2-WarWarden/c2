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

현재 도구는 콘솔 기반 진단이다. production 외부 metric endpoint, 영속 structured log,
인증/권한, 자동 restart 및 장비 승인 체계는 추가 요구사항과 배포 환경에 따라 설계한다.
